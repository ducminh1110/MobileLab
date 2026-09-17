#include "ApiServer.h"

#include "AndroidRuntime.h"
#include "ResourceScheduler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>

ApiServer::ApiServer(AndroidRuntime *runtime, ResourceScheduler *scheduler, QObject *parent)
    : QObject(parent), m_runtime(runtime), m_scheduler(scheduler) {
    connect(&m_server, &QTcpServer::newConnection, this, &ApiServer::incoming);
}

bool ApiServer::listen(quint16 port) {
    return m_server.listen(QHostAddress::LocalHost, port);
}

QByteArray ApiServer::response(int code, const QJsonObject &body) const {
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QByteArray reason = code == 200 ? "OK" : code == 201 ? "Created" :
        code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : "Bad Request";
    return "HTTP/1.1 " + QByteArray::number(code) + " " + reason +
           "\r\nContent-Type: application/json\r\nContent-Length: " +
           QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;
}

void ApiServer::incoming() {
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { m_buffers.remove(socket); });
    }
}

void ApiServer::readRequest(QTcpSocket *socket) {
    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }

    int contentLength = 0;
    const QList<QByteArray> headers = buffer.left(headerEnd).split('\n');
    for (const QByteArray &header : headers) {
        const QByteArray trimmed = header.trimmed();
        const QByteArray contentLengthHeader = QByteArrayLiteral("content-length:");
        if (trimmed.toLower().startsWith(contentLengthHeader)) {
            bool ok = false;
            contentLength = trimmed.mid(contentLengthHeader.size()).trimmed().toInt(&ok);
            if (!ok || contentLength < 0 || contentLength > 16 * 1024) {
                socket->write(response(400, {{"error", "invalid content length"}}));
                socket->disconnectFromHost();
                return;
            }
        }
    }
    const int requestSize = headerEnd + 4 + contentLength;
    if (buffer.size() < requestSize) {
        return;
    }

    const QByteArray request = buffer.left(requestSize);
    m_buffers.remove(socket);
    handle(socket, request);
}

QJsonObject ApiServer::parseRunRequest(const QByteArray &body, int *statusCode) const {
    *statusCode = 200;
    if (body.isEmpty()) {
        return {{"command", "true"}};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        *statusCode = 400;
        return {{"error", "body must be a JSON object"}};
    }

    const QJsonObject input = document.object();
    const QString command = input.value("command").toString().trimmed();
    if (command.isEmpty() || command.size() > 4096) {
        *statusCode = 400;
        return {{"error", "command must contain 1 to 4096 characters"}};
    }
    return input;
}

void ApiServer::handle(QTcpSocket *socket, const QByteArray &request) {
    const int headerEnd = request.indexOf("\r\n\r\n");
    const QList<QByteArray> requestLine = request.left(headerEnd).split('\n').value(0).trimmed().split(' ');
    if (requestLine.size() != 3) {
        socket->write(response(400, {{"error", "invalid request line"}}));
        socket->disconnectFromHost();
        return;
    }

    const QString method = QString::fromLatin1(requestLine.at(0));
    const QString path = QString::fromLatin1(requestLine.at(1));
    const QByteArray body = request.mid(headerEnd + 4);
    QJsonObject output;
    int code = 200;

    if (method == "GET" && path == "/status") {
        output = {{"runtime", m_runtime->status()}, {"scheduler", m_scheduler->status()}};
    } else if (method == "GET" && path == "/devices") {
        QJsonArray devices;
        for (const AndroidTarget &target : m_runtime->targets()) {
            devices.append(QJsonObject{{"id", target.id}, {"api", target.api}, {"arch", target.arch},
                                       {"state", target.state}, {"backend", target.backend},
                                       {"stability", target.stability}, {"tags", QJsonArray::fromStringList(target.tags)},
                                       {"health_score", target.healthScore}, {"pid", target.pid}});
        }
        output = {{"devices", devices}};
    } else if (method == "POST" && path.startsWith("/devices/") && path.endsWith("/start")) {
        const QString target = path.section('/', 2, 2);
        output = {{"ok", m_runtime->start(target)}, {"target", target}};
    } else if (method == "POST" && path.startsWith("/devices/") && path.endsWith("/stop")) {
        const QString target = path.section('/', 2, 2);
        output = {{"ok", m_runtime->stop(target)}, {"target", target}};
    } else if (method == "GET" && path == "/scheduler/dry-run") {
        const QString target = m_runtime->resolveTarget();
        output = {{"dry_run", m_scheduler->dryRun(target, "true", 1, 50)}};
    } else if (method == "POST" && path == "/runs") {
        const QJsonObject input = parseRunRequest(body, &code);
        if (code == 200) {
            const QString target = m_runtime->resolveTarget(input.value("target").toString(),
                                                            input.value("abi").toString());
            if (target.isEmpty()) {
                code = 400;
                output = {{"error", "no compatible target is available"}};
            } else {
                const QString command = input.value("command").toString().trimmed();
                const int cost = input.value("cost").toInt(1);
                const int priority = input.value("priority").toInt(50);
                const int retries = input.value("retries").toInt(1);
                output = {{"id", m_scheduler->submit(target, command, cost, priority, retries)},
                          {"target", target}};
                code = 201;
            }
        } else {
            output = input;
        }
    } else if (path == "/status" || path == "/devices" || path == "/scheduler/dry-run") {
        code = 405;
        output = {{"error", "method not allowed"}};
    } else {
        code = 404;
        output = {{"error", "not found"}};
    }

    socket->write(response(code, output));
    socket->disconnectFromHost();
    emit logMessage(method + " " + path);
}

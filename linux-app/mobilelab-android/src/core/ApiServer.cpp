#include "ApiServer.h"
#include "AndroidRuntime.h"
#include "ResourceScheduler.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>

ApiServer::ApiServer(AndroidRuntime *r, ResourceScheduler *s, QObject *p) : QObject(p), m_runtime(r), m_scheduler(s) {
    m_server.setParent(this);
    connect(&m_server, &QTcpServer::newConnection, this, &ApiServer::incoming);
}

quint16 ApiServer::configuredPort() {
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("MOBILELAB_ANDROID_API_PORT", &ok);
    return (ok && v >= 0 && v <= 65535) ? static_cast<quint16>(v) : kDefaultPort;
}

bool ApiServer::listen(quint16 p) { return m_server.listen(QHostAddress::LocalHost, p ? p : configuredPort()); }

QByteArray ApiServer::response(int code, const QJsonObject &body) const {
    const auto b = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QByteArray reason = code == 200 ? "OK" : code == 404 ? "Not Found" : "Bad Request";
    return QByteArray("HTTP/1.1 ") + QByteArray::number(code) + " " + reason +
           "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(b.size()) +
           "\r\nConnection: close\r\n\r\n" + b;
}

void ApiServer::incoming() {
    while (m_server.hasPendingConnections()) {
        auto *s = m_server.nextPendingConnection();
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { handle(s, s->readAll()); });
    }
}

void ApiServer::handle(QTcpSocket *s, const QByteArray &raw) {
    const auto first = raw.split('\n').value(0).trimmed();
    const auto parts = first.split(' ');
    if (parts.size() < 2) {
        s->write(response(400, {{"error", "invalid request"}}));
        s->disconnectFromHost();
        return;
    }
    const QString method = QString::fromLatin1(parts[0]), path = QString::fromLatin1(parts[1]);
    QJsonObject out;
    int code = 200;
    const auto firstTarget = [this] { return m_runtime->targets().isEmpty() ? QString() : m_runtime->targets().first().id; };
    if (method == "GET" && path == "/status") {
        out = {{"runtime", m_runtime->status()}, {"scheduler", m_scheduler->status()}};
    } else if (method == "GET" && path == "/devices") {
        QJsonArray a;
        for (const auto &t : m_runtime->targets())
            a.append(QJsonObject{{"id", t.id}, {"api", t.api}, {"arch", t.arch}, {"state", t.state}, {"backend", t.backend},
                                 {"stability", t.stability}, {"tags", t.tags.join(",")}, {"health_score", t.healthScore},
                                 {"serial", t.serial}, {"pid", double(t.pid)}});
        out = {{"devices", a}};
    } else if (method == "POST" && path.startsWith("/devices/") && path.endsWith("/start")) {
        out = {{"ok", m_runtime->start(path.section('/', 2, 2))}};
    } else if (method == "POST" && path.startsWith("/devices/") && path.endsWith("/stop")) {
        out = {{"ok", m_runtime->stop(path.section('/', 2, 2))}};
    } else if (method == "GET" && path == "/scheduler/dry-run") {
        if (firstTarget().isEmpty()) {
            code = 404;
            out = {{"error", "no Android targets discovered"}};
        } else {
            out = {{"dry_run", m_scheduler->dryRun(firstTarget(), "android-test", 1, 50)}};
        }
    } else if (method == "POST" && path == "/runs") {
        if (firstTarget().isEmpty()) {
            code = 404;
            out = {{"error", "no Android targets discovered"}};
        } else {
            out = {{"id", m_scheduler->submit(firstTarget(), "android-test", 1, 50, 1)}};
        }
    } else {
        code = 404;
        out = {{"error", "not found"}};
    }
    s->write(response(code, out));
    s->disconnectFromHost();
    emit logMessage(method + " " + path);
}

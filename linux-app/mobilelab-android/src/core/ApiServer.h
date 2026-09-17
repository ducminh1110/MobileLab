#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;
class AndroidRuntime;
class ResourceScheduler;

class ApiServer : public QObject {
    Q_OBJECT
public:
    ApiServer(AndroidRuntime *runtime, ResourceScheduler *scheduler, QObject *parent = nullptr);
    bool listen(quint16 port = 4000);
    quint16 port() const { return m_server.serverPort(); }

signals:
    void logMessage(const QString &message);

private slots:
    void incoming();

private:
    QByteArray response(int code, const QJsonObject &body) const;
    void readRequest(QTcpSocket *socket);
    void handle(QTcpSocket *socket, const QByteArray &request);
    QJsonObject parseRunRequest(const QByteArray &body, int *statusCode) const;

    QTcpServer m_server;
    AndroidRuntime *m_runtime;
    ResourceScheduler *m_scheduler;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

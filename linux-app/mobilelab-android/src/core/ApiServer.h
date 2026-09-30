#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>

class AndroidRuntime;
class ResourceScheduler;
class QTcpSocket;

// Local REST server. The port is configurable through MOBILELAB_ANDROID_API_PORT (default 4100);
// 4000 belongs to the Node backend and must not be used here.
class ApiServer : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 kDefaultPort = 4100;
    ApiServer(AndroidRuntime *, ResourceScheduler *, QObject *parent = nullptr);
    static quint16 configuredPort();
    bool listen(quint16 port = 0);  // 0 = configuredPort()
    quint16 port() const { return m_server.serverPort(); }
    QString lastError() const { return m_server.errorString(); }
    bool isListening() const { return m_server.isListening(); }

signals:
    void logMessage(const QString &);

private slots:
    void incoming();

private:
    QByteArray response(int code, const QJsonObject &body) const;
    void handle(QTcpSocket *, const QByteArray &);
    QTcpServer m_server;
    AndroidRuntime *m_runtime;
    ResourceScheduler *m_scheduler;
};

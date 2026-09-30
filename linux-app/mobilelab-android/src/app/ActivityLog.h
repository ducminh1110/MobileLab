#pragma once
// Everything the runtime, scheduler, matrix executor, REST server and IDE launcher say, with timestamps.
#include <QDateTime>
#include <QObject>
#include <QVector>

struct ActivityEntry {
    QDateTime time;
    QString source;  // runtime | scheduler | matrix | api | ide | avd | container | app
    QString text;
    QString line() const { return "[" + source + "] " + text; }
};

class ActivityLog : public QObject {
    Q_OBJECT
public:
    explicit ActivityLog(QObject *parent = nullptr) : QObject(parent) {}
    void add(const QString &source, const QString &text);
    const QVector<ActivityEntry> &entries() const { return m_entries; }
    void clear();
signals:
    void added(const ActivityEntry &e);
    void cleared();
private:
    QVector<ActivityEntry> m_entries;
    static constexpr int kMax = 5000;
};

#include "ActivityLog.h"

void ActivityLog::add(const QString &source, const QString &text) {
    for (const auto &l : text.split('\n', Qt::SkipEmptyParts)) {
        ActivityEntry e{QDateTime::currentDateTime(), source, l.trimmed()};
        if (m_entries.size() >= kMax) m_entries.remove(0, kMax / 10);
        m_entries.push_back(e);
        emit added(e);
    }
}

void ActivityLog::clear() {
    m_entries.clear();
    emit cleared();
}

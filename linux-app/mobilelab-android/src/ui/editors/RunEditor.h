#pragma once
// Report editor for a matrix run or one of its targets: Summary | Tests | Logs.
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include "AppContext.h"
#include "Location.h"
#include "LogView.h"
#include "NavCommon.h"
#include "Widgets.h"

class RunEditor : public QWidget {
    Q_OBJECT
public:
    explicit RunEditor(const AppContext &ctx, QWidget *parent = nullptr);
    void showLocation(const Location &loc);
    void refresh();
    QString runId() const { return m_runId; }
    QString avd() const { return m_avd; }
    QString tab() const;
    LogView *logView() const { return m_log; }
    SegmentedControl *tabs() const { return m_tabs; }
    static bool exportJUnit(const class MatrixRunRecord &r, QString *pathOut);

signals:
    void locationRequested(const Location &loc);
    void action(const QString &name, const QString &id);

private:
    void rebuildSummary();
    void rebuildTests();
    void reloadLog(bool keepPosition);
    void setTab(int i, bool emitLocation);
    QStringList logSources() const;
    QString sourcePath(int index) const;
    AppContext m_ctx;
    QString m_runId, m_avd;
    SegmentedControl *m_tabs;
    QStackedWidget *m_stack;
    QScrollArea *m_summaryScroll;
    NavTree *m_tests;
    LogView *m_log;
    PopupButton *m_source;
    class ThemedLabel *m_logInfo;
    QTimer m_coalesce;
    int m_pendingLine = -1;
    int m_logSource = 0;
};

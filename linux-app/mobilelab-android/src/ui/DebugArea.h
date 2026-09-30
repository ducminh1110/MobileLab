#pragma once
// Debug area under the editor: debug bar (a status strip when collapsed), variables view, green console, footers.
#include <QSplitter>
#include <QTimer>
#include "AppContext.h"
#include "Location.h"
#include "NavCommon.h"
#include "editors/LogView.h"

class QAction;

class LineSplitter : public QSplitter {
    Q_OBJECT
public:
    explicit LineSplitter(Qt::Orientation o, QWidget *parent = nullptr) : QSplitter(o, parent) { setHandleWidth(1); setChildrenCollapsible(false); }
protected:
    QSplitterHandle *createHandle() override;
};

class DebugArea : public QWidget {
    Q_OBJECT
public:
    struct Actions { QAction *toggleDebug, *toggleInspector, *run, *stop, *retryFailed, *screenshot, *clearConsole; };
    DebugArea(const AppContext &ctx, const Actions &a, QWidget *parent = nullptr);
    void setLocation(const Location &loc);
    void setExpanded(bool expanded);
    void setStatus(const QString &state, const QString &detail);
    NavTree *variables() const { return m_vars; }
    LogView *console() const { return m_console; }
    QSplitter *splitter() const { return m_split; }
    int barHeight() const { return Metrics::debugBar; }
    bool consoleVisible() const { return m_console->isVisibleTo(this); }
    void refreshVariables();
    void rebuildConsole();

protected:
    void resizeEvent(QResizeEvent *) override { relayout(); }
    void paintEvent(QPaintEvent *) override;

private:
    void relayout();
    void applyFilter();
    void syncFooter();
    QString breadcrumb() const;
    AppContext m_ctx;
    Actions m_a;
    Location m_loc;
    bool m_expanded = false;
    QString m_state, m_detail;
    // bar
    IconButton *m_toggleBtn, *m_runBtn, *m_stopBtn, *m_retryBtn, *m_shotBtn, *m_inspectorBtn;
    // body
    LineSplitter *m_split;
    NavTree *m_vars;
    LogView *m_console;
    // footers
    QWidget *m_footer, *m_footL, *m_footR;
    PopupButton *m_mode, *m_output;
    FilterBar *m_varFilter, *m_consoleFilter;
    IconButton *m_trash, *m_showVars, *m_showConsole;
    int m_outputIndex = 0;
    QString m_consoleText;
    QTimer m_coalesce;
};

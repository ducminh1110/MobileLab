#pragma once
// Inspector: Attributes | History | Quick Help for the current selection.
#include <QScrollArea>
#include <QStackedWidget>
#include "AppContext.h"
#include "Location.h"
#include "NavCommon.h"

class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(const AppContext &ctx, QWidget *parent = nullptr);
    void setLocation(const Location &loc);
    void refresh();
    int currentTab() const { return m_tabs->current(); }
    void setCurrentTab(int i);
    NavTabBar *tabBar() const { return m_tabs; }
    enum Tab { Attributes, History, QuickHelp };
signals:
    void locationRequested(const Location &loc);
private:
    void rebuildAttributes();
    void rebuildHistory();
    void rebuildHelp();
    AppContext m_ctx;
    Location m_loc;
    NavTabBar *m_tabs;
    QStackedWidget *m_stack;
    QScrollArea *m_attr, *m_hist, *m_help;
    QTimer *m_coalesce;
};

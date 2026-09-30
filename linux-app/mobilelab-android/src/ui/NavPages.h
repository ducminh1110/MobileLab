#pragma once
// The six navigators of section 6 mapped onto the Android core (docs/design/xcode-interface.md section 12).
#include <QLabel>
#include <QTimer>
#include "AppContext.h"
#include "NavCommon.h"

class NavPage : public QWidget {
    Q_OBJECT
public:
    NavPage(const AppContext &ctx, QWidget *parent = nullptr);
    virtual void refresh() = 0;
    // Highlights the row for `loc` without emitting anything.
    virtual void reveal(const Location &loc) { Q_UNUSED(loc); }
    NavTree *tree() const { return m_tree; }
    FilterBar *filter() const { return m_filter; }
    virtual QWidget *focusTarget() { return m_tree; }
    // Number shown in the tab tooltip / accessible description (issues count etc.).
    virtual QString summary() const { return {}; }

signals:
    void locationRequested(const Location &loc);
    void previewRequested(const Location &loc);
    void action(const QString &name, const QString &id);   // context menu / empty state actions

protected:
    void buildBottomBar(QWidget *leading = nullptr);
    AppContext m_ctx;
    NavTree *m_tree;
    FilterBar *m_filter;
    QWidget *m_bottom = nullptr;
};

class DevicesPage : public NavPage {
    Q_OBJECT
public:
    explicit DevicesPage(const AppContext &ctx, QWidget *parent = nullptr);
    void refresh() override;
    void reveal(const Location &loc) override;
    QString summary() const override;
private:
    void showMenu(const QString &id, const QPoint &pos);
    bool m_runningOnly = false;
};

class TestsPage : public NavPage {
    Q_OBJECT
public:
    explicit TestsPage(const AppContext &ctx, QWidget *parent = nullptr);
    void refresh() override;
    void reveal(const Location &loc) override;
private:
    bool m_failedOnly = false, m_recentOnly = false;
};

class IssuesPage : public NavPage {
    Q_OBJECT
public:
    explicit IssuesPage(const AppContext &ctx, QWidget *parent = nullptr);
    void refresh() override;
    QString summary() const override { return m_summary; }
    int count() const { return m_count; }
signals:
    void countChanged(int errors, int warnings);
private:
    bool m_errorsOnly = false;
    int m_count = 0;
    QString m_summary;
};

class FindPage : public NavPage {
    Q_OBJECT
public:
    explicit FindPage(const AppContext &ctx, QWidget *parent = nullptr);
    void refresh() override {}
    QWidget *focusTarget() override { return m_filter->edit(); }
    void setQuery(const QString &q);
private:
    void search();
    PopupButton *m_scope;
    QTimer m_debounce;
    QLabel *m_status;
};

class GaugeColumn;

class DebugPage : public NavPage {
    Q_OBJECT
public:
    explicit DebugPage(const AppContext &ctx, QWidget *parent = nullptr);
    void refresh() override;
protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
private:
    GaugeColumn *m_gauges;
    QTimer m_timer;
    QHash<QString, QVector<qreal>> m_history;
    qint64 m_artifactBytes = -1;
    qint64 m_artifactStamp = 0;
};

class ReportsPage : public NavPage {
    Q_OBJECT
public:
    explicit ReportsPage(const AppContext &ctx, QWidget *parent = nullptr);
    void refresh() override;
    void reveal(const Location &loc) override;
private:
    bool m_failedOnly = false;
};

// The navigator column: tab bar + one page per tab.
class Navigator : public QWidget {
    Q_OBJECT
public:
    explicit Navigator(const AppContext &ctx, QWidget *parent = nullptr);
    enum Tab { Devices, Tests, Issues, Find, Debug, Reports, TabCount };
    int current() const;
    void setCurrent(int tab, bool focusTree = false);
    NavPage *page(int tab) const { return m_pages[tab]; }
    DevicesPage *devices() const { return static_cast<DevicesPage *>(m_pages[Devices]); }
    FindPage *find() const { return static_cast<FindPage *>(m_pages[Find]); }
    IssuesPage *issues() const { return static_cast<IssuesPage *>(m_pages[Issues]); }
    NavTabBar *tabBar() const { return m_tabs; }
    void refreshAll();
    void reveal(const Location &loc);
    static QString tabName(int tab);
signals:
    void locationRequested(const Location &loc);
    void previewRequested(const Location &loc);
    void action(const QString &name, const QString &id);
    void currentChanged(int tab);
private:
    NavTabBar *m_tabs;
    class QStackedWidget *m_stack;
    NavPage *m_pages[TabCount];
};

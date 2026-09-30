#pragma once
// Jump bar + the editor pages (Welcome, device canvas, run report, container).
#include <QStackedWidget>
#include "AppContext.h"
#include "JumpBar.h"
#include "Location.h"
#include "editors/RunEditor.h"
#include "editors/TargetEditor.h"
#include "editors/WelcomeEditor.h"

class ContainerEditor;

class EditorArea : public QWidget {
    Q_OBJECT
public:
    struct Actions { QAction *back, *forward, *related, *options, *add; };
    EditorArea(const AppContext &ctx, const Actions &a, QWidget *parent = nullptr);
    void showLocation(const Location &loc);
    const Location &location() const { return m_loc; }
    JumpBar *jumpBar() const { return m_jump; }
    WelcomeEditor *welcome() const { return m_welcome; }
    TargetEditor *target() const { return m_target; }
    RunEditor *run() const { return m_run; }
    QWidget *currentEditor() const { return m_stack->currentWidget(); }
    void refresh();
    QVector<JumpBar::Crumb> crumbsFor(const Location &loc) const;

signals:
    void locationRequested(const Location &loc);
    void action(const QString &name, const QString &id);
    void navigatorRequested(int tab);

private:
    AppContext m_ctx;
    Location m_loc;
    JumpBar *m_jump;
    QStackedWidget *m_stack;
    WelcomeEditor *m_welcome;
    TargetEditor *m_target;
    RunEditor *m_run;
    ContainerEditor *m_container;
};

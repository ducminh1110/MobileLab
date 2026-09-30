#pragma once
#include "AppContext.h"
#include "SheetDialog.h"

class QStackedWidget;
class SegmentedControl;
class QWidget;

class SettingsSheet : public SheetDialog {
    Q_OBJECT
public:
    enum Pane { General, Environment, Storage };
    SettingsSheet(const AppContext &ctx, QWidget *parent);
    void showPane(Pane p);
    Pane pane() const;
signals:
    void rerunChecks();
    void artifactsCleaned(int removed);
private:
    QWidget *buildGeneral();
    QWidget *buildEnvironment();
    QWidget *buildStorage();
    void rebuildEnvironment();
    AppContext m_ctx;
    SegmentedControl *m_tabs;
    QStackedWidget *m_stack;
    QWidget *m_envHost = nullptr;
    QWidget *m_storageHost = nullptr;
    int m_days = 30;
};

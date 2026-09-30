#pragma once
// Application window: toolbar, floating navigator / editor / inspector panels, debug area, menus and state.
#include <QMainWindow>
#include <QTimer>
#include "AppContext.h"
#include "DebugArea.h"
#include "EditorArea.h"
#include "Inspector.h"
#include "Location.h"
#include "NavPages.h"
#include "PaneHost.h"
#include "SettingsSheet.h"
#include "Toolbar.h"

class AndroidRuntime;
class AndroidEmulator;
class ResourceScheduler;
class MatrixExecutor;
class ArtifactCollector;
class ApiServer;
class AndroidContainerRuntime;
class ActivityLog;
class HostMetrics;
class QSettings;
class Toast;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    AppContext &context() { return m_ctx; }
    Toolbar *toolbar() const { return m_toolbar; }
    Navigator *navigator() const { return m_navigator; }
    EditorArea *editor() const { return m_editor; }
    Inspector *inspector() const { return m_inspector; }
    DebugArea *debugArea() const { return m_debug; }
    PaneHost *panes() const { return m_panes; }
    PaneHost *editorPanes() const { return m_editorPanes; }
    int navigatorPane() const { return m_navId; }
    int inspectorPane() const { return m_insId; }
    int debugPane() const { return m_dbgId; }
    Location location() const { return m_loc; }

    void navigate(const Location &loc, bool addHistory = true);
    void goBack();
    void goForward();
    void showNavigator(bool on, bool animate = true);
    void showInspector(bool on, bool animate = true);
    void showDebugArea(bool on, bool animate = true);
    void selectNavigator(int tab, bool focus = true);
    void runMatrix(const QStringList &only = {});
    void stopRun();
    void newAvd();
    void openSettings(SettingsSheet::Pane pane = SettingsSheet::General);
    void openQuickly();
    void showShortcuts();
    void launchVsCode();
    void refreshTargets();
    void resetLayout();
    void saveUiState();
    void toast(const QString &text, bool error = false);
    QString destinationText() const;
    QStringList destinationSelection() const { return m_dest; }
    void setDestination(const QStringList &names);
    int schemeCount() const { return m_schemes.size(); }
    bool loadedSettings() const { return m_restored; }
    // Used by screenshot mode to keep dialogs non-modal.
    void setModalDialogsEnabled(bool on) { m_modalDialogs = on; }
    QWidget *lastSheet() const { return m_lastSheet; }
    void updateCapsule();

protected:
    void paintEvent(QPaintEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct Scheme { QString file, title; QStringList abis; };
    void createActions();
    void createMenus();
    void createUi();
    void connectCore();
    void restoreUiState();
    void handleAction(const QString &name, const QString &id);
    void updateActions();
    void updateToggleText();
    void showSchemeMenu(const QPoint &below);
    void showDestinationMenu(const QPoint &below);
    void loadSchemes();
    QVector<struct MatrixTarget> targetsFor(const QStringList &only) const;
    void nextIssue(int direction);
    void updateHistoryActions();
    void openReportFromStatus();
    QString selectedTargetId() const;
    void scheduleSave();
    void sendKeyToFocus(const QKeySequence &seq);

    AppContext m_ctx;
    Toolbar *m_toolbar = nullptr;
    PaneHost *m_panes = nullptr, *m_editorPanes = nullptr;
    Navigator *m_navigator = nullptr;
    EditorArea *m_editor = nullptr;
    Inspector *m_inspector = nullptr;
    DebugArea *m_debug = nullptr;
    Panel *m_navPanel = nullptr, *m_editorPanel = nullptr, *m_inspectorPanel = nullptr;
    int m_navId = -1, m_insId = -1, m_dbgId = -1;
    Toast *m_toast = nullptr;
    QWidget *m_lastSheet = nullptr;
    QVector<Location> m_history;
    int m_hpos = -1;
    Location m_loc;
    QStringList m_dest;      // empty = all targets
    QVector<Scheme> m_schemes;
    int m_scheme = 0;
    QTimer m_tick, m_save;
    bool m_restored = false, m_modalDialogs = true, m_navigating = false;
    int m_issueCursor = -1;
    // actions
    QAction *aNavigator, *aInspector, *aDebug, *aRun, *aStop, *aRetry, *aBack, *aForward, *aRelated, *aOptions, *aAdd;
    QAction *aClear, *aScreenshot, *aNewAvd, *aQuickly, *aSettings, *aRefresh, *aStartDev, *aStopDev, *aRestartDev, *aProbe, *aVsCode;
    QAction *aFind, *aQuit, *aShortcuts, *aDiagnostics, *aReset, *aNextIssue, *aPrevIssue;
    QAction *aNavTab[6], *aInsTab[3];
};

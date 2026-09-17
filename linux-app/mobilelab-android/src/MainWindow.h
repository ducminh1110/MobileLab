#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QProgressBar>
#include <QLineEdit>
#include <QTimer>
class AndroidRuntime; class AndroidEmulator; class ResourceScheduler; class ApiServer; class MatrixExecutor; class ArtifactCollector;
class MainWindow final:public QMainWindow{
 Q_OBJECT
public:explicit MainWindow(QWidget *parent=nullptr);
private:
 QWidget*makeSidebar(); QWidget*makeTopBar(); QWidget*makeDashboard(); QWidget*makeDevices(); QWidget*makeMatrix(); QWidget*makeConsole(); QWidget*makeInspector();
 void selectPage(int); void refreshUi(); void showAvdWizard(); void runMatrix();
 QStackedWidget*pages_=nullptr; QListWidget*deviceList_=nullptr; QTableWidget*matrix_=nullptr; QTextEdit*console_=nullptr; QLabel*connection_=nullptr; QLabel*active_=nullptr; QLabel*health_=nullptr; QLineEdit*deviceSearch_=nullptr; QProgressBar*cpu_=nullptr; QProgressBar*memory_=nullptr; QTimer*pollTimer_=nullptr;
 AndroidRuntime*runtime_=nullptr; AndroidEmulator*emulator_=nullptr; ResourceScheduler*scheduler_=nullptr; ApiServer*api_=nullptr; ArtifactCollector*artifacts_=nullptr; MatrixExecutor*matrixExecutor_=nullptr;
};

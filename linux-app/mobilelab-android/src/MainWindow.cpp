#include "MainWindow.h"
#include "core/AndroidRuntime.h"
#include "core/AndroidEmulator.h"
#include "core/AndroidPackageCatalog.h"
#include "core/ArtifactCollector.h"
#include "core/MatrixExecutor.h"
#include "core/ResourceScheduler.h"
#include "core/ApiServer.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QComboBox>
#include <QFrame>
#include <QSplitter>
#include <QFont>
#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QMessageBox>
#include <QDateTime>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <algorithm>

namespace{
const char*mobileLabStyle=R"(
QMainWindow,QWidget{background:#111318;color:#e7e9ee;font-family:"Noto Sans",sans-serif;font-size:13px;}
QFrame#sidebar,QFrame#inspector,QFrame#toolbar{background:#17191f;}
QLabel#brand{font-size:18px;font-weight:700;color:#fff;} QLabel#title{font-size:20px;font-weight:700;color:#fff;} QLabel#muted{color:#858b98;font-size:11px;} QLabel#cardTitle{font-size:10px;font-weight:700;color:#9da4b1;} QLabel#metric{font-size:22px;font-weight:700;color:#fff;}
QPushButton#nav{border:0;border-radius:7px;text-align:left;padding:9px 12px;color:#aeb4c0;background:transparent;} QPushButton#nav:hover,QPushButton#nav[selected="true"]{background:#292e38;color:#fff;}
QPushButton#primary{background:#3f7cff;color:white;border:0;border-radius:7px;padding:7px 14px;font-weight:600;} QPushButton#secondary{background:#242832;color:#d9dce3;border:1px solid #343945;border-radius:7px;padding:7px 12px;}
QFrame#card{background:#191c23;border:1px solid #292d36;border-radius:10px;} QListWidget,QTableWidget,QTextEdit{background:#15171d;border:1px solid #292d36;border-radius:8px;color:#dfe2e8;} QListWidget::item{padding:9px;border-radius:6px;} QListWidget::item:selected{background:#292f3a;color:white;}
QHeaderView::section{background:#1c1f26;color:#8f96a3;border:0;padding:8px;font-size:11px;} QTableWidget::item{padding:7px;} QProgressBar{background:#242832;border:0;border-radius:4px;height:7px;} QProgressBar::chunk{background:#5b8cff;border-radius:4px;} QComboBox,QLineEdit,QSpinBox{background:#20232b;border:1px solid #30343e;border-radius:7px;padding:6px;color:#ddd;}
)";
QLabel*lab(const QString&t,const QString&n={}){auto*l=new QLabel(t);if(!n.isEmpty())l->setObjectName(n);return l;}
QFrame*card(const QString&t,const QString&v,const QString&s){auto*f=new QFrame;f->setObjectName("card");auto*x=new QVBoxLayout(f);x->setContentsMargins(14,12,14,12);x->addWidget(lab(t.toUpper(),"cardTitle"));x->addWidget(lab(v,"metric"));x->addWidget(lab(s,"muted"));return f;}
}

MainWindow::MainWindow(QWidget*p):QMainWindow(p){
 setWindowTitle("MobileLab Android · Hybrid");resize(1460,900);setMinimumSize(1100,700);qApp->setStyleSheet(mobileLabStyle);
 emulator_=new AndroidEmulator(); artifacts_=new ArtifactCollector(); runtime_=new AndroidRuntime(this); runtime_->setEmulator(emulator_); scheduler_=new ResourceScheduler(this); api_=new ApiServer(runtime_,scheduler_,this); matrixExecutor_=new MatrixExecutor(emulator_,scheduler_,artifacts_,this);
 runtime_->probe(); scheduler_->configure(8,16384); api_->listen(4000);
 connect(runtime_,&AndroidRuntime::logMessage,this,[this](const QString&m){if(console_)console_->append("[runtime] "+m);});
 connect(scheduler_,&ResourceScheduler::logMessage,this,[this](const QString&m){if(console_)console_->append("[scheduler] "+m);});
 connect(matrixExecutor_,&MatrixExecutor::logMessage,this,[this](const QString&m){if(console_)console_->append("[matrix] "+m);});
 auto*root=new QWidget;auto*hl=new QHBoxLayout(root);hl->setContentsMargins(0,0,0,0);hl->setSpacing(0);hl->addWidget(makeSidebar());
 auto*center=new QWidget;auto*cv=new QVBoxLayout(center);cv->setContentsMargins(0,0,0,0);cv->setSpacing(0);cv->addWidget(makeTopBar());pages_=new QStackedWidget;pages_->addWidget(makeDashboard());pages_->addWidget(makeDevices());pages_->addWidget(makeMatrix());pages_->addWidget(makeConsole());cv->addWidget(pages_,1);hl->addWidget(center,1);hl->addWidget(makeInspector());setCentralWidget(root);
 pollTimer_=new QTimer(this);connect(pollTimer_,&QTimer::timeout,this,[this]{runtime_->refreshTargets();refreshUi();});pollTimer_->start(2000);
}

void MainWindow::refreshUi(){
 if(!deviceList_)return;deviceList_->clear();const auto query=deviceSearch_?deviceSearch_->text().trimmed():QString();int healthSum=0,healthCount=0;for(const auto&t:runtime_->targets()){const QString row=QString("%1 · Android %2 · %3 · %4 · health %5 · %6").arg(t.id,t.api,t.arch,t.state).arg(t.healthScore).arg(t.tags.join(","));if(!query.isEmpty()&&!row.contains(query,Qt::CaseInsensitive))continue;deviceList_->addItem(row);healthSum+=t.healthScore;++healthCount;}
 if(active_)active_->setText(QString::number(std::count_if(runtime_->targets().begin(),runtime_->targets().end(),[](const AndroidTarget&t){return t.state=="running";})));
 if(health_)health_->setText(healthCount?QString::number(healthSum/healthCount):"0");
}
void MainWindow::selectPage(int i){if(pages_)pages_->setCurrentIndex(qBound(0,i,pages_->count()-1));}

QWidget*MainWindow::makeSidebar(){auto*f=new QFrame;f->setObjectName("sidebar");f->setFixedWidth(240);auto*v=new QVBoxLayout(f);v->setContentsMargins(14,18,14,14);v->addWidget(lab("MobileLab","brand"));v->addWidget(lab("ANDROID · HYBRID x86_64 + ARM64","muted"));v->addSpacing(18);QStringList ns={"⌘  Overview","▦  Devices","▤  Test Matrix","◌  Live Console"};for(int i=0;i<ns.size();++i){auto*b=new QPushButton(ns[i]);b->setObjectName("nav");b->setProperty("selected",i==0);connect(b,&QPushButton::clicked,this,[this,i]{selectPage(i);});v->addWidget(b);}v->addStretch();auto*c=new QFrame;c->setObjectName("card");auto*x=new QVBoxLayout(c);x->addWidget(lab("HYBRID RUNTIME","cardTitle"));x->addWidget(lab(runtime_->hybridAbiAvailable()?"● x86_64 + ARM64 images":"● Partial ABI inventory","muted"));x->addWidget(lab(runtime_->kvmAvailable()?"KVM available":"KVM unavailable · reduced compatibility","muted"));x->addWidget(lab("REST 127.0.0.1:4000","muted"));v->addWidget(c);return f;}

QWidget*MainWindow::makeTopBar(){auto*f=new QFrame;f->setObjectName("toolbar");f->setFixedHeight(58);auto*h=new QHBoxLayout(f);h->setContentsMargins(16,8,16,8);h->addWidget(lab("Android Device Lab","title"));h->addStretch();connection_=lab(runtime_->kvmAvailable()?"● Connected · hybrid KVM":"● Connected · hybrid degraded","muted");h->addWidget(connection_);auto*code=new QPushButton("</> VS Code");code->setObjectName("secondary");connect(code,&QPushButton::clicked,this,[this]{const auto bin=QStandardPaths::findExecutable("code");if(bin.isEmpty()){if(console_)console_->append("[ide] VS Code CLI not found. Install code or add it to PATH.");QMessageBox::information(this,"VS Code unavailable","The VS Code CLI (code) was not found on PATH.");return;}QProcess::startDetached(bin,{QDir::currentPath()});if(console_)console_->append("[ide] launched VS Code for parallel app development");});h->addWidget(code);auto*newAvd=new QPushButton("＋ New AVD");newAvd->setObjectName("secondary");connect(newAvd,&QPushButton::clicked,this,&MainWindow::showAvdWizard);h->addWidget(newAvd);auto*b=new QPushButton("▶  Run Matrix");b->setObjectName("primary");connect(b,&QPushButton::clicked,this,&MainWindow::runMatrix);h->addWidget(b);return f;}

QWidget*MainWindow::makeDashboard(){auto*p=new QWidget;auto*v=new QVBoxLayout(p);v->setContentsMargins(22,22,22,18);v->addWidget(lab("Overview","title"));v->addWidget(lab("Hybrid Android orchestration for x86_64-first and ARM64-fundamental execution on Linux","muted"));auto*c=new QHBoxLayout;c->addWidget(card("Targets",QString::number(runtime_->targets().size()),"discovered from Android SDK / AVD manager"));auto*a=card("Running","0","active emulator workloads");active_=a->findChild<QLabel*>("metric");c->addWidget(a);auto*hcard=card("Health","0","average score for visible devices");health_=hcard->findChild<QLabel*>("metric");c->addWidget(hcard);c->addWidget(card("API","4000","local REST endpoint"));v->addLayout(c);auto*s=new QSplitter(Qt::Horizontal);auto*l=new QFrame;l->setObjectName("card");auto*lv=new QVBoxLayout(l);lv->addWidget(lab("DEVICE POOL","cardTitle"));deviceSearch_=new QLineEdit;deviceSearch_->setPlaceholderText("Filter by ABI, API, backend, state, tag...");connect(deviceSearch_,&QLineEdit::textChanged,this,[this]{refreshUi();});lv->addWidget(deviceSearch_);deviceList_=new QListWidget;lv->addWidget(deviceList_);s->addWidget(l);auto*r=new QFrame;r->setObjectName("card");auto*rv=new QVBoxLayout(r);rv->addWidget(lab("HOST CAPABILITIES","cardTitle"));rv->addWidget(lab("Architecture: "+runtime_->architecture(),"muted"));rv->addWidget(lab("Kernel: "+runtime_->kernel(),"muted"));rv->addWidget(lab(runtime_->kvmAvailable()?"KVM acceleration available":"KVM unavailable · no nested virtualization required by userspace ARM path","muted"));rv->addWidget(lab(emulator_->info().available?"Google Android Emulator: detected":"Google Android Emulator: not detected","muted"));rv->addWidget(lab(QString("Installed system images: %1").arg(emulator_->installedSystemImages().size()),"muted"));rv->addStretch();s->addWidget(r);v->addWidget(s,1);return p;}

QWidget*MainWindow::makeDevices(){auto*p=new QWidget;auto*v=new QVBoxLayout(p);v->setContentsMargins(22,22,22,18);auto*h=new QHBoxLayout;h->addWidget(lab("Devices","title"));h->addStretch();auto*b=new QPushButton("＋ Create Hybrid AVD");b->setObjectName("primary");connect(b,&QPushButton::clicked,this,&MainWindow::showAvdWizard);h->addWidget(b);v->addLayout(h);v->addWidget(lab("Real AVD inventory from Android Emulator / avdmanager, annotated by ABI stability, tags and health score","muted"));auto*l=new QListWidget;for(const auto&t:runtime_->targets())l->addItem(QString("%1     API %2     %3     %4     %5     health=%6     tags=%7").arg(t.id,t.api,t.arch,t.state,t.stability).arg(t.healthScore).arg(t.tags.join(",")));v->addWidget(l,1);return p;}

QWidget*MainWindow::makeMatrix(){auto*p=new QWidget;auto*v=new QVBoxLayout(p);v->setContentsMargins(22,22,22,18);auto*h=new QHBoxLayout;h->addWidget(lab("Test Matrix","title"));h->addStretch();auto*b=new QPushButton("▶ Execute Matrix");b->setObjectName("primary");connect(b,&QPushButton::clicked,this,&MainWindow::runMatrix);h->addWidget(b);v->addLayout(h);v->addWidget(lab("Selected x86_64 and ARM64 targets are scheduled together with priority, retry metadata and dry-run API support.","muted"));matrix_=new QTableWidget;matrix_->setColumnCount(5);matrix_->setHorizontalHeaderLabels({"Target","Android","ABI","Execution","Status"});matrix_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);for(const auto&t:runtime_->targets()){const int r=matrix_->rowCount();matrix_->insertRow(r);matrix_->setItem(r,0,new QTableWidgetItem(t.id));matrix_->setItem(r,1,new QTableWidgetItem(t.api));matrix_->setItem(r,2,new QTableWidgetItem(t.arch));matrix_->setItem(r,3,new QTableWidgetItem(runtime_->kvmAvailable()?"accelerated hybrid":"degraded userspace"));matrix_->setItem(r,4,new QTableWidgetItem("READY"));}v->addWidget(matrix_,1);return p;}

QWidget*MainWindow::makeConsole(){auto*p=new QWidget;auto*v=new QVBoxLayout(p);v->setContentsMargins(22,22,22,18);v->addWidget(lab("Live Console","title"));v->addWidget(lab("Runtime, scheduler, matrix and provisioning events","muted"));console_=new QTextEdit;console_->setReadOnly(true);console_->setFont(QFont("monospace",10));console_->setPlainText(QString("[MobileLab] Android hybrid framework initialized.\n[probe] arch=%1\n[probe] KVM=%2\n[probe] emulator=%3\n[probe] installed system images=%4\n[api] REST listening on 127.0.0.1:%5").arg(runtime_->architecture(),runtime_->kvmAvailable()?"available":"unavailable",emulator_->info().available?"available":"unavailable").arg(emulator_->installedSystemImages().size()).arg(api_->port()));v->addWidget(console_,1);return p;}

QWidget*MainWindow::makeInspector(){auto*f=new QFrame;f->setObjectName("inspector");f->setFixedWidth(280);auto*v=new QVBoxLayout(f);v->setContentsMargins(14,18,14,14);v->addWidget(lab("Inspector","title"));v->addWidget(lab("RUNTIME","cardTitle"));auto*c=new QFrame;c->setObjectName("card");auto*x=new QVBoxLayout(c);x->addWidget(lab("Android Hybrid Runtime","brand"));x->addWidget(lab("Linux / x86_64 + ARM64","muted"));x->addWidget(lab(runtime_->qemuAvailable()?"QEMU: available":"QEMU: not installed","muted"));x->addWidget(lab(emulator_->info().available?"Google Emulator: available":"Google Emulator: not installed","muted"));v->addWidget(c);v->addWidget(lab("ACTIONS","cardTitle"));auto*run=new QPushButton("Start Selected Target");run->setObjectName("secondary");connect(run,&QPushButton::clicked,this,[this]{if(!runtime_->targets().isEmpty())runtime_->start(runtime_->targets().first().id);});v->addWidget(run);auto*stop=new QPushButton("Stop Selected Target");stop->setObjectName("secondary");connect(stop,&QPushButton::clicked,this,[this]{if(!runtime_->targets().isEmpty())runtime_->stop(runtime_->targets().first().id);});v->addWidget(stop);auto*shell=new QPushButton("Run ABI Shell Probe");shell->setObjectName("secondary");connect(shell,&QPushButton::clicked,this,[this]{if(!runtime_->targets().isEmpty())runtime_->shell(runtime_->targets().first().id,"getprop ro.product.cpu.abi");});v->addWidget(shell);v->addStretch();return f;}

void MainWindow::showAvdWizard(){
 QDialog d(this);d.setWindowTitle("Create Hybrid Android AVD");d.resize(560,300);auto*v=new QVBoxLayout(&d);v->addWidget(lab("New Hybrid Android Device","title"));v->addWidget(lab("Provision through Google's Android SDK / avdmanager. No system images are bundled in MobileLab.","muted"));auto*grid=new QGridLayout;auto*name=new QLineEdit(runtime_->x86_64Host()?"mobilelab-x86_64-api35":"mobilelab-arm64-api35");auto*pkg=new QComboBox;auto catalog=AndroidPackageCatalog(emulator_->sdkRoot());for(const auto&p:catalog.systemImages())pkg->addItem(p.displayName,p.id);if(pkg->count()==0)pkg->addItem("No installed system images detected (x86_64 preferred, ARM64 fundamental)",runtime_->x86_64Host()?"system-images;android-35;google_apis;x86_64":"system-images;android-35;default;arm64-v8a");auto*device=new QLineEdit("pixel_8");grid->addWidget(lab("AVD name","muted"),0,0);grid->addWidget(name,0,1);grid->addWidget(lab("System image","muted"),1,0);grid->addWidget(pkg,1,1);grid->addWidget(lab("Device profile","muted"),2,0);grid->addWidget(device,2,1);v->addLayout(grid);auto*info=lab(QString("SDK: %1\nEmulator: %2\nKVM: %3").arg(emulator_->sdkRoot(),emulator_->info().available?"detected":"not detected",runtime_->kvmAvailable()?"available":"unavailable"),"muted");v->addWidget(info);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);buttons->button(QDialogButtonBox::Ok)->setText("Create AVD");connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);v->addWidget(buttons);if(d.exec()==QDialog::Accepted){if(emulator_->createAvd(name->text(),pkg->currentData().toString(),device->text())){runtime_->refreshTargets();refreshUi();if(console_)console_->append("[avd] created "+name->text());}else QMessageBox::warning(this,"AVD creation failed","avdmanager could not create the requested AVD. Check that the selected x86_64 or ARM64 system image is installed.");}}

void MainWindow::runMatrix(){
 QVector<MatrixTarget> targets;for(const auto&t:runtime_->targets())targets.push_back({t.id,t.api,t.arch,"android-test"});
 if(targets.isEmpty()){QMessageBox::information(this,"No AVDs","Create or install an x86_64 or ARM64 AVD first, then run the matrix.");return;}
 selectPage(2);if(console_)console_->append(QString("[matrix] executing %1 hybrid Android target(s)").arg(targets.size()));matrixExecutor_->run(targets);if(console_)console_->append("[matrix] artifacts: "+matrixExecutor_->lastRun().value("artifactDir").toString());
}

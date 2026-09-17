#include "AndroidEmulator.h"
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QFile>

AndroidEmulator::AndroidEmulator(){
    m_sdkRoot=qEnvironmentVariable("ANDROID_HOME");
    if(m_sdkRoot.isEmpty()) m_sdkRoot=qEnvironmentVariable("ANDROID_SDK_ROOT");
    if(m_sdkRoot.isEmpty()) m_sdkRoot=QDir::homePath()+"/Android/Sdk";
    m_emulator=m_sdkRoot+"/emulator/emulator";
    m_adb=m_sdkRoot+"/platform-tools/adb";
    m_avdManager=m_sdkRoot+"/cmdline-tools/latest/bin/avdmanager";
    if(!QFileInfo::exists(m_avdManager)) m_avdManager=m_sdkRoot+"/cmdline-tools/bin/avdmanager";
}

bool AndroidEmulator::run(const QString &program,const QStringList &args,QString *output,int timeout) const{
    QProcess p; p.start(program,args);
    if(!p.waitForStarted(3000)||!p.waitForFinished(timeout)) return false;
    if(output) *output=QString::fromLocal8Bit(p.readAllStandardOutput()+p.readAllStandardError());
    return p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0;
}

bool AndroidEmulator::discover(){
    m_avds.clear(); QString out;
    if(!run(m_emulator,{"-list-avds"},&out)) return false;
    for(const auto &line:out.split('\n',Qt::SkipEmptyParts)){
        AndroidAvd a; a.name=line.trimmed(); a.abi="arm64-v8a"; a.api="unknown";
        const QString ini=QDir::homePath()+"/.android/avd/"+a.name+".ini";
        if(QFileInfo::exists(ini)){
            QFile f(ini); if(f.open(QIODevice::ReadOnly|QIODevice::Text)){
                const QString text=QString::fromUtf8(f.readAll());
                const auto m=QRegularExpression("path=(.+)").match(text); if(m.hasMatch()) a.path=m.captured(1).trimmed();
                const auto abi=QRegularExpression("(?:abi.type|hw.cpu.arch)=(.+)").match(text); if(abi.hasMatch()) a.abi=abi.captured(1).trimmed();
                const auto api=QRegularExpression("(?:image.sysdir.1|target)=(?:.*android-|android-)?([0-9]+)").match(text); if(api.hasMatch()) a.api=api.captured(1).trimmed();
            }
        }
        if(!a.path.isEmpty()){
            QFile config(QDir(a.path).filePath("config.ini"));
            if(config.open(QIODevice::ReadOnly|QIODevice::Text)){
                const QString text=QString::fromUtf8(config.readAll());
                const auto abi=QRegularExpression("(?:abi.type|hw.cpu.arch)=(.+)").match(text); if(abi.hasMatch()) a.abi=abi.captured(1).trimmed();
                const auto api=QRegularExpression("(?:image.sysdir.1|target)=(?:.*android-|android-)?([0-9]+)").match(text); if(api.hasMatch()) a.api=api.captured(1).trimmed();
            }
        }
        m_avds.push_back(a);
    }
    return true;
}

bool AndroidEmulator::createAvd(const QString &name,const QString &packageName,const QString &device){
    if(!QFileInfo::exists(m_avdManager)) return false;
    return run(m_avdManager,{"create","avd","-n",name,"-k",packageName,"-d",device,"--force"},nullptr,30000);
}

bool AndroidEmulator::start(const QString &name,bool noWindow){
    if(!QFileInfo::exists(m_emulator)) return false;
    QStringList a={"-avd",name,"-no-boot-anim","-no-snapshot"};
    if(noWindow) a<<"-no-window";
    QProcess *p=new QProcess(); p->setProgram(m_emulator); p->setArguments(a); p->setProcessChannelMode(QProcess::MergedChannels); p->setParent(nullptr); p->start();
    return p->waitForStarted(3000);
}

bool AndroidEmulator::stop(const QString &name){ QString out; return adb({"-s",name,"emu","kill"},&out); }

bool AndroidEmulator::adb(const QStringList &args,QString *output){ return run(m_adb,args,output,15000); }

QStringList AndroidEmulator::installedSystemImages() const{
    QStringList out;
    QDir root(m_sdkRoot+"/system-images");
    if(!root.exists()) return out;
    for(const auto &api:root.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
        QDir a(root.filePath(api));
        for(const auto &tag:a.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
            QDir b(a.filePath(tag));
            for(const auto &abi:b.entryList(QDir::Dirs|QDir::NoDotAndDotDot)) out<<"system-images;android-"+api+";"+tag+";"+abi;
        }
    }
    return out;
}

AndroidEmulatorInfo AndroidEmulator::info() const{
    return {m_sdkRoot,m_emulator,m_adb,m_avdManager,QFileInfo::exists(m_emulator)&&QFileInfo::exists(m_adb)};
}

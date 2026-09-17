#include "AndroidRuntime.h"
#include "AndroidEmulator.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

AndroidRuntime::AndroidRuntime(QObject *parent):QObject(parent) {}

void AndroidRuntime::setEmulator(AndroidEmulator *emulator){ m_androidEmulator=emulator; }

QString AndroidRuntime::findExecutable(const QStringList &names) const{
    for(const auto &n:names){const auto p=QStandardPaths::findExecutable(n);if(!p.isEmpty())return p;}
    return {};
}

QString AndroidRuntime::classifyTargetStability(const QString &abi) const{
    if(abi.contains("x86_64",Qt::CaseInsensitive)) return "preferred";
    if(abi.contains("arm64",Qt::CaseInsensitive)||abi.contains("aarch64",Qt::CaseInsensitive)) return "fundamental";
    return "limited";
}

QStringList AndroidRuntime::deriveTags(const AndroidTarget &target) const{
    QStringList tags;
    tags << target.backend << target.stability;
    if(target.arch.contains("x86_64",Qt::CaseInsensitive)) tags << "abi:x86_64" << "focus";
    if(target.arch.contains("arm64",Qt::CaseInsensitive)||target.arch.contains("aarch64",Qt::CaseInsensitive)) tags << "abi:arm64" << "compat";
    if(target.state=="running") tags << "running";
    if(target.api!="unknown") tags << "api:" + target.api;
    tags.removeDuplicates();
    return tags;
}

int AndroidRuntime::calculateHealthScore(const AndroidTarget &target) const{
    int score=70;
    if(target.state=="running") score+=10;
    if(target.stability=="preferred") score+=15;
    if(target.stability=="fundamental") score-=10;
    if(target.backend=="host-dev") score-=5;
    if(!m_kvm && target.backend.contains("emulator")) score-=20;
    if(target.api=="unknown") score-=5;
    return qBound(0,score,100);
}

bool AndroidRuntime::probe(){
    m_arch=QSysInfo::currentCpuArchitecture(); m_kernel=QSysInfo::kernelVersion(); m_kvm=QFile::exists("/dev/kvm");
    m_arm64Host=m_arch.contains("arm",Qt::CaseInsensitive)||m_arch.contains("aarch64",Qt::CaseInsensitive);
    m_x86_64Host=m_arch.contains("x86_64",Qt::CaseInsensitive)||m_arch.contains("amd64",Qt::CaseInsensitive);
    m_qemu=!findExecutable({"qemu-system-x86_64","qemu-system-aarch64","qemu-system-arm"}).isEmpty();
    m_emulator=m_androidEmulator?m_androidEmulator->info().available:!findExecutable({"emulator"}).isEmpty();
    m_supportedAbis.clear(); m_hasArm64Abi=false; m_hasX8664Abi=false;
    if(m_androidEmulator){
        for(const auto &image:m_androidEmulator->installedSystemImages()){
            const auto abi=image.section(';',-1);
            if(!m_supportedAbis.contains(abi)) m_supportedAbis << abi;
            if(abi.contains("arm64",Qt::CaseInsensitive)) m_hasArm64Abi=true;
            if(abi.contains("x86_64",Qt::CaseInsensitive)) m_hasX8664Abi=true;
        }
    }
    emit logMessage(QString("Hybrid Android runtime probe: host=%1 kernel=%2 kvm=%3 qemu=%4 emulator=%5 abis=%6")
        .arg(m_arch,m_kernel,m_kvm?"yes":"no",m_qemu?"yes":"no",m_emulator?"yes":"no",m_supportedAbis.join(",")));
    if(m_hasArm64Abi) emit logMessage("ARM64 ABI is enabled but remains fundamental: package availability and runtime compatibility are stricter than x86_64.");
    if(m_hasX8664Abi) emit logMessage("x86_64 ABI is enabled as the preferred near-term Android emulator path while compatibility gaps are burned down.");
    if(!m_kvm)emit logMessage("KVM unavailable; accelerated emulator workloads are downgraded or excluded by the scheduler.");
    refreshTargets(); return true;
}

void AndroidRuntime::refreshTargets(){
    m_targets.clear();
    if(m_androidEmulator){
        m_androidEmulator->discover();
        for(const auto &a:m_androidEmulator->avds()){
            AndroidTarget t;t.id=a.name;t.api=a.api;t.arch=a.abi;t.state=a.running?"running":"stopped";t.backend="google-emulator";t.stability=classifyTargetStability(a.abi);t.tags=deriveTags(t);t.healthScore=calculateHealthScore(t);m_targets.push_back(t);
        }
    }
    if(m_targets.isEmpty()){
        const auto home=QDir::homePath()+"/.android/avd";QDir d(home);
        for(const auto &entry:d.entryList(QStringList()<<"*.ini",QDir::Files)){AndroidTarget t;t.id=entry.left(entry.size()-4);t.api="unknown";t.arch=m_x86_64Host?"x86_64":"arm64-v8a";t.state="stopped";t.backend="avd";t.stability=classifyTargetStability(t.arch);t.tags=deriveTags(t);t.healthScore=calculateHealthScore(t);m_targets.push_back(t);}
    }
    if(m_targets.isEmpty()){
        AndroidTarget x86;x86.id="x86_64-dev";x86.api="native-dev";x86.arch="x86_64";x86.state="ready";x86.backend="host-dev";x86.stability="preferred";x86.tags=deriveTags(x86);x86.healthScore=calculateHealthScore(x86);m_targets.push_back(x86);
        AndroidTarget arm;arm.id="arm64-dev";arm.api="native-dev";arm.arch="arm64-v8a";arm.state="ready";arm.backend="host-dev";arm.stability="fundamental";arm.tags=deriveTags(arm);arm.healthScore=calculateHealthScore(arm);m_targets.push_back(arm);
    }
}

int AndroidRuntime::indexOf(const QString &id) const{for(int i=0;i<m_targets.size();++i)if(m_targets[i].id==id)return i;return -1;}

QString AndroidRuntime::resolveTarget(const QString &requested, const QString &abi) const {
    if (!requested.trimmed().isEmpty()) {
        return indexOf(requested.trimmed()) >= 0 ? requested.trimmed() : QString();
    }

    const QString normalizedAbi = abi.trimmed();
    const AndroidTarget *best = nullptr;
    for (const AndroidTarget &candidate : m_targets) {
        if (!normalizedAbi.isEmpty() &&
            !candidate.arch.contains(normalizedAbi, Qt::CaseInsensitive)) {
            continue;
        }
        if (!best || candidate.healthScore > best->healthScore ||
            (candidate.healthScore == best->healthScore &&
             candidate.stability == QStringLiteral("preferred") &&
             best->stability != QStringLiteral("preferred"))) {
            best = &candidate;
        }
    }
    return best ? best->id : QString();
}

bool AndroidRuntime::start(const QString &id){
    const int i=indexOf(id);if(i<0)return false;if(m_targets[i].state=="running")return true;
    bool ok=m_androidEmulator?m_androidEmulator->start(id,true):false;
    if(!ok&&m_targets[i].backend=="host-dev"){m_targets[i].state="ready";emit logMessage("Host development target selected: "+id+" ("+m_targets[i].arch+")");ok=true;}
    if(ok){m_targets[i].state="running";m_targets[i].tags=deriveTags(m_targets[i]);m_targets[i].healthScore=calculateHealthScore(m_targets[i]);emit logMessage("Started Android target "+id+" ["+m_targets[i].arch+", "+m_targets[i].stability+"]");emit targetChanged(id);}return ok;
}

bool AndroidRuntime::stop(const QString &id){const int i=indexOf(id);if(i<0)return false;bool ok=m_androidEmulator?m_androidEmulator->stop(id):true;m_targets[i].state="stopped";m_targets[i].tags=deriveTags(m_targets[i]);m_targets[i].healthScore=calculateHealthScore(m_targets[i]);emit logMessage("Stopped target "+id);emit targetChanged(id);return ok;}
bool AndroidRuntime::restart(const QString &id){return stop(id)&&start(id);}

bool AndroidRuntime::shell(const QString &id,const QString &command){const int i=indexOf(id);if(i<0||m_targets[i].state!="running")return false;if(m_androidEmulator){QString out;const bool ok=m_androidEmulator->adb({"-s",id,"shell",command},&out);emit logMessage("["+id+"] "+out.trimmed());return ok;}return false;}
bool AndroidRuntime::screenshot(const QString &id,const QString &path){const int i=indexOf(id);if(i<0||m_targets[i].state!="running")return false;if(m_androidEmulator){QProcess p;p.start(m_androidEmulator->adbPath(),{"-s",id,"exec-out","screencap","-p"});if(!p.waitForFinished(15000))return false;QFile f(path);if(!f.open(QIODevice::WriteOnly))return false;f.write(p.readAllStandardOutput());return true;}return false;}

QJsonObject AndroidRuntime::status() const{return {{"architecture",m_arch},{"kernel",m_kernel},{"kvm",m_kvm},{"qemu",m_qemu},{"android_emulator",m_emulator},{"arm64_host",m_arm64Host},{"x86_64_host",m_x86_64Host},{"arm64_abi",m_hasArm64Abi},{"x86_64_abi",m_hasX8664Abi},{"hybrid_abi",hybridAbiAvailable()},{"supported_abis",m_supportedAbis.join(",")},{"targets",m_targets.size()}};}

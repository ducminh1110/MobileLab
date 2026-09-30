#include "AndroidPackageCatalog.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>

AndroidPackageCatalog::AndroidPackageCatalog(const QString &sdkRoot){
    m_sdkRoot=sdkRoot.isEmpty()?qEnvironmentVariable("ANDROID_SDK_ROOT"):sdkRoot;
    if(m_sdkRoot.isEmpty()) m_sdkRoot=qEnvironmentVariable("ANDROID_HOME");
    if(m_sdkRoot.isEmpty()) m_sdkRoot=QDir::homePath()+"/Android/Sdk";
}

QVector<AndroidPackage> AndroidPackageCatalog::systemImages() const{
    QVector<AndroidPackage> out;
    QDir root(m_sdkRoot+"/system-images");
    if(!root.exists()) return out;
    for(const auto &apiDir:root.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
        QDir api(root.filePath(apiDir));
        for(const auto &tag:api.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
            QDir abi(api.filePath(tag));
            for(const auto &img:abi.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
                AndroidPackage p;
                const QString level=apiDir.startsWith("android-")?apiDir.mid(8):apiDir;
                p.api=level;
                p.abi=img;
                p.id="system-images;"+apiDir+";"+tag+";"+img;
                p.displayName="Android "+level+" - "+tag+" - "+img;
                p.installed=true;
                out.push_back(p);
            }
        }
    }
    return out;
}

QVector<AndroidPackage> AndroidPackageCatalog::installedPackages() const{
    QVector<AndroidPackage> out=systemImages();
    return out;
}

bool AndroidPackageCatalog::hasPackage(const QString &id) const{
    for(const auto &p:installedPackages()) if(p.id==id) return true;
    return false;
}

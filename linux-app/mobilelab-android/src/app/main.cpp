#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStyleHints>
#include "MainWindow.h"
#include "ScreenshotDriver.h"
#include "Theme.h"
#include "XcodeStyle.h"
#include "Icons.h"

int main(int argc, char *argv[]) {
    // Settings location can be redirected (tests, screenshots) before anything reads them.
    const QByteArray settingsDir = qgetenv("MOBILELAB_SETTINGS_DIR");
    QApplication app(argc, argv);
    app.setApplicationName("MobileLab Android");
    app.setOrganizationName("MobileLab");
    app.setApplicationDisplayName("MobileLab Android");
    app.setApplicationVersion(MOBILELAB_VERSION);
    app.setDesktopFileName("mobilelab-android");
    app.setWindowIcon(Icons::icon("mobilelab.logo", Qt::white, 256));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    if (!settingsDir.isEmpty()) {
        QDir().mkpath(QString::fromLocal8Bit(settingsDir));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromLocal8Bit(settingsDir));
    }
    Theme::ensureFonts();
    Theme &theme = Theme::instance();
    {
        QSettings s;
        theme.load(s);
    }
    app.setStyle(new XcodeStyle);
    app.setFont(theme.ui(13));
    app.setPalette(theme.palette());
    QObject::connect(&theme, &Theme::changed, &app, [&app, &theme] {
        app.setPalette(theme.palette());
        Icons::clearCache();
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &theme, [&theme](Qt::ColorScheme) { theme.setSystemDark(Theme::detectSystemDark()); });
#endif
    MainWindow window;
    window.setMinimumSize(1000, 640);
    if (ScreenshotDriver::requested()) {
        ScreenshotDriver driver(&window);
        window.show();
        driver.start();
        return app.exec();
    }
    window.show();
    return app.exec();
}

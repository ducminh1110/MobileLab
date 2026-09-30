#pragma once
#include <QObject>
#include <QString>
class MainWindow;

// MOBILELAB_SCREENSHOT_DIR=<dir>: after the window is shown, drives each navigator, editor and dialog state,
// grabs PNGs, writes a manifest and exits. Sizes and appearance come from MOBILELAB_SCREENSHOT_SIZE=WxH and
// MOBILELAB_THEME=light|dark.
class ScreenshotDriver : public QObject {
    Q_OBJECT
public:
    explicit ScreenshotDriver(MainWindow *w);
    static bool requested();
    void start();
private:
    void step();
    void finish(int code);
    MainWindow *m_w;
};

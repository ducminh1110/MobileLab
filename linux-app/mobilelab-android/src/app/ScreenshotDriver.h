#pragma once
#include <QObject>
#include <QString>
class MainWindow;

// MOBILELAB_SCREENSHOT_DIR=<dir>: after the window is shown, drives each navigator, editor and dialog state,
// grabs PNGs, writes a manifest and exits. Sizes and appearance come from MOBILELAB_SCREENSHOT_SIZE=WxH,
// MOBILELAB_THEME=light|dark, MOBILELAB_ACCENT=blue|purple|pink|red|orange|green|graphite and
// MOBILELAB_TEXT_SIZE=small|default|large|larger. Near the end it switches accent and text size while running
// (welcome, report, debug area and settings at Larger with Purple, then Small with Orange) and switches back.
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

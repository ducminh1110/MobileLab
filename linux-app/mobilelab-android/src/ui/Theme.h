#pragma once
// Design tokens (docs/design/xcode-interface.md section 3), fonts and the light/dark switch.
#include <QColor>
#include <QFont>
#include <QObject>
#include <QPalette>

class QSettings;

struct Tokens {
    bool dark = false;
    // Spec tokens, named exactly like the design document.
    QColor window, sidebar, editor, gutter, gutterText, divider, text, textSecondary, textTertiary;
    QColor accent, accentText, selection, selectionFocused, capsule, folder, field, debugBar, console, consoleFooter;
    QColor lineCurrent, lineError, pass, fail, warn, running, breakpoint;
    QColor synComment, synKeyword, synString, synNumber, synType, synIdentifier, synError;
    // Window chrome (tinted background behind the floating panels) and shadows.
    QColor windowTop, windowMid, windowBottom, blobA, blobB, panelBorder, shadow;
    // Liquid glass material.
    QColor glassTintTop, glassTintBottom, glassRimLight, glassRimDark, glassSheen, glassOpaque, glassHover, glassPressed;
};

namespace Metrics {
constexpr int toolbarHeight = 52;
constexpr int controlHeight = 28;
constexpr int capsuleHeight = 34;
constexpr int jumpBarHeight = 28;
constexpr int navRow = 22;
constexpr int gutterRow = 17;
constexpr int navTabBar = 44;
constexpr int inspectorTabBar = 32;
constexpr int filterBar = 34;
constexpr int panelRadius = 14;
constexpr int panelGap = 8;
constexpr int debugBar = 28;
constexpr int footerHeight = 30;
}

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };
    static Theme &instance();
    static Tokens tokens(bool dark);
    // Registers the embedded fonts once; returns false if Inter or JetBrains Mono could not be resolved.
    static bool ensureFonts();
    static QString uiFamily();
    static QString monoFamily();

    Mode mode() const { return m_mode; }
    void setMode(Mode m);
    bool dark() const { return m_dark; }
    const Tokens &t() const { return m_tokens; }
    // Called at start-up and when the desktop colour scheme changes.
    void setSystemDark(bool dark);
    static bool detectSystemDark();
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool on);
    QFont ui(qreal px, QFont::Weight w = QFont::Normal) const;
    QFont mono(qreal px, QFont::Weight w = QFont::Normal) const;
    QPalette palette() const;
    void load(QSettings &s);
    void save(QSettings &s) const;

signals:
    void changed();
    void motionChanged();

private:
    Theme();
    void recompute();
    Mode m_mode = Mode::System;
    bool m_systemDark = false;
    bool m_dark = false;
    bool m_reducedMotion = false;
    Tokens m_tokens;
};

inline const Tokens &tk() { return Theme::instance().t(); }

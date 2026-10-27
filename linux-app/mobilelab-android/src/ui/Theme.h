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

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };
    enum class Accent { Blue, Purple, Pink, Red, Orange, Green, Graphite };
    enum class TextSize { Small, Default, Large, Larger };
    static constexpr int accentCount = 7;
    static constexpr int textSizeCount = 4;
    static Theme &instance();
    static Tokens tokens(bool dark, Accent accent = Accent::Blue);
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
    // Accent colour: the swatch value for a light or dark window (white text on it keeps at least 3:1).
    static QString accentName(Accent a);
    static QString accentKey(Accent a);
    static Accent accentFromKey(const QString &key, Accent fallback = Accent::Blue);   // unknown values give `fallback`
    static QColor accentColor(Accent a, bool dark);
    Accent accent() const { return m_accent; }
    void setAccent(Accent a);
    // Text size: scales interface text and the heights that hold it. Default is exactly the unscaled layout.
    static QString textSizeName(TextSize s);
    static QString textSizeKey(TextSize s);
    static TextSize textSizeFromKey(const QString &key, TextSize fallback = TextSize::Default);   // unknown values give `fallback`
    static qreal textScaleOf(TextSize s);
    TextSize textSize() const { return m_textSize; }
    qreal textScale() const { return textScaleOf(m_textSize); }
    void setTextSize(TextSize s);
    // `px` as a pixel length at the current text size (a height or width that holds text).
    static int scaled(int px) { return qRound(px * instance().textScale()); }
    // Mode System, accent Blue, text size Default and reduce motion off; nothing else.
    void resetAppearance();
    QFont ui(qreal px, QFont::Weight w = QFont::Normal) const;
    QFont mono(qreal px, QFont::Weight w = QFont::Normal) const;
    QPalette palette() const;
    void load(QSettings &s);
    void save(QSettings &s) const;

signals:
    void changed();
    // Sent before changed() so cached sizes are redone before everything repaints.
    void textScaleChanged();
    void motionChanged();

private:
    Theme();
    void recompute();
    Mode m_mode = Mode::System;
    Accent m_accent = Accent::Blue;
    TextSize m_textSize = TextSize::Default;
    bool m_systemDark = false;
    bool m_dark = false;
    bool m_reducedMotion = false;
    Tokens m_tokens;
};

inline const Tokens &tk() { return Theme::instance().t(); }

// Layout metrics from the design document. The ones that hold text follow the text size (Default = the numbers
// below); panel geometry and the icon-only tab bars do not.
namespace Metrics {
constexpr int panelRadius = 14;
constexpr int panelGap = 8;
constexpr int navTabBar = 44;
constexpr int inspectorTabBar = 32;
inline int toolbarHeight() { return Theme::scaled(52); }
inline int controlHeight() { return Theme::scaled(28); }
inline int capsuleHeight() { return Theme::scaled(34); }
inline int jumpBarHeight() { return Theme::scaled(28); }
inline int navRow() { return Theme::scaled(22); }
inline int gutterRow() { return Theme::scaled(17); }
inline int filterBar() { return Theme::scaled(34); }
inline int debugBar() { return Theme::scaled(28); }
inline int footerHeight() { return Theme::scaled(30); }
}

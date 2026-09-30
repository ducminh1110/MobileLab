#include "Theme.h"
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QDebug>

static void initResources() { Q_INIT_RESOURCE(mobilelab_resources); }

namespace {
QColor c(const char *hex) { return QColor(QLatin1String(hex)); }
QColor a(int r, int g, int b, qreal alpha) { return QColor(r, g, b, qRound(alpha * 255)); }
QString g_ui = "Inter", g_mono = "JetBrains Mono";
bool g_fontsLoaded = false, g_fontsOk = false;
}

Theme::Theme() {
    const QByteArray env = qgetenv("MOBILELAB_REDUCED_MOTION");
    m_reducedMotion = env == "1" || env.toLower() == "true";
    m_systemDark = detectSystemDark();
    recompute();
}

Theme &Theme::instance() {
    static Theme t;
    return t;
}

Tokens Theme::tokens(bool dark) {
    Tokens k;
    k.dark = dark;
    if (!dark) {
        k.window = c("#f5f5f7"); k.sidebar = c("#f3f5f7"); k.editor = c("#ffffff"); k.gutter = c("#fbfbfc");
        k.gutterText = c("#b4b6ba"); k.divider = c("#e4e4e8"); k.text = c("#1d1d1f"); k.textSecondary = c("#6c6c72");
        k.textTertiary = c("#a1a1a8"); k.accent = c("#0a7aff"); k.accentText = c("#ffffff");
        k.selection = c("#dcdde1"); k.capsule = c("#ffffff"); k.folder = c("#5cb0f5"); k.field = c("#efeff1");
        k.debugBar = c("#f2f2f4"); k.console = c("#eaf1e8"); k.consoleFooter = c("#dbe7d9");
        k.lineCurrent = c("#e5f7e8"); k.lineError = c("#fdeceb"); k.pass = c("#30b356"); k.fail = c("#ff3b30");
        k.warn = c("#ff9f0a"); k.breakpoint = c("#097dfe");
        k.synComment = c("#5d6c79"); k.synKeyword = c("#ad3da4"); k.synString = c("#d12f1b"); k.synNumber = c("#272ad8");
        k.synType = c("#703daa"); k.synIdentifier = c("#326d74"); k.synError = c("#ff3b30");
        k.windowTop = c("#e7eef8"); k.windowBottom = c("#dde6f2");
        k.blobA = a(96, 165, 255, 0.34); k.blobB = a(170, 140, 255, 0.20);
        k.panelBorder = a(0, 0, 0, 0.07); k.shadow = a(20, 30, 60, 0.20);
        k.glassTintTop = a(255, 255, 255, 0.50); k.glassTintBottom = a(255, 255, 255, 0.30);
        k.glassRimLight = a(255, 255, 255, 0.95); k.glassRimDark = a(0, 0, 0, 0.14);
        k.glassSheen = a(255, 255, 255, 0.55); k.glassOpaque = c("#ffffff");
        k.glassHover = a(0, 0, 0, 0.05); k.glassPressed = a(0, 0, 0, 0.12);
    } else {
        k.window = c("#2a2a2d"); k.sidebar = c("#262629"); k.editor = c("#1f1f24"); k.gutter = c("#1f1f24");
        k.gutterText = c("#5f6068"); k.divider = c("#3a3a3f"); k.text = c("#ececee"); k.textSecondary = c("#a0a0a8");
        k.textTertiary = c("#6f6f78"); k.accent = c("#0a84ff"); k.accentText = c("#ffffff");
        k.selection = c("#3c3c42"); k.capsule = c("#3b3b3f"); k.folder = c("#5cb0f5"); k.field = c("#333338");
        k.debugBar = c("#2c2c30"); k.console = c("#1d2a22"); k.consoleFooter = c("#17211b");
        k.lineCurrent = c("#26372c"); k.lineError = c("#3a2426"); k.pass = c("#32d15b"); k.fail = c("#ff453a");
        k.warn = c("#ffb340"); k.breakpoint = c("#0a84ff");
        k.synComment = c("#6c7986"); k.synKeyword = c("#fc5fa3"); k.synString = c("#fc6a5d"); k.synNumber = c("#d0bf69");
        k.synType = c("#d0a8ff"); k.synIdentifier = c("#67b7a4"); k.synError = c("#ff453a");
        k.windowTop = c("#33363f"); k.windowBottom = c("#24262c");
        k.blobA = a(60, 110, 200, 0.34); k.blobB = a(110, 80, 190, 0.22);
        k.panelBorder = a(255, 255, 255, 0.08); k.shadow = a(0, 0, 0, 0.55);
        k.glassTintTop = a(70, 72, 82, 0.50); k.glassTintBottom = a(30, 30, 34, 0.42);
        k.glassRimLight = a(255, 255, 255, 0.34); k.glassRimDark = a(0, 0, 0, 0.50);
        k.glassSheen = a(255, 255, 255, 0.16); k.glassOpaque = c("#3b3b3f");
        k.glassHover = a(255, 255, 255, 0.07); k.glassPressed = a(255, 255, 255, 0.14);
    }
    k.selectionFocused = k.accent;
    k.running = k.accent;
    return k;
}

bool Theme::ensureFonts() {
    if (g_fontsLoaded) return g_fontsOk;
    g_fontsLoaded = true;
    initResources();
    static const char *files[] = {"Inter-Regular", "Inter-Medium", "Inter-SemiBold", "Inter-Bold",
                                  "JetBrainsMono-Regular", "JetBrainsMono-Medium", "JetBrainsMono-Bold"};
    int loaded = 0;
    for (const char *f : files) {
        QFile file(QString(":/fonts/%1.ttf").arg(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        if (QFontDatabase::addApplicationFontFromData(file.readAll()) >= 0) ++loaded;
    }
    const QStringList fam = QFontDatabase::families();
    g_fontsOk = loaded == 7 && fam.contains("Inter") && fam.contains("JetBrains Mono");
    if (!g_fontsOk) qWarning() << "MobileLab: embedded fonts incomplete, loaded" << loaded << "of 7";
    return g_fontsOk;
}

QString Theme::uiFamily() { return g_ui; }
QString Theme::monoFamily() { return g_mono; }

bool Theme::detectSystemDark() {
    const QByteArray env = qgetenv("MOBILELAB_THEME").toLower();
    if (env == "dark") return true;
    if (env == "light") return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto *h = QGuiApplication::styleHints()) return h->colorScheme() == Qt::ColorScheme::Dark;
#endif
    // Qt < 6.5: infer from the desktop palette as it was before we installed our own.
    static const QPalette systemPalette = QGuiApplication::palette();
    return systemPalette.color(QPalette::Window).lightness() < 128;
}

void Theme::recompute() {
    const bool dark = m_mode == Mode::Dark || (m_mode == Mode::System && m_systemDark);
    m_dark = dark;
    m_tokens = tokens(dark);
}

void Theme::setMode(Mode m) {
    if (m == m_mode) return;
    m_mode = m;
    recompute();
    emit changed();
}

void Theme::setSystemDark(bool dark) {
    if (dark == m_systemDark) return;
    m_systemDark = dark;
    const bool before = m_dark;
    recompute();
    if (before != m_dark) emit changed();
}

void Theme::setReducedMotion(bool on) {
    if (on == m_reducedMotion) return;
    m_reducedMotion = on;
    emit motionChanged();
}

QFont Theme::ui(qreal px, QFont::Weight w) const {
    QFont f(g_ui);
    f.setPixelSize(qRound(px));
    if (px != qRound(px)) f.setPixelSize(qRound(px));
    f.setWeight(w);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont Theme::mono(qreal px, QFont::Weight w) const {
    QFont f(g_mono);
    f.setPixelSize(qRound(px));
    f.setWeight(w);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setFixedPitch(true);
    return f;
}

QPalette Theme::palette() const {
    const Tokens &k = m_tokens;
    QPalette p;
    p.setColor(QPalette::Window, k.window);
    p.setColor(QPalette::WindowText, k.text);
    p.setColor(QPalette::Base, k.editor);
    p.setColor(QPalette::AlternateBase, k.sidebar);
    p.setColor(QPalette::Text, k.text);
    p.setColor(QPalette::Button, k.field);
    p.setColor(QPalette::ButtonText, k.text);
    p.setColor(QPalette::BrightText, k.accentText);
    p.setColor(QPalette::Highlight, k.accent);
    p.setColor(QPalette::HighlightedText, k.accentText);
    p.setColor(QPalette::ToolTipBase, k.capsule);
    p.setColor(QPalette::ToolTipText, k.text);
    p.setColor(QPalette::PlaceholderText, k.textTertiary);
    p.setColor(QPalette::Link, k.accent);
    p.setColor(QPalette::Light, k.editor);
    p.setColor(QPalette::Mid, k.divider);
    p.setColor(QPalette::Dark, k.divider);
    p.setColor(QPalette::Shadow, k.shadow);
    p.setColor(QPalette::Disabled, QPalette::Text, k.textTertiary);
    p.setColor(QPalette::Disabled, QPalette::WindowText, k.textTertiary);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, k.textTertiary);
    return p;
}

void Theme::load(QSettings &s) {
    const QString m = s.value("appearance/mode", "system").toString();
    m_mode = m == "light" ? Mode::Light : m == "dark" ? Mode::Dark : Mode::System;
    if (!qEnvironmentVariableIsSet("MOBILELAB_REDUCED_MOTION")) m_reducedMotion = s.value("appearance/reducedMotion", false).toBool();
    // MOBILELAB_THEME forces the appearance (tests, screenshots).
    const QByteArray env = qgetenv("MOBILELAB_THEME").toLower();
    if (env == "dark") m_mode = Mode::Dark;
    else if (env == "light") m_mode = Mode::Light;
    recompute();
}

void Theme::save(QSettings &s) const {
    s.setValue("appearance/mode", m_mode == Mode::Light ? "light" : m_mode == Mode::Dark ? "dark" : "system");
    s.setValue("appearance/reducedMotion", m_reducedMotion);
}

#include "SettingsSheet.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPainter>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include "AndroidContainerRuntime.h"
#include "AndroidEmulator.h"
#include "AndroidRuntime.h"
#include "ApiServer.h"
#include "ArtifactCollector.h"
#include "EditorParts.h"
#include "HostMetrics.h"
#include "Icons.h"
#include "MatrixExecutor.h"
#include "UiUtil.h"
#include "Widgets.h"
#include "glass/Glass.h"

namespace {
class CheckRow : public QWidget {
public:
    CheckRow(bool ok, bool warn, const QString &title, const QString &detail, const QString &remedy, QWidget *parent) : QWidget(parent), m_ok(ok), m_warn(warn), m_title(title), m_detail(detail), m_remedy(remedy) {
        setAccessibleName(title + (ok ? ": ok" : warn ? ": warning" : ": problem"));
        setAccessibleDescription(detail + " " + remedy);
    }
    QSize sizeHint() const override { return QSize(300, heightFor(width() > 0 ? width() : 460)); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return heightFor(w); }
protected:
    void paintEvent(QPaintEvent *) override {
        const Tokens &t = tk();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Icons::paint(&p, m_ok ? "checkmark.diamond.fill" : m_warn ? "exclamationmark.triangle" : "xmark.diamond.fill", QRectF(2, 5, 15, 15), m_ok ? t.pass : m_warn ? t.warn : t.fail);
        p.setFont(Theme::instance().ui(13, QFont::Medium));
        p.setPen(t.text);
        p.drawText(QRect(26, 2, width() - 28, 20), Qt::AlignVCenter | Qt::AlignLeft, m_title);
        int y = 22;
        p.setFont(Theme::instance().ui(12));
        p.setPen(t.textSecondary);
        if (!m_detail.isEmpty()) {
            const QRect r = p.boundingRect(QRect(26, y, width() - 30, 1000), Qt::TextWordWrap, m_detail);
            p.drawText(QRect(26, y, width() - 30, r.height()), Qt::TextWordWrap, m_detail);
            y += r.height() + 2;
        }
        if (!m_ok && !m_remedy.isEmpty()) {
            p.setPen(t.text);
            const QString txt = "Fix: " + m_remedy;
            const QRect r = p.boundingRect(QRect(26, y, width() - 30, 1000), Qt::TextWordWrap, txt);
            p.drawText(QRect(26, y, width() - 30, r.height()), Qt::TextWordWrap, txt);
        }
    }
private:
    int heightFor(int w) const {
        const QFontMetrics fm(Theme::instance().ui(12));
        int h = 24;
        if (!m_detail.isEmpty()) h += fm.boundingRect(QRect(0, 0, w - 30, 1000), Qt::TextWordWrap, m_detail).height() + 2;
        if (!m_ok && !m_remedy.isEmpty()) h += fm.boundingRect(QRect(0, 0, w - 30, 1000), Qt::TextWordWrap, "Fix: " + m_remedy).height();
        return h + 6;
    }
    bool m_ok, m_warn;
    QString m_title, m_detail, m_remedy;
};

QWidget *labeled(const QString &label, QWidget *field, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 2, 0, 2);
    auto *l = new ThemedLabel(label, 13, QFont::Normal, ThemedLabel::Role::Secondary, w);
    l->setFixedWidth(120);
    l->setAlignment(Qt::AlignRight | Qt::AlignTop);
    h->addWidget(l);
    h->addWidget(field, 1);
    return w;
}
}  // namespace

SettingsSheet::SettingsSheet(const AppContext &ctx, QWidget *parent) : SheetDialog("Settings", parent, QSize(600, 560)), m_ctx(ctx) {
    m_tabs = new SegmentedControl({"General", "Environment", "Storage"}, this);
    auto *tabRow = new QHBoxLayout;
    tabRow->addStretch();
    tabRow->addWidget(m_tabs);
    tabRow->addStretch();
    body()->addLayout(tabRow);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildGeneral());
    m_stack->addWidget(buildEnvironment());
    m_stack->addWidget(buildStorage());
    body()->addWidget(m_stack, 1);
    connect(m_tabs, &SegmentedControl::currentChanged, this, [this](int i) {
        m_stack->setCurrentIndex(i);
        if (i == Environment) rebuildEnvironment();
    });
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *done = new PillButton("Done", PillButton::Style::Primary, this);
    connect(done, &QAbstractButton::clicked, this, &QDialog::accept);
    row->addWidget(done);
    body()->addLayout(row);
}

void SettingsSheet::showPane(Pane p) {
    m_tabs->setCurrent(p);
    m_stack->setCurrentIndex(p);
    if (p == Environment) rebuildEnvironment();
}

SettingsSheet::Pane SettingsSheet::pane() const { return Pane(m_stack->currentIndex()); }

QWidget *SettingsSheet::buildGeneral() {
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(4, 10, 4, 0);
    v->setSpacing(6);
    auto radios = [&](const QStringList &names, int current, std::function<void(int)> apply, const QString &group) {
        auto *w = new QWidget(page);
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(4);
        auto *bg = new QButtonGroup(w);
        for (int i = 0; i < names.size(); ++i) {
            auto *r = new QRadioButton(names[i], w);
            r->setFont(Theme::instance().ui(13));
            r->setChecked(i == current);
            r->setAccessibleName(group + " " + names[i]);
            bg->addButton(r, i);
            l->addWidget(r);
        }
        connect(bg, &QButtonGroup::idClicked, this, apply);
        return w;
    };
    const int mode = int(Theme::instance().mode());
    v->addWidget(labeled("Appearance", radios({"System", "Light", "Dark"}, mode, [](int i) { Theme::instance().setMode(Theme::Mode(i)); }, "Appearance"), page));
    const int lvl = int(Glass::level());
    auto *glassBox = radios({"Off (opaque controls)", "Blur (frosted, no refraction)", "Full (refracting Liquid Glass)"}, lvl, [](int i) { Glass::Settings::instance().setLevel(Glass::Level(i)); }, "Liquid Glass");
    auto *glassWrap = new QWidget(page);
    auto *gv = new QVBoxLayout(glassWrap);
    gv->setContentsMargins(0, 0, 0, 0);
    gv->setSpacing(2);
    gv->addWidget(glassBox);
    if (Glass::Settings::instance().envOverride()) {
        auto *note = new ThemedLabel("MOBILELAB_GLASS is set in the environment and wins at the next start.", 11, QFont::Normal, ThemedLabel::Role::Tertiary, glassWrap);
        note->setWordWrap(true);
        gv->addWidget(note);
    }
    v->addWidget(labeled("Liquid Glass", glassWrap, page));
    auto *motion = new QCheckBox("Reduce motion", page);
    motion->setFont(Theme::instance().ui(13));
    motion->setChecked(Theme::instance().reducedMotion());
    connect(motion, &QCheckBox::toggled, this, [](bool on) { Theme::instance().setReducedMotion(on); });
    v->addWidget(labeled("Motion", motion, page));
    auto *api = new ThemedLabel(m_ctx.api && m_ctx.api->isListening() ? QString("http://127.0.0.1:%1").arg(m_ctx.api->port()) : QString("Not listening: %1").arg(m_ctx.apiError), 12, QFont::Normal, ThemedLabel::Role::Text, page, true);
    v->addWidget(labeled("REST API", api, page));
    auto *hint = new ThemedLabel("Set MOBILELAB_ANDROID_API_PORT to change the port (default 4100; 4000 is used by the Node backend). It takes effect at the next start.", 11, QFont::Normal, ThemedLabel::Role::Tertiary, page);
    hint->setWordWrap(true);
    v->addWidget(labeled("", hint, page));
    v->addStretch();
    return page;
}

QWidget *SettingsSheet::buildEnvironment() {
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(4, 8, 4, 0);
    m_envHost = new QWidget(page);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidget(m_envHost);
    m_envHost->setAutoFillBackground(false);
    scroll->setAccessibleName("Environment checks");
    v->addWidget(scroll, 1);
    auto *h = new QHBoxLayout;
    auto *again = new PillButton("Run Checks Again", PillButton::Style::Secondary, page, "arrow.clockwise");
    connect(again, &QAbstractButton::clicked, this, [this] { emit rerunChecks(); rebuildEnvironment(); });
    h->addWidget(again);
    h->addStretch();
    v->addLayout(h);
    rebuildEnvironment();
    return page;
}

void SettingsSheet::rebuildEnvironment() {
    if (!m_envHost) return;
    qDeleteAll(m_envHost->children());
    delete m_envHost->layout();
    auto *v = new QVBoxLayout(m_envHost);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto add = [&](bool ok, bool warn, const QString &t, const QString &d, const QString &r) { v->addWidget(new CheckRow(ok, warn, t, d, r, m_envHost)); };
    if (m_ctx.emulator) {
        const auto info = m_ctx.emulator->info();
        add(QFileInfo::exists(info.emulatorPath), false, "Android emulator", info.emulatorPath, "sdkmanager \"emulator\", then set ANDROID_HOME to that SDK.");
        add(QFileInfo::exists(info.adbPath), false, "adb", info.adbPath, "sdkmanager \"platform-tools\" or put adb on PATH.");
        add(QFileInfo::exists(info.cmdlineToolsPath), true, "avdmanager", info.cmdlineToolsPath, "sdkmanager \"cmdline-tools;latest\" (needed to create AVDs).");
        const int n = m_ctx.emulator->installedSystemImages().size();
        add(n > 0, true, "System images", n ? QString("%1 installed under %2/system-images").arg(n).arg(info.sdkRoot) : "None under " + info.sdkRoot + "/system-images",
            "sdkmanager \"system-images;android-35;google_apis;x86_64\"");
        add(true, false, "AVD home", m_ctx.emulator->avdHome() + QString("  (%1 AVDs)").arg(m_ctx.emulator->avds().size()), {});
    }
    if (m_ctx.runtime) {
        add(m_ctx.runtime->kvmAvailable(), true, "KVM acceleration", m_ctx.runtime->kvmAvailable() ? "/dev/kvm is present" : "/dev/kvm is missing or not accessible", "Enable virtualization in firmware; sudo usermod -aG kvm $USER; log in again.");
        add(m_ctx.runtime->qemuAvailable(), true, "QEMU", m_ctx.runtime->qemuAvailable() ? "qemu-system found on PATH" : "No qemu-system binary on PATH", "Install qemu-system-x86 and qemu-system-arm (optional, for userspace ARM).");
        add(m_ctx.runtime->hybridAbiAvailable(), true, "Hybrid ABIs", m_ctx.runtime->supportedAbis().isEmpty() ? "No ABIs" : "Installed: " + m_ctx.runtime->supportedAbis().join(", "), "Install both an x86_64 and an arm64-v8a image to run the hybrid matrix.");
    }
    if (m_ctx.container) {
        const QJsonObject d = m_ctx.container->diagnostics();
        const bool viable = d.value("arm64").toObject().value("container_backend_viable").toBool();
        add(viable, true, "Waydroid container backend", viable ? "ARM64 host with binder and cgroups" : "Needs an ARM64 host with binder, cgroups and Waydroid", "Optional: install Waydroid on ARM64 Linux.");
    }
    add(m_ctx.api && m_ctx.api->isListening(), false, "REST server", m_ctx.api && m_ctx.api->isListening() ? QString("Listening on 127.0.0.1:%1").arg(m_ctx.api->port()) : m_ctx.apiError, "Set MOBILELAB_ANDROID_API_PORT to a free port.");
    if (m_ctx.matrix) add(!m_ctx.matrix->configSource().isEmpty(), true, "Matrix configuration", m_ctx.matrix->configSource().isEmpty() ? "config/matrix/hybrid-x86_64-arm64.yaml not found; every ABI costs 1 unit" : m_ctx.matrix->configSource(), "Set MOBILELAB_ANDROID_CONFIG to the config directory.");
    v->addStretch();
    m_envHost->show();
}

QWidget *SettingsSheet::buildStorage() {
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(4, 10, 4, 0);
    v->setSpacing(6);
    const QString path = m_ctx.artifacts ? m_ctx.artifacts->root() : QString();
    v->addWidget(labeled("Artifacts", new ThemedLabel(path, 12, QFont::Normal, ThemedLabel::Role::Text, page, true), page));
    auto *size = new ThemedLabel("Measuring...", 12, QFont::Normal, ThemedLabel::Role::Text, page);
    v->addWidget(labeled("Size on disk", size, page));
    const int runs = m_ctx.matrix ? m_ctx.matrix->records().size() : 0;
    v->addWidget(labeled("Runs", new ThemedLabel(QString::number(runs), 12, QFont::Normal, ThemedLabel::Role::Text, page), page));
    QMetaObject::invokeMethod(this, [this, size] { size->setText(m_ctx.artifacts ? HostMetrics::formatBytes(double(m_ctx.artifacts->sizeOnDisk())) : "n/a"); }, Qt::QueuedConnection);
    auto *cleanRow = new QWidget(page);
    auto *ch = new QHBoxLayout(cleanRow);
    ch->setContentsMargins(0, 0, 0, 0);
    auto *days = new PopupButton(cleanRow);
    days->setItems({"1 day", "7 days", "14 days", "30 days", "90 days"}, 3);
    days->setAccessibleName("Delete runs older than");
    connect(days, &PopupButton::currentChanged, this, [this](int i) { static const int d[] = {1, 7, 14, 30, 90}; m_days = d[i]; });
    ch->addWidget(new ThemedLabel("Delete runs older than", 13, QFont::Normal, ThemedLabel::Role::Text, cleanRow));
    ch->addWidget(days);
    auto *clean = new PillButton("Clean Up", PillButton::Style::Secondary, cleanRow, "trash");
    auto *result = new ThemedLabel({}, 12, QFont::Normal, ThemedLabel::Role::Secondary, cleanRow);
    connect(clean, &QAbstractButton::clicked, this, [this, result, size] {
        if (!m_ctx.artifacts) return;
        const int n = m_ctx.artifacts->cleanOlderThan(m_days);
        if (m_ctx.matrix && !m_ctx.matrix->isRunning()) m_ctx.matrix->loadHistory();
        result->setText(QString("Removed %1 %2.").arg(n).arg(n == 1 ? "run" : "runs"));
        size->setText(HostMetrics::formatBytes(double(m_ctx.artifacts->sizeOnDisk())));
        emit artifactsCleaned(n);
    });
    ch->addWidget(clean);
    ch->addWidget(result);
    ch->addStretch();
    v->addWidget(labeled("Clean up", cleanRow, page));
    v->addStretch();
    return page;
}

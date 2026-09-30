#include "WelcomeEditor.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include "AndroidEmulator.h"
#include "AndroidRuntime.h"
#include "EditorParts.h"
#include "Icons.h"
#include "MatrixExecutor.h"
#include "UiUtil.h"

namespace {
// Large action row: icon tile, bold title, one line of explanation.
class BigRow : public QAbstractButton {
public:
    BigRow(const QString &icon, const QString &title, const QString &sub, QWidget *parent) : QAbstractButton(parent), m_icon(icon), m_title(title), m_sub(sub) {
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(title);
        setAccessibleDescription(sub);
        setFixedHeight(58);
        setMinimumWidth(320);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        const Tokens &t = tk();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (underMouse() || isDown()) {
            p.setPen(Qt::NoPen);
            p.setBrush(isDown() ? t.glassPressed : t.glassHover);
            p.drawRoundedRect(r, 10, 10);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(Ui::withAlpha(t.accent, t.dark ? 46 : 28));
        p.drawRoundedRect(QRectF(12, 11, 36, 36), 9, 9);
        Icons::paint(&p, m_icon, QRectF(20, 19, 20, 20), t.accent);
        p.setFont(Theme::instance().ui(13, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRect(62, 10, width() - 72, 20), Qt::AlignVCenter | Qt::AlignLeft, m_title);
        p.setFont(Theme::instance().ui(12));
        p.setPen(t.textSecondary);
        p.drawText(QRect(62, 30, width() - 72, 18), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(p.font()).elidedText(m_sub, Qt::ElideRight, width() - 74));
        if (hasFocus()) Ui::drawFocusRing(&p, r, 10);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) click();
        else QAbstractButton::keyPressEvent(e);
    }
private:
    QString m_icon, m_title, m_sub;
};

class MarkView : public QWidget {
public:
    explicit MarkView(QWidget *p) : QWidget(p) { setFixedSize(72, 72); }
protected:
    void paintEvent(QPaintEvent *) override { QPainter p(this); p.setRenderHint(QPainter::Antialiasing); Icons::paint(&p, "mobilelab.mark", QRectF(4, 4, 64, 64), tk().accent); }
};
}

WelcomeEditor::WelcomeEditor(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx) {
    setAccessibleName("Welcome");
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(0, 0, 0, 0);
    connect(&Theme::instance(), &Theme::changed, this, [this] { refresh(); });
    if (m_ctx.matrix) connect(m_ctx.matrix, &MatrixExecutor::runChanged, this, [this] { if (isVisible()) refresh(); });
    refresh();
}

void WelcomeEditor::refresh() {
    delete m_content;
    m_content = new QWidget(this);
    auto *outer = new QVBoxLayout(m_content);
    outer->setContentsMargins(20, 20, 20, 20);
    outer->addStretch(1);
    auto *center = new QWidget(m_content);
    center->setMaximumWidth(780);
    auto *c = new QVBoxLayout(center);
    c->setSpacing(4);
    auto *mark = new MarkView(center);
    c->addWidget(mark, 0, Qt::AlignHCenter);
    auto *title = new ThemedLabel("Welcome to MobileLab", 30, QFont::Bold, ThemedLabel::Role::Text, center);
    title->setAlignment(Qt::AlignHCenter);
    c->addWidget(title);
    auto *ver = new ThemedLabel(QString("Android device lab, version %1").arg(MOBILELAB_VERSION), 12, QFont::Normal, ThemedLabel::Role::Secondary, center);
    ver->setAlignment(Qt::AlignHCenter);
    c->addWidget(ver);
    c->addSpacing(18);
    auto *cols = new QHBoxLayout;
    cols->setSpacing(30);
    auto *left = new QVBoxLayout;
    left->setSpacing(4);
    const int targets = m_ctx.runtime ? m_ctx.runtime->targets().size() : 0;
    auto *r1 = new BigRow("play.fill", "Run Matrix...", targets ? QString("Boot %1 %2 and check ABI, API level, logcat and screenshot").arg(targets).arg(targets == 1 ? "target" : "targets") : "Needs at least one virtual device", center);
    connect(r1, &QAbstractButton::clicked, this, [this] { emit action("run", {}); });
    auto *r2 = new BigRow("plus", "Create Virtual Device...", "Provision an AVD from an installed system image", center);
    connect(r2, &QAbstractButton::clicked, this, [this] { emit action("new-avd", {}); });
    auto *r3 = new BigRow("gearshape", "Run Diagnostics...", "Check the Android SDK, KVM and the REST port, with fixes", center);
    connect(r3, &QAbstractButton::clicked, this, [this] { emit action("diagnostics", {}); });
    left->addWidget(r1);
    left->addWidget(r2);
    left->addWidget(r3);
    left->addStretch();
    cols->addLayout(left, 1);
    auto *right = new QVBoxLayout;
    right->setSpacing(0);
    auto *rh = new ThemedLabel("RECENT RUNS", 11, QFont::Bold, ThemedLabel::Role::Secondary, center);
    rh->setContentsMargins(6, 4, 0, 6);
    right->addWidget(rh);
    const auto &recs = m_ctx.matrix ? m_ctx.matrix->records() : QVector<MatrixRunRecord>();
    if (recs.isEmpty()) {
        auto *none = new ThemedLabel("No runs yet. Results of every matrix run are listed here.", 12, QFont::Normal, ThemedLabel::Role::Tertiary, center);
        none->setWordWrap(true);
        none->setContentsMargins(6, 0, 0, 0);
        right->addWidget(none);
    }
    int shown = 0;
    for (int i = recs.size() - 1; i >= 0 && shown < 6; --i, ++shown) {
        const auto &r = recs[i];
        const bool ok = r.state == RunState::Passed, bad = r.state == RunState::Failed;
        const QString what = r.targets.size() == 1 ? r.targets.first().avd : QString("%1 targets").arg(r.targets.size());
        auto *row = new RowButton(ok ? "checkmark.diamond.fill" : bad ? "xmark.diamond.fill" : "diamond", "Android Matrix on " + what,
                                  r.started.toLocalTime().toString("ddd d MMM, HH:mm"), bad ? QString("%1 failed").arg(r.failedSteps()) : QString(), center);
        row->setIconColor(ok ? tk().pass : bad ? tk().fail : tk().textSecondary);
        Location l; l.kind = Location::Run; l.id = r.id; l.tab = "summary";
        connect(row, &QAbstractButton::clicked, this, [this, l] { emit locationRequested(l); });
        right->addWidget(row);
    }
    right->addStretch();
    cols->addLayout(right, 1);
    c->addLayout(cols);
    c->addSpacing(12);
    // environment summary, real values
    QStringList env;
    if (m_ctx.emulator) env << (m_ctx.emulator->info().available ? "Android SDK found" : "Android SDK not found") << QString("%1 installed system images").arg(m_ctx.emulator->installedSystemImages().size());
    if (m_ctx.runtime) env << (m_ctx.runtime->kvmAvailable() ? "KVM available" : "KVM unavailable");
    auto *envl = new ThemedLabel(env.join("    |    "), 11, QFont::Normal, ThemedLabel::Role::Tertiary, center);
    envl->setAlignment(Qt::AlignHCenter);
    c->addWidget(envl);
    outer->addWidget(center, 0, Qt::AlignHCenter);
    outer->addStretch(2);
    // replace
    while (m_root->count()) delete m_root->takeAt(0);
    m_root->addWidget(m_content);
    m_content->show();
}

#include "TargetEditor.h"
#include <QAction>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include "AndroidRuntime.h"
#include "EditorParts.h"
#include "GlassGroup.h"
#include "Icons.h"
#include "UiUtil.h"

TargetEditor::TargetEditor(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAccessibleName("Device canvas");
    m_toggle = new QAction("Boot", this);
    m_camera = new QAction("Take Screenshot", this);
    m_reload = new QAction("Refresh Preview", this);
    m_zoomOut = new QAction("Zoom Out", this);
    m_zoomIn = new QAction("Zoom In", this);
    connect(m_toggle, &QAction::triggered, this, [this] {
        const auto *t = m_ctx.runtime->target(m_id);
        if (t) emit action(t->state == "running" || t->state == "booting" ? "stop" : "start", m_id);
    });
    connect(m_camera, &QAction::triggered, this, [this] { emit action("screenshot", m_id); });
    connect(m_reload, &QAction::triggered, this, &TargetEditor::captureNow);
    connect(m_zoomOut, &QAction::triggered, this, [this] { zoomBy(-1); });
    connect(m_zoomIn, &QAction::triggered, this, [this] { zoomBy(1); });
    m_bar = new GlassGroup(this, Glass::Kind::Control);
    m_bar->setObjectName("canvas-bar");
    m_bar->addButton(m_toggle, "play.fill");
    m_bar->addButton(m_camera, "camera");
    m_bar->addButton(m_reload, "arrow.clockwise");
    m_bar->addButton(m_zoomOut, "minus");
    m_bar->addButton(m_zoomIn, "plus");
    m_boot = new PillButton("Boot", PillButton::Style::Primary, this, "play.fill");
    m_boot->hide();
    connect(m_boot, &QAbstractButton::clicked, this, [this] { emit action("start", m_id); });
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/previews";
    QDir().mkpath(dir);
    m_previewPath = dir;
    m_timer.setParent(this);
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &TargetEditor::captureNow);
    connect(&Theme::instance(), &Theme::changed, this, [this] { update(); });
    connect(&Theme::instance(), &Theme::motionChanged, this, [this] { update(); });
}

void TargetEditor::showTarget(const QString &id) {
    if (id != m_id) {
        m_id = id;
        m_image = QPixmap();
        m_reason.clear();
        m_inflight = false;
    }
    refresh();
}

QString TargetEditor::stateText() const {
    const auto *t = m_ctx.runtime->target(m_id);
    if (!t) return "Unknown target";
    QString s = t->state == "running" ? "Running" : t->state == "booting" ? "Booting" : t->state == "stopping" ? "Shutting down" : "Not running";
    if (!t->serial.isEmpty()) s += " as " + t->serial;
    return s;
}

void TargetEditor::refresh() {
    const auto *t = m_ctx.runtime->target(m_id);
    const bool running = t && t->state == "running";
    const bool active = t && (t->state == "running" || t->state == "booting");
    m_toggle->setText(active ? "Shut Down" : "Boot");
    m_bar->buttons()[0]->setIconName(active ? "stop.fill" : "play.fill");
    m_toggle->setEnabled(t && (t->state == "running" || t->state == "booting" || t->state == "stopped"));
    m_camera->setEnabled(running);
    m_reload->setEnabled(running);
    if (!running) {
        if (!m_image.isNull() && t && t->state == "stopped") m_image = QPixmap();
        m_reason = t ? (t->state == "booting" ? "Booting. The screen appears once Android has started." : "This device is not running.") : "This device no longer exists.";
    } else if (m_image.isNull() && !m_inflight) {
        captureNow();
    }
    m_boot->setVisible(t && t->state == "stopped");
    placeChildren();
    updateTimer();
    update();
}

void TargetEditor::captureNow() {
    const auto *t = m_ctx.runtime->target(m_id);
    if (!t || t->state != "running" || m_inflight) return;
    m_inflight = true;
    const QString id = m_id;
    const QString path = m_previewPath + "/" + id + ".png";
    m_ctx.runtime->screenshotAsync(id, path, [this, id, path](bool ok, const QString &why) {
        m_inflight = false;
        if (id != m_id) return;
        if (ok) {
            QPixmap pm;
            if (pm.load(path)) { m_image = pm; m_reason.clear(); }
            else { m_reason = "The screenshot could not be decoded."; }
        } else {
            m_reason = "Screenshot unavailable: " + why;
        }
        update();
    });
}

void TargetEditor::zoomBy(int steps) {
    m_zoom = qBound(-3, m_zoom + steps, 3);
    m_zoomOut->setEnabled(m_zoom > -3);
    m_zoomIn->setEnabled(m_zoom < 3);
    update();
}

void TargetEditor::updateTimer() {
    const auto *t = m_ctx.runtime->target(m_id);
    const bool need = isVisible() && t && t->state == "running";
    if (need && !m_timer.isActive()) m_timer.start();
    else if (!need && m_timer.isActive()) m_timer.stop();
}

void TargetEditor::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    updateTimer();
    if (m_ctx.runtime->target(m_id) && m_ctx.runtime->target(m_id)->state == "running") captureNow();
}
void TargetEditor::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    updateTimer();
}

void TargetEditor::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    placeChildren();
}

void TargetEditor::placeChildren() {
    const QSize bs = m_bar->sizeHint();
    m_bar->setGeometry((width() - bs.width()) / 2, height() - bs.height() - 8, bs.width(), bs.height());
    const QRectF fr = frameRect();
    const QSize ps = m_boot->sizeHint();
    m_boot->setGeometry(int(fr.center().x() - ps.width() / 2), int(fr.center().y() + 20), ps.width(), ps.height());
}

QRectF TargetEditor::frameRect() const {
    const qreal aspect = m_image.isNull() ? 0.4615 : qreal(m_image.width()) / m_image.height();
    const qreal availH = height() - 60 - 78, availW = width() - 60;
    const qreal factor = std::pow(1.2, m_zoom);
    qreal h = qMin(availH, 760.0) * factor;
    qreal w = h * aspect + 24;
    if (w > availW * factor) { w = availW * factor; h = (w - 24) / aspect; }
    return QRectF((width() - w) / 2, 22 + (availH - h - 24) / 2, w, h + 24);
}

void TargetEditor::paintEvent(QPaintEvent *) {
    const Tokens &t = tk();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // canvas: slightly darker than the editor
    p.fillRect(rect(), Ui::mix(t.editor, t.dark ? QColor(Qt::black) : t.text, t.dark ? 0.18 : 0.035));
    const QRectF fr = frameRect();
    const qreal outer = qMin(44.0, fr.width() * 0.12);
    // drop shadow of the phone
    Ui::drawSoftShadow(&p, fr.toRect(), int(outer), 14, 6, Ui::withAlpha(Qt::black, t.dark ? 130 : 60));
    p.setPen(QPen(QColor("#3a3a3f"), 1));
    p.setBrush(QColor("#0f0f11"));
    p.drawRoundedRect(fr, outer, outer);
    const QRectF screen = fr.adjusted(8, 8, -8, -8);
    const qreal inner = outer - 6;
    QPainterPath clip;
    clip.addRoundedRect(screen, inner, inner);
    p.save();
    p.setClipPath(clip);
    const auto *tg = m_ctx.runtime->target(m_id);
    if (!m_image.isNull() && tg && tg->state == "running") {
        p.drawPixmap(screen, m_image, QRectF(m_image.rect()));
    } else {
        p.fillRect(screen, t.dark ? QColor("#17171b") : QColor("#e9eaee"));
        p.setPen(t.textSecondary);
        p.setFont(Theme::instance().ui(14, QFont::DemiBold));
        const QRectF tr(screen.left() + 16, screen.center().y() - 46, screen.width() - 32, 24);
        const QString head = tg ? (tg->state == "running" ? "No preview" : tg->state == "booting" ? "Booting" : "Not Running") : "Unavailable";
        p.drawText(tr, Qt::AlignCenter, head);
        p.setFont(Theme::instance().ui(12));
        p.setPen(t.textTertiary);
        p.drawText(QRectF(screen.left() + 18, screen.center().y() - 18, screen.width() - 36, 64), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_reason);
        if (m_inflight) {
            p.drawText(QRectF(screen.left() + 18, screen.bottom() - 60, screen.width() - 36, 20), Qt::AlignCenter, "Capturing...");
        }
    }
    p.restore();
    // punch-hole camera
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#000000"));
    p.drawEllipse(QPointF(fr.center().x(), screen.top() + 14), 5.5, 5.5);
    // caption
    if (tg) {
        p.setFont(Theme::instance().ui(12));
        p.setPen(t.textSecondary);
        const QString cap = QString("%1   |   API %2   |   %3   |   %4").arg(tg->id, tg->api, tg->arch, stateText());
        p.drawText(QRectF(10, fr.bottom() + 10, width() - 20, 18), Qt::AlignHCenter | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(cap, Qt::ElideRight, width() - 30));
    }
}

#include "LogView.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include "GlassMenu.h"
#include "Icons.h"
#include "UiUtil.h"
#include "glass/Glass.h"

namespace {
class JumpPill : public GlassPanel {
public:
    explicit JumpPill(QWidget *parent) : GlassPanel(parent) {
        setShape(Shape::Capsule);
        setShadowMargin(3);
        setObjectName("jump-to-end");
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAccessibleName("Jump to end");
    }
    QSize sizeHint() const override { return QSize(116 + 2 * shadowMargin(), 28 + 2 * shadowMargin()); }
    std::function<void()> onClick;
    void paintContent(QPainter &p, const QRect &shape) override {
        Icons::paint(&p, "chevron.down", QRectF(shape.left() + 12, shape.center().y() - 6, 12, 12), tk().text);
        p.setFont(Theme::instance().ui(12, QFont::Medium));
        p.setPen(tk().text);
        p.drawText(shape.adjusted(30, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, "Jump to End");
    }
protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (rect().contains(e->position().toPoint()) && onClick) onClick();
    }
    void mousePressEvent(QMouseEvent *) override { setPressedLook(true); }
    void enterEvent(QEnterEvent *) override { setHoverLook(true); }
    void leaveEvent(QEvent *) override { setHoverLook(false); setPressedLook(false); }
};
}

LogView::LogView(Mode mode, QWidget *parent) : QAbstractScrollArea(parent), m_mode(mode) {
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(mode == Mode::Source ? "Log editor" : "Console");
    verticalScrollBar()->setSingleStep(rowHeight() * 3);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        viewport()->update();
        const bool atEnd = verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 2;
        if (atEnd != m_follow && !m_dragging) {
            m_follow = atEnd;
            emit followChanged(m_follow);
            if (m_pill) m_pill->setVisible(!m_follow);
        }
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); });
    connect(&Theme::instance(), &Theme::changed, this, [this] { m_cache.clear(); viewport()->update(); });
    m_pill = new JumpPill(this);
    static_cast<JumpPill *>(m_pill)->onClick = [this] { setFollow(true); };
    m_pill->hide();
}

int LogView::rowHeight() const { return Metrics::gutterRow; }
int LogView::charWidth() const { return QFontMetrics(Theme::instance().mono(12)).horizontalAdvance('0'); }

int LogView::gutterWidth() const {
    if (m_mode == Mode::Console) return 0;
    const int digits = qMax(3, QString::number(qMax(1, m_lines.size())).size());
    return digits * charWidth() + 40;
}

const LogSyntax::Line &LogView::info(int i) const {
    auto it = m_cache.find(i);
    if (it == m_cache.end()) {
        if (m_cache.size() > 6000) m_cache.clear();
        it = m_cache.insert(i, (m_mode == Mode::Console && !m_xcode) ? LogSyntax::classifyConsole(m_lines[i]) : LogSyntax::classify(m_lines[i]));
    }
    return it.value();
}

void LogView::updateRange() {
    const int n = m_filter ? m_visible.size() : m_lines.size();
    const int total = n * rowHeight() + 12;
    verticalScrollBar()->setRange(0, qMax(0, total - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    const int textW = m_maxCols * charWidth() + 60;
    horizontalScrollBar()->setRange(0, qMax(0, gutterWidth() + textW - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
}

void LogView::setLines(const QStringList &lines) {
    m_lines = lines;
    m_cache.clear();
    m_maxCols = 0;
    for (const auto &l : lines) m_maxCols = qMax(m_maxCols, int(l.size()) + 3 * int(l.count('\t')));
    m_current = -1;
    m_sel0 = m_sel1 = -1;
    if (m_filter) setFilter(m_filter);
    updateRange();
    if (m_follow) verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    viewport()->update();
}

void LogView::setFilter(const std::function<bool(const QString &)> &accept) {
    m_filter = accept;
    m_visible.clear();
    if (m_filter)
        for (int i = 0; i < m_lines.size(); ++i)
            if (m_filter(m_lines[i])) m_visible << i;
    updateRange();
    if (m_follow) verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    viewport()->update();
}

void LogView::appendLine(const QString &line) {
    m_lines << line;
    m_maxCols = qMax(m_maxCols, int(line.size()) + 3 * int(line.count('\t')));
    if (m_filter && m_filter(line)) m_visible << (m_lines.size() - 1);
    updateRange();
    if (m_follow) verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    viewport()->update();
}

void LogView::clear() {
    m_lines.clear();
    m_visible.clear();
    m_cache.clear();
    m_current = m_sel0 = m_sel1 = -1;
    m_maxCols = 0;
    m_follow = true;
    if (m_pill) m_pill->hide();
    updateRange();
    viewport()->update();
}

int LogView::firstVisibleLine() const { return verticalScrollBar()->value() / rowHeight(); }
int LogView::visibleLineCount() const { return viewport()->height() / rowHeight() + 2; }

void LogView::setFollow(bool on) {
    m_follow = on;
    if (on) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        if (m_pill) m_pill->hide();
    }
    emit followChanged(on);
}

void LogView::setHighlight(const QString &text) {
    m_highlight = text;
    viewport()->update();
}

void LogView::setCurrentLine(int line, bool reveal) {
    if (m_lines.isEmpty()) return;
    line = qBound(0, line, m_lines.size() - 1);
    m_current = line;
    if (reveal) {
        int row = line;
        if (m_filter) {
            row = m_visible.indexOf(line);
            if (row < 0) row = 0;
        }
        const int top = row * rowHeight(), bottom = top + rowHeight();
        QScrollBar *sb = verticalScrollBar();
        if (top < sb->value()) sb->setValue(top);
        else if (bottom > sb->value() + viewport()->height() - 6) sb->setValue(bottom - viewport()->height() + 6);
    }
    setAccessibleDescription(QString("Line %1 of %2: %3").arg(line + 1).arg(m_lines.size()).arg(m_lines[line]));
    emit currentLineChanged(line);
    viewport()->update();
}

void LogView::scrollToLine(int line, bool select) {
    if (m_lines.isEmpty() || line < 0) return;
    line = qMin(line, m_lines.size() - 1);
    m_follow = false;
    if (m_pill) m_pill->setVisible(true);
    int row = line;
    if (m_filter) row = qMax(0, m_visible.indexOf(line));
    verticalScrollBar()->setValue(qMax(0, row * rowHeight() - viewport()->height() / 3));
    if (select) { m_current = line; m_sel0 = m_sel1 = line; }
    emit followChanged(false);
    viewport()->update();
}

QString LogView::selectedText() const {
    if (m_sel0 < 0) return m_current >= 0 ? m_lines.value(m_current) : QString();
    QStringList out;
    for (int i = qMin(m_sel0, m_sel1); i <= qMax(m_sel0, m_sel1) && i < m_lines.size(); ++i) out << m_lines[i];
    return out.join('\n');
}

void LogView::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    updateRange();
    placePill();
}

void LogView::placePill() {
    if (!m_pill) return;
    const QSize s = m_pill->sizeHint();
    m_pill->setGeometry((width() - s.width()) / 2, height() - s.height() - 10, s.width(), s.height());
    m_pill->raise();
}

int LogView::lineAtY(int y) const {
    const int row = (y + verticalScrollBar()->value()) / rowHeight();
    const int n = m_filter ? m_visible.size() : m_lines.size();
    if (row < 0 || row >= n) return -1;
    return m_filter ? m_visible[row] : row;
}

void LogView::paintEvent(QPaintEvent *) {
    const Tokens &t = tk();
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, false);
    const int gw = gutterWidth();
    const int rh = rowHeight(), cw = charWidth();
    const int W = viewport()->width(), H = viewport()->height();
    const int scrollY = verticalScrollBar()->value(), scrollX = horizontalScrollBar()->value();
    const bool console = m_mode == Mode::Console;
    if (!console) p.fillRect(QRect(0, 0, gw, H), t.gutter);
    const QFont mono = Theme::instance().mono(12), monoMed = Theme::instance().mono(12, QFont::Medium);
    const int n = m_filter ? m_visible.size() : m_lines.size();
    if (n == 0 && !m_emptyText.isEmpty()) {
        p.setFont(Theme::instance().ui(12));
        p.setPen(t.textTertiary);
        p.drawText(viewport()->rect().adjusted(20, 0, -20, 0), Qt::AlignCenter | Qt::TextWordWrap, m_emptyText);
        return;
    }
    const int first = qMax(0, scrollY / rh - 1), last = qMin(n - 1, (scrollY + H) / rh + 1);
    const int textX = gw + (console ? 10 : 10) - scrollX;
    for (int row = first; row <= last; ++row) {
        const int i = m_filter ? m_visible[row] : row;
        const int y = row * rh - scrollY + (console ? 6 : 6);
        const QRect band(0, y, W, rh);
        const auto &li = info(i);
        const QString text = QString(m_lines[i]).replace('\t', "    ");
        const bool isErrorLine = li.kind == LogSyntax::Kind::Error;
        // line bands: error, then the current (execution) line
        if (!console) {
            if (isErrorLine) p.fillRect(QRect(gw, y, W - gw, rh), t.lineError);
            if (i == m_current) p.fillRect(QRect(gw, y, W - gw, rh), isErrorLine ? t.lineError.darker(103) : t.lineCurrent);
            if (m_sel0 >= 0 && i >= qMin(m_sel0, m_sel1) && i <= qMax(m_sel0, m_sel1)) p.fillRect(QRect(gw, y, W - gw, rh), Ui::withAlpha(t.accent, hasFocus() ? 46 : 26));
        } else if (m_sel0 >= 0 && i >= qMin(m_sel0, m_sel1) && i <= qMax(m_sel0, m_sel1)) {
            p.fillRect(band, Ui::withAlpha(t.accent, 46));
        }
        // search highlight
        if (!m_highlight.isEmpty()) {
            int at = 0;
            while ((at = text.indexOf(m_highlight, at, Qt::CaseInsensitive)) >= 0) {
                p.fillRect(QRect(textX + at * cw - 1, y + 1, m_highlight.size() * cw + 2, rh - 2), Ui::withAlpha(t.warn, 110));
                at += m_highlight.size();
            }
        }
        // text with syntax colours (fixed pitch, so runs are placed by column)
        p.setFont(mono);
        const bool tabs = m_lines[i].contains('\t');
        QVector<LogSyntax::Span> spans = LogSyntax::withGaps(li.spans, m_lines[i].size());
        int col = 0;
        if (!tabs) {
            for (const auto &s : spans) {
                const QString run = text.mid(s.start, s.length);
                p.setPen(LogSyntax::colorFor(s.token, t));
                p.setFont(s.token == LogSyntax::Token::Keyword ? monoMed : mono);
                p.drawText(QRect(textX + (s.start) * cw, y, run.size() * cw + 4, rh), Qt::AlignVCenter | Qt::AlignLeft, run);
            }
        } else {
            // rare: expand tabs per run while keeping colours
            for (const auto &s : spans) {
                QString run = m_lines[i].mid(s.start, s.length).replace('\t', "    ");
                p.setPen(LogSyntax::colorFor(s.token, t));
                p.drawText(QRect(textX + col * cw, y, run.size() * cw + 4, rh), Qt::AlignVCenter | Qt::AlignLeft, run);
                col += run.size();
            }
        }
        if (console) continue;
        // gutter: diamonds on Test Case lines, breakpoint or number
        const bool bp = m_breakpoints.contains(i);
        const bool cur = i == m_current;
        QString num = QString::number(i + 1);
        p.setFont(Theme::instance().mono(11));
        const int numRight = gw - 12;
        if (bp || cur) {
            const int pw = QFontMetrics(p.font()).horizontalAdvance(num) + 16;
            const QRectF pr(numRight - pw + 10, y + 1, pw + 2, rh - 2);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(t.breakpoint);
            p.drawRoundedRect(pr, 7, 7);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(Qt::white);
        } else {
            p.setPen(t.gutterText);
        }
        p.drawText(QRect(0, y, numRight + 2, rh), Qt::AlignVCenter | Qt::AlignRight, num);
        if (li.kind == LogSyntax::Kind::CasePass || li.kind == LogSyntax::Kind::CaseFail) {
            p.setRenderHint(QPainter::Antialiasing, true);
            Icons::paint(&p, li.kind == LogSyntax::Kind::CasePass ? "checkmark.diamond.fill" : "xmark.diamond.fill", QRectF(6, y + 2, 13, 13),
                         li.kind == LogSyntax::Kind::CasePass ? t.pass : t.fail);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }
    // inline pill for failures, drawn last so it overlays long lines
    if (!console) {
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int row = first; row <= last; ++row) {
            const int i = m_filter ? m_visible[row] : row;
            const auto &li = info(i);
            if (li.kind != LogSyntax::Kind::Error) continue;
            const int y = row * rh - scrollY + 6;
            const QFont f = Theme::instance().ui(11, QFont::Medium);
            const QString msg = QFontMetrics(f).elidedText(li.message, Qt::ElideRight, qMax(80, (W - gw) / 3));
            const int pw = QFontMetrics(f).horizontalAdvance(msg) + 34;
            const QRectF pr(W - pw - 8, y + 1, pw, rh - 2);
            // opaque plate so the pill stays readable over the text underneath
            p.setPen(Qt::NoPen);
            p.setBrush(i == m_current ? t.lineError.darker(103) : t.lineError);
            p.drawRoundedRect(pr.adjusted(-3, 0, 3, 0), 8, 8);
            p.setBrush(Ui::withAlpha(t.fail, t.dark ? 70 : 46));
            p.drawRoundedRect(pr, 8, 8);
            Icons::paint(&p, "xmark.diamond.fill", QRectF(pr.left() + 6, y + 3, 11, 11), t.fail);
            p.setFont(f);
            p.setPen(t.dark ? QColor("#ffb4ae") : QColor("#a3140b"));
            p.drawText(QRectF(pr.left() + 21, y, pr.width() - 24, rh), Qt::AlignVCenter | Qt::AlignLeft, msg);
        }
    }
}

void LogView::goto_(int line, bool extend) {
    if (m_lines.isEmpty()) return;
    line = qBound(0, line, m_lines.size() - 1);
    if (!extend) { m_anchor = line; m_sel0 = m_sel1 = -1; }
    else { if (m_anchor < 0) m_anchor = m_current >= 0 ? m_current : line; m_sel0 = m_anchor; m_sel1 = line; }
    setCurrentLine(line);
}

void LogView::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    if (e->button() != Qt::LeftButton && e->button() != Qt::RightButton) return;
    const int i = lineAtY(e->position().toPoint().y() - 6 + 0);
    if (i < 0) return;
    if (m_mode == Mode::Source && e->position().x() < gutterWidth() && e->button() == Qt::LeftButton) {
        const bool on = !m_breakpoints.contains(i);
        if (on) m_breakpoints.insert(i); else m_breakpoints.remove(i);
        emit breakpointToggled(i, on);
        viewport()->update();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        goto_(i, e->modifiers() & Qt::ShiftModifier);
    } else if (m_sel0 < 0 || i < qMin(m_sel0, m_sel1) || i > qMax(m_sel0, m_sel1)) {
        goto_(i, false);
    }
}

void LogView::mouseMoveEvent(QMouseEvent *e) {
    if (!m_dragging) {
        setCursor(m_mode == Mode::Source && e->position().x() < gutterWidth() ? Qt::PointingHandCursor : Qt::IBeamCursor);
        return;
    }
    const int y = qBound(0, int(e->position().y()), viewport()->height() - 1);
    const int i = lineAtY(y - 6);
    if (i >= 0) {
        if (m_anchor < 0) m_anchor = i;
        m_sel0 = m_anchor;
        m_sel1 = i;
        m_current = i;
        viewport()->update();
    }
}

void LogView::mouseReleaseEvent(QMouseEvent *) {
    m_dragging = false;
    if (m_sel0 == m_sel1) m_sel0 = m_sel1 = -1;
}

void LogView::keyPressEvent(QKeyEvent *e) {
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const int cur = m_current < 0 ? 0 : m_current;
    const int page = qMax(1, viewport()->height() / rowHeight() - 1);
    if (e->matches(QKeySequence::Copy)) {
        QApplication::clipboard()->setText(selectedText());
    } else if (e->matches(QKeySequence::SelectAll)) {
        m_sel0 = 0;
        m_sel1 = m_lines.size() - 1;
        viewport()->update();
    } else if (e->key() == Qt::Key_Down) goto_(cur + 1, shift);
    else if (e->key() == Qt::Key_Up) goto_(cur - 1, shift);
    else if (e->key() == Qt::Key_PageDown) goto_(cur + page, shift);
    else if (e->key() == Qt::Key_PageUp) goto_(cur - page, shift);
    else if (e->key() == Qt::Key_Home) goto_(0, shift);
    else if (e->key() == Qt::Key_End) { goto_(m_lines.size() - 1, shift); setFollow(true); }
    else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_mode == Mode::Source && m_current >= 0) {
            const bool on = !m_breakpoints.contains(m_current);
            if (on) m_breakpoints.insert(m_current); else m_breakpoints.remove(m_current);
            emit breakpointToggled(m_current, on);
            viewport()->update();
        }
    } else QAbstractScrollArea::keyPressEvent(e);
}

void LogView::contextMenuEvent(QContextMenuEvent *e) {
    GlassMenu menu(this);
    QAction *copy = menu.addAction("Copy");
    copy->setShortcut(QKeySequence::Copy);
    connect(copy, &QAction::triggered, this, [this] { QApplication::clipboard()->setText(selectedText()); });
    QAction *all = menu.addAction("Select All");
    all->setShortcut(QKeySequence::SelectAll);
    connect(all, &QAction::triggered, this, [this] { m_sel0 = 0; m_sel1 = m_lines.size() - 1; viewport()->update(); });
    menu.exec(e->globalPos());
}

#include "NavCommon.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include "GlassMenu.h"
#include "Icons.h"
#include "UiUtil.h"

// --- NavFilterProxy -----------------------------------------------------------------------------------

bool NavFilterProxy::matches(const QModelIndex &idx) const {
    if (m_text.isEmpty()) return true;
    const QString hay = idx.data(Qt::DisplayRole).toString() + " " + idx.data(NavRole::Sub).toString() + " " + idx.data(NavRole::Trailing).toString();
    return hay.contains(m_text, Qt::CaseInsensitive);
}

bool NavFilterProxy::filterAcceptsRow(int row, const QModelIndex &parent) const {
    if (m_text.isEmpty()) return true;
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    if (matches(idx)) return true;
    // Keep everything below a matching group.
    for (QModelIndex p = parent; p.isValid(); p = p.parent())
        if (matches(p)) return true;
    return false;  // (recursive filtering already keeps ancestors of matches)
}

// --- NavDelegate --------------------------------------------------------------------------------------

QSize NavDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const { return QSize(100, m_tree->compact() ? 20 : Metrics::navRow); }

void NavDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    const Tokens &t = tk();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const bool selected = m_tree->selectionModel()->isSelected(idx);
    const bool onAccent = selected && m_tree->hasFocus();
    const bool dim = idx.data(NavRole::Dim).toBool();
    const QColor fg = onAccent ? t.accentText : (dim ? t.textTertiary : t.text);
    const QColor fg2 = onAccent ? Ui::withAlpha(t.accentText, 200) : t.textSecondary;
    const QRect r = opt.rect;
    int x = r.left() + 2;
    const int right = r.right() - 8;
    const QString status = idx.data(NavRole::Status).toString();
    const QString icon = idx.data(NavRole::Icon).toString();
    const QString square = idx.data(NavRole::Square).toString();
    if (!square.isEmpty()) {
        const QColor sc = idx.data(NavRole::SquareColor).value<QColor>();
        const QRectF sq(x + 1, r.center().y() - 7, 14, 14);
        p->setPen(Qt::NoPen);
        p->setBrush(sc.isValid() ? sc : t.textTertiary);
        p->drawRoundedRect(sq, 3.5, 3.5);
        p->setFont(Theme::instance().ui(9, QFont::Bold));
        p->setPen(Qt::white);
        p->drawText(sq, Qt::AlignCenter, square);
        x += 16 + 6;
    } else if (!icon.isEmpty()) {
        QColor ic = idx.data(NavRole::IconColor).value<QColor>();
        if (!ic.isValid()) ic = onAccent ? t.accentText : t.textSecondary;
        else if (onAccent) ic = t.accentText;
        Icons::paint(p, icon, QRectF(x, r.center().y() - 8, 16, 16), ic);
        x += 16 + 6;
    } else if (status == "spin") {
        // spinning ring in the icon slot
        const QRectF rr(x + 1, r.center().y() - 7, 14, 14);
        p->setPen(QPen(onAccent ? t.accentText : t.accent, 1.6, Qt::SolidLine, Qt::RoundCap));
        p->drawArc(rr, int(-m_tree->spinAngle() * 16), 270 * 16);
        x += 16 + 6;
    }
    if (icon.isEmpty() && status == "spin") {}
    // trailing items, right to left
    int rx = right;
    if (status == "running" || status == "booting" || status == "stopped" || status == "error" || status == "stopping") {
        const QColor dc = onAccent ? t.accentText : Ui::statusColor(status, t);
        p->setPen(Qt::NoPen);
        p->setBrush(dc);
        p->drawEllipse(QPointF(rx - 4, r.center().y()), 4, 4);
        rx -= 14;
    }
    const QString trailing = idx.data(NavRole::Trailing).toString();
    if (!trailing.isEmpty()) {
        p->setFont(Theme::instance().ui(11));
        const int w = QFontMetrics(p->font()).horizontalAdvance(trailing);
        p->setPen(fg2);
        p->drawText(QRect(rx - w, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignRight, trailing);
        rx -= w + 8;
    }
    const QString badge = idx.data(NavRole::Badge).toString();
    if (!badge.isEmpty()) {
        p->setFont(Theme::instance().ui(10, QFont::DemiBold));
        const int w = QFontMetrics(p->font()).horizontalAdvance(badge) + 10;
        const QRect br(rx - w, r.center().y() - 7, w, 14);
        p->setPen(Qt::NoPen);
        p->setBrush(onAccent ? Ui::withAlpha(t.accentText, 50) : Ui::withAlpha(t.text, 22));
        p->drawRoundedRect(br, 7, 7);
        p->setPen(fg2);
        p->drawText(br, Qt::AlignCenter, badge);
        rx -= w + 6;
    }
    // title + sub
    const bool bold = idx.data(NavRole::Bold).toBool();
    QFont f = m_tree->compact() ? Theme::instance().mono(11.5, bold ? QFont::Medium : QFont::Normal) : Theme::instance().ui(13, bold ? QFont::DemiBold : QFont::Normal);
    if (m_tree->compact()) f.setPixelSize(12);
    p->setFont(f);
    const QString title = idx.data(Qt::DisplayRole).toString();
    const QString sub = idx.data(NavRole::Sub).toString();
    const int avail = qMax(10, rx - x);
    const QFontMetrics fm(f);
    const QString shown = fm.elidedText(title, Qt::ElideRight, avail);
    // filter match highlight
    const QString q = m_tree->filterText();
    if (!q.isEmpty()) {
        const int at = shown.indexOf(q, 0, Qt::CaseInsensitive);
        if (at >= 0) {
            const int hx = x + fm.horizontalAdvance(shown.left(at));
            p->setPen(Qt::NoPen);
            p->setBrush(Ui::withAlpha(t.warn, onAccent ? 110 : 90));
            p->drawRoundedRect(QRectF(hx - 1, r.center().y() - 9, fm.horizontalAdvance(shown.mid(at, q.size())) + 2, 18), 3, 3);
        }
    }
    p->setPen(fg);
    p->drawText(QRect(x, r.top(), avail, r.height()), Qt::AlignVCenter | Qt::AlignLeft, shown);
    if (!sub.isEmpty()) {
        const int tw = fm.horizontalAdvance(shown) + 6;
        if (tw < avail - 20) {
            QFont sf = idx.data(NavRole::SubMono).toBool() ? Theme::instance().mono(11) : Theme::instance().ui(12);
            p->setFont(sf);
            p->setPen(fg2);
            p->drawText(QRect(x + tw, r.top(), avail - tw, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                        QFontMetrics(sf).elidedText(sub, Qt::ElideRight, avail - tw));
        }
    }
    p->restore();
}

// --- NavTree ------------------------------------------------------------------------------------------

NavTree::NavTree(QWidget *parent) : QTreeView(parent) {
    m_proxy = new NavFilterProxy(this);
    m_model = new QStandardItemModel(this);
    m_proxy->setSourceModel(m_model);
    setModel(m_proxy);
    setItemDelegate(new NavDelegate(this, this));
    setHeaderHidden(true);
    setUniformRowHeights(true);
    setIndentation(14);
    setRootIsDecorated(true);
    setAnimated(false);
    setFrameShape(QFrame::NoFrame);
    setEditTriggers(NoEditTriggers);
    setSelectionMode(SingleSelection);
    setVerticalScrollMode(ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setExpandsOnDoubleClick(true);
    setTextElideMode(Qt::ElideRight);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    setPalette(pal);
    m_empty = new EmptyState(viewport());
    m_empty->hide();
    connect(m_empty, &EmptyState::actionTriggered, this, &NavTree::emptyActionTriggered);
    m_spinTimer.setParent(this);
    m_spinTimer.setInterval(33);
    connect(&m_spinTimer, &QTimer::timeout, this, [this] {
        m_spin = std::fmod(m_spin + 12.0, 360.0);
        viewport()->update();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this] { viewport()->update(); });
    connect(&Theme::instance(), &Theme::motionChanged, this, &NavTree::updateSpin);
    connect(this, &QTreeView::expanded, this, [this](const QModelIndex &i) {
        if (!m_silent) { const QString id = i.data(NavRole::Id).toString(); m_userExpanded.insert(id); m_userCollapsed.remove(id); }
        animateArrow(i, true);
    });
    connect(this, &QTreeView::collapsed, this, [this](const QModelIndex &i) {
        if (!m_silent) { const QString id = i.data(NavRole::Id).toString(); m_userCollapsed.insert(id); m_userExpanded.remove(id); }
        animateArrow(i, false);
    });
    connect(this, &QTreeView::activated, this, [this](const QModelIndex &i) {
        const Location l = locationOf(i);
        if (l.kind != Location::Welcome || !i.data(NavRole::Loc).isValid()) emit locationRequested(l);
    });
    connect(this, &QTreeView::clicked, this, [this](const QModelIndex &i) {
        if (!i.data(NavRole::Loc).isValid()) return;
        emit locationRequested(locationOf(i));
    });
}

Location NavTree::locationOf(const QModelIndex &idx) const {
    const QVariant v = idx.data(NavRole::Loc);
    return v.isValid() ? v.value<Location>() : Location{};
}

int NavTree::visibleRows() const {
    int n = 0;
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &p) {
        for (int i = 0; i < m_proxy->rowCount(p); ++i) {
            ++n;
            const QModelIndex c = m_proxy->index(i, 0, p);
            if (isExpanded(c)) walk(c);
        }
    };
    walk({});
    return n;
}

void NavTree::currentChanged(const QModelIndex &cur, const QModelIndex &prev) {
    QTreeView::currentChanged(cur, prev);
    // Arrow key navigation opens the item like a click does (mouse clicks are reported by clicked()).
    if (!m_silent && cur.isValid() && !m_mouseSelect && cur.data(NavRole::Loc).isValid()) emit locationRequested(locationOf(cur));
}

void NavTree::mousePressEvent(QMouseEvent *e) {
    m_mouseSelect = true;
    QTreeView::mousePressEvent(e);
    m_mouseSelect = false;
}

QString NavTree::signatureOf(const QStandardItemModel &m) const {
    QString sig;
    std::function<void(const QStandardItem *, int)> walk = [&](const QStandardItem *it, int depth) {
        for (int r = 0; r < it->rowCount(); ++r) {
            const QStandardItem *c = it->child(r);
            sig += QString::number(depth) + '|' + c->text() + '|' + c->data(NavRole::Id).toString() + '|' + c->data(NavRole::Status).toString() + '|' +
                   c->data(NavRole::Trailing).toString() + '|' + c->data(NavRole::Sub).toString() + '|' + c->data(NavRole::Badge).toString() + '|' +
                   c->data(NavRole::Icon).toString() + '|' + c->data(NavRole::IconColor).value<QColor>().name() + '\n';
            walk(c, depth + 1);
        }
    };
    walk(m.invisibleRootItem(), 0);
    return sig;
}

void NavTree::collectExpanded(const QModelIndex &parent, QSet<QString> &out) const {
    for (int i = 0; i < m_proxy->rowCount(parent); ++i) {
        const QModelIndex c = m_proxy->index(i, 0, parent);
        if (m_proxy->rowCount(c) > 0) {
            if (isExpanded(c)) out.insert(c.data(NavRole::Id).toString());
            collectExpanded(c, out);
        }
    }
}

void NavTree::applyExpansion(const QModelIndex &parent, const QSet<QString> &expanded) {
    Q_UNUSED(expanded);
    for (int i = 0; i < m_proxy->rowCount(parent); ++i) {
        const QModelIndex c = m_proxy->index(i, 0, parent);
        const QString id = c.data(NavRole::Id).toString();
        if (m_proxy->rowCount(c) > 0) {
            const bool open = m_userExpanded.contains(id) || (!m_userCollapsed.contains(id) && c.data(NavRole::Expand).toBool());
            if (open) setExpanded(c, true);
            applyExpansion(c, {});
        }
    }
}

void NavTree::rebuild(const std::function<void(QStandardItemModel &)> &fill) {
    auto *fresh = new QStandardItemModel(this);
    fill(*fresh);
    const QString sig = signatureOf(*fresh);
    if (sig == m_sig && m_model->rowCount() == fresh->rowCount()) {
        delete fresh;
        return;
    }
    m_sig = sig;
    const QString selected = currentId();
    QSet<QString> expanded;
    collectExpanded({}, expanded);
    const int scroll = verticalScrollBar()->value();
    m_silent = true;
    m_proxy->setSourceModel(fresh);
    delete m_model;
    m_model = fresh;
    applyExpansion({}, expanded);
    if (!m_proxy->text().isEmpty()) expandAll();
    if (!selected.isEmpty()) selectId(selected);
    verticalScrollBar()->setValue(scroll);
    m_silent = false;
    m_empty->setVisible(m_model->rowCount() == 0);
    m_empty->setGeometry(viewport()->rect());
    updateSpin();
    viewport()->update();
}

void NavTree::setFilterText(const QString &t) {
    m_proxy->setText(t);
    if (!t.trimmed().isEmpty()) {
        const bool was = m_silent;
        m_silent = true;
        expandAll();
        m_silent = was;
    }
    m_empty->setVisible(m_proxy->rowCount() == 0 && m_model->rowCount() > 0 ? false : m_model->rowCount() == 0);
    viewport()->update();
}

void NavTree::setEmpty(const QString &icon, const QString &title, const QString &body, const QString &action) {
    m_empty->setContent(icon, title, body, action);
    m_empty->setVisible(m_model->rowCount() == 0);
    m_empty->setGeometry(viewport()->rect());
}

QString NavTree::currentId() const { return currentIndex().isValid() ? currentIndex().data(NavRole::Id).toString() : QString(); }

QModelIndex NavTree::findId(const QString &id) const {
    std::function<QModelIndex(const QModelIndex &)> walk = [&](const QModelIndex &p) -> QModelIndex {
        for (int i = 0; i < m_proxy->rowCount(p); ++i) {
            const QModelIndex c = m_proxy->index(i, 0, p);
            if (c.data(NavRole::Id).toString() == id) return c;
            const QModelIndex f = walk(c);
            if (f.isValid()) return f;
        }
        return {};
    };
    return walk({});
}

bool NavTree::selectId(const QString &id) {
    const QModelIndex idx = findId(id);
    if (!idx.isValid()) return false;
    const bool was = m_silent;
    m_silent = true;
    for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent()) setExpanded(p, true);
    setCurrentIndex(idx);
    scrollTo(idx);
    m_silent = was;
    return true;
}

void NavTree::clearSelectionSilently() {
    const bool was = m_silent;
    m_silent = true;
    clearSelection();
    setCurrentIndex(QModelIndex());
    m_silent = was;
}

void NavTree::drawRow(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    const bool selected = selectionModel()->isSelected(idx);
    if (selected) {
        const Tokens &t = tk();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(hasFocus() ? t.selectionFocused : t.selection);
        p->drawRoundedRect(QRectF(6, opt.rect.top() + 0.5, viewport()->width() - 12, opt.rect.height() - 1), 6, 6);
        p->restore();
    }
    QStyleOptionViewItem o = opt;
    o.state &= ~QStyle::State_Selected;
    o.state &= ~QStyle::State_MouseOver;
    QTreeView::drawRow(p, o, idx);
}

void NavTree::drawBranches(QPainter *p, const QRect &rect, const QModelIndex &idx) const {
    if (m_proxy->rowCount(idx) == 0) return;
    const Tokens &t = tk();
    const bool sel = selectionModel()->isSelected(idx);
    const QColor c = (sel && hasFocus()) ? t.accentText : t.textSecondary;
    const int cell = 14;
    const QString id = idx.data(NavRole::Id).toString();
    const qreal open = m_arrow.contains(id) ? m_arrow.value(id) : (isExpanded(idx) ? 1.0 : 0.0);
    p->save();
    p->translate(rect.right() - cell + 1 + 5, rect.center().y());
    p->rotate(open * 90.0);       // right-pointing chevron rotates down
    Icons::paint(p, "chevron.right", QRectF(-6, -6, 12, 12), c);
    p->restore();
}

void NavTree::animateArrow(const QModelIndex &idx, bool open) {
    const QString id = idx.data(NavRole::Id).toString();
    if (id.isEmpty()) return;
    if (m_silent || !isVisible() || !Ui::motionAllowed()) { m_arrow.remove(id); return; }
    const qreal from = m_arrow.contains(id) ? m_arrow.value(id) : (open ? 0.0 : 1.0);
    m_arrow[id] = from;
    Ui::animate(this, from, open ? 1.0 : 0.0, 130, [this, id](qreal v) { m_arrow[id] = v; viewport()->update(); },
                QEasingCurve::OutCubic, [this, id] { m_arrow.remove(id); viewport()->update(); });
}

void NavTree::setCompact(bool on) {
    m_compact = on;
    doItemsLayout();
    viewport()->update();
}

void NavTree::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && currentIndex().isValid()) {
        emit previewRequested(locationOf(currentIndex()));
        return;
    }
    QTreeView::keyPressEvent(e);
}

void NavTree::resizeEvent(QResizeEvent *e) {
    QTreeView::resizeEvent(e);
    m_empty->setGeometry(viewport()->rect());
}

void NavTree::contextMenuEvent(QContextMenuEvent *e) {
    const QModelIndex i = indexAt(e->pos());
    if (!i.isValid()) return;
    setCurrentIndex(i);
    if (m_proxy->rowCount(i) > 0) {
        // every group offers Expand / Collapse / Expand All / Collapse All
        GlassMenu menu(this);
        QAction *ex = menu.addAction("Expand");
        ex->setEnabled(!isExpanded(i));
        connect(ex, &QAction::triggered, this, [this, i] { expand(i); });
        QAction *co = menu.addAction("Collapse");
        co->setEnabled(isExpanded(i));
        connect(co, &QAction::triggered, this, [this, i] { collapse(i); });
        menu.addSeparator();
        connect(menu.addAction("Expand All"), &QAction::triggered, this, [this] { expandAll(); });
        connect(menu.addAction("Collapse All"), &QAction::triggered, this, [this] { collapseAll(); });
        menu.exec(e->globalPos());
        return;
    }
    emit contextRequested(i.data(NavRole::Id).toString(), e->globalPos());
}

bool NavTree::hasSpinning() const {
    for (int r = 0; r < m_model->rowCount(); ++r) {
        std::function<bool(const QStandardItem *)> walk = [&](const QStandardItem *it) {
            if (it->data(NavRole::Status).toString() == "spin") return true;
            for (int k = 0; k < it->rowCount(); ++k)
                if (walk(it->child(k))) return true;
            return false;
        };
        if (walk(m_model->item(r))) return true;
    }
    return false;
}

void NavTree::updateSpin() {
    const bool need = isVisible() && Ui::motionAllowed() && hasSpinning();
    if (need && !m_spinTimer.isActive()) m_spinTimer.start();
    else if (!need && m_spinTimer.isActive()) m_spinTimer.stop();
}

void NavTree::showEvent(QShowEvent *e) {
    QTreeView::showEvent(e);
    updateSpin();
}
void NavTree::hideEvent(QHideEvent *e) {
    QTreeView::hideEvent(e);
    updateSpin();
}

void NavTree::paintEvent(QPaintEvent *e) {
    QTreeView::paintEvent(e);
    if (m_skeleton) {
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Ui::withAlpha(tk().text, 14));
        for (int i = 0; i < 3; ++i) p.drawRoundedRect(QRectF(16 + (i % 2) * 14, 6 + i * Metrics::navRow + 4, 120 + (i * 37) % 60, 10), 5, 5);
    }
}

// --- NavTabBar ----------------------------------------------------------------------------------------

NavTabBar::NavTabBar(QWidget *parent) : GlassPanel(parent, Glass::Kind::Field) {
    setShape(Shape::Capsule);
    setShadowMargin(3);
    setObjectName("navtabs");
    setAccessibleName("Navigator tabs");
}

void NavTabBar::setTabs(const QVector<Tab> &tabs) {
    qDeleteAll(m_buttons);
    m_buttons.clear();
    m_tabs = tabs;
    for (int i = 0; i < tabs.size(); ++i) {
        auto *b = new IconButton(tabs[i].icon, this);
        b->setFixedButtonSize(30, 28);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCircular(true);
        b->setHoverEnabled(false);
        b->setCheckedTint(tk().accentText);
        b->setTint(Ui::withAlpha(tk().text, 200));
        b->setGlyphSize(17);
        Ui::setTip(b, tabs[i].name, tabs[i].shortcut);
        b->setAccessibleName(tabs[i].name);
        b->installEventFilter(this);
        b->show();
        connect(b, &QAbstractButton::clicked, this, [this, i] { setCurrent(i, true); });
        m_buttons << b;
    }
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        for (auto *b : std::as_const(m_buttons)) { b->setCheckedTint(tk().accentText); b->setTint(Ui::withAlpha(tk().text, 200)); }
    });
    for (int i = 0; i + 1 < m_buttons.size(); ++i) setTabOrder(m_buttons[i], m_buttons[i + 1]);
    setCurrent(m_current, false, false);
    relayout();
}

QPointF NavTabBar::centerOf(int i) const {
    const QRect sr = shapeRect();
    const int n = qMax(1, m_buttons.size());
    const int pad = 6 + 15;
    const qreal span = qMax(1, sr.width() - 2 * pad);
    const qreal x = n == 1 ? sr.center().x() : sr.left() + pad + span * i / (n - 1);
    return QPointF(x, sr.center().y());
}

void NavTabBar::relayout() {
    for (int i = 0; i < m_buttons.size(); ++i) {
        const QPointF c = centerOf(i);
        m_buttons[i]->move(int(c.x() - m_buttons[i]->width() / 2.0 + 0.5), int(c.y() - m_buttons[i]->height() / 2.0 + 0.5));
    }
    if (m_circleX < 0 || !m_anim) m_circleX = centerOf(m_current).x();
    update();
}

void NavTabBar::resizeEvent(QResizeEvent *e) {
    GlassPanel::resizeEvent(e);
    relayout();
}

void NavTabBar::setCurrent(int i, bool emitSignal, bool animate) {
    if (m_buttons.isEmpty()) { m_current = i; return; }
    i = qBound(0, i, m_buttons.size() - 1);
    const bool changed = i != m_current;
    m_current = i;
    for (int k = 0; k < m_buttons.size(); ++k) {
        QSignalBlocker b(m_buttons[k]);
        m_buttons[k]->setChecked(k == i);
    }
    const qreal target = centerOf(i).x();
    if (m_anim) { m_anim->stop(); m_anim->deleteLater(); m_anim = nullptr; }
    if (animate && changed && isVisible() && Ui::motionAllowed()) {
        m_anim = Ui::animate(this, m_circleX < 0 ? target : m_circleX, target, 160, [this](qreal v) { m_circleX = v; update(); },
                             QEasingCurve::OutCubic, [this, target] { m_anim = nullptr; m_circleX = target; update(); });
    } else {
        m_circleX = target;
        update();
    }
    if (changed && emitSignal) emit currentChanged(i);
}

bool NavTabBar::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const int idx = m_buttons.indexOf(qobject_cast<IconButton *>(o));
        if (idx >= 0 && (ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right)) {
            const int n = qBound(0, idx + (ke->key() == Qt::Key_Left ? -1 : 1), m_buttons.size() - 1);
            m_buttons[n]->setFocus(Qt::TabFocusReason);
            setCurrent(n, true);
            return true;
        }
    }
    return GlassPanel::eventFilter(o, e);
}

void NavTabBar::setBadge(int tab, int count, const QColor &color) {
    if (count <= 0) m_badges.remove(tab);
    else m_badges.insert(tab, {count, color});
    update();
}

void NavTabBar::paintContent(QPainter &p, const QRect &shape) {
    if (m_buttons.isEmpty()) return;
    const qreal dpr = devicePixelRatioF();
    const int d = 28;
    static QImage cache;
    static quint64 cacheKey = 0;
    const quint64 key = quint64(d) * 1000003ull + (tk().dark ? 7 : 3) + quint64(dpr * 100) * 131 + tk().accent.rgb();
    if (key != cacheKey || cache.isNull()) {
        const Glass::Material m = Glass::Material::forKind(Glass::Kind::Accent, tk());
        cache = Glass::renderMaterial(QImage(), QPoint(), int(d * dpr), int(d * dpr), dpr, d * dpr / 2.0, m,
                                      Glass::level() == Glass::Level::Off ? Glass::Level::Off : Glass::Level::Blur);
        cacheKey = key;
    }
    p.drawImage(QPointF(m_circleX - d / 2.0, shape.center().y() - d / 2.0 + 0.5), cache);
    // count badges sit above the buttons, painted by the bar after its own content but behind nothing
    for (auto it = m_badges.constBegin(); it != m_badges.constEnd(); ++it) {
        if (it.key() < 0 || it.key() >= m_buttons.size()) continue;
        const QRect b = m_buttons[it.key()]->geometry();
        const QString txt = it.value().first > 99 ? "99+" : QString::number(it.value().first);
        p.setFont(Theme::instance().ui(9, QFont::Bold));
        const int w = qMax(13, QFontMetrics(p.font()).horizontalAdvance(txt) + 7);
        const QRect br(b.right() - w + 4, b.top() - 2, w, 13);
        p.setPen(Qt::NoPen);
        p.setBrush(it.value().second);
        p.drawRoundedRect(br, 6.5, 6.5);
        p.setPen(Qt::white);
        p.drawText(br, Qt::AlignCenter, txt);
    }
}

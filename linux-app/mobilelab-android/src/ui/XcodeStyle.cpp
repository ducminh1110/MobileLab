#include "XcodeStyle.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHintReturnMask>
#include <QStyleOption>
#include "Icons.h"
#include "Theme.h"
#include "UiUtil.h"

XcodeStyle::XcodeStyle() : QProxyStyle("Fusion") {}

void XcodeStyle::polish(QWidget *w) {
    QProxyStyle::polish(w);
    if (qobject_cast<QAbstractScrollArea *>(w)) w->setAttribute(Qt::WA_Hover, true);
    if (qobject_cast<QMenuBar *>(w)) {
        w->setAutoFillBackground(false);
        w->setAttribute(Qt::WA_NoSystemBackground, true);
        w->setFont(Theme::instance().ui(13));
    }
    if (qobject_cast<QMenu *>(w)) w->setFont(Theme::instance().ui(13));
}

int XcodeStyle::pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const {
    switch (m) {
    case PM_ScrollBarExtent: return 12;
    case PM_ScrollBarSliderMin: return 32;
    case PM_MenuHMargin: return 6;
    case PM_MenuVMargin: return 6;
    case PM_MenuPanelWidth: return 0;
    case PM_MenuBarPanelWidth: return 0;
    case PM_MenuBarItemSpacing: return 1;
    case PM_MenuBarHMargin: return 8;
    case PM_MenuBarVMargin: return 1;
    case PM_SmallIconSize: return 16;
    case PM_ToolTipLabelFrameWidth: return 6;
    case PM_DefaultFrameWidth: return 1;
    case PM_IndicatorWidth: case PM_IndicatorHeight: case PM_ExclusiveIndicatorWidth: case PM_ExclusiveIndicatorHeight: return 16;
    case PM_ButtonMargin: return 8;
    case PM_FocusFrameHMargin: case PM_FocusFrameVMargin: return 2;
    default: return QProxyStyle::pixelMetric(m, o, w);
    }
}

int XcodeStyle::styleHint(StyleHint h, const QStyleOption *o, const QWidget *w, QStyleHintReturn *r) const {
    switch (h) {
    case SH_Menu_SubMenuPopupDelay: return 150;
    case SH_Menu_Scrollable: return 1;
    case SH_ScrollBar_Transient: return 0;
    case SH_ToolTip_WakeUpDelay: return 500;
    case SH_ToolTip_Mask:
        if (auto *mask = qstyleoption_cast<QStyleHintReturnMask *>(r)) {
            QPainterPath path;
            path.addRoundedRect(QRectF(o->rect), 7, 7);
            mask->region = QRegion(path.toFillPolygon().toPolygon());
            return 1;
        }
        return 0;
    case SH_ComboBox_Popup: return 1;
    case SH_ItemView_ActivateItemOnSingleClick: return 0;
    case SH_TabBar_Alignment: return Qt::AlignCenter;
    default: return QProxyStyle::styleHint(h, o, w, r);
    }
}

QRect XcodeStyle::subElementRect(SubElement e, const QStyleOption *o, const QWidget *w) const {
    if (e == SE_ItemViewItemFocusRect) return o->rect;
    return QProxyStyle::subElementRect(e, o, w);
}

void XcodeStyle::drawPrimitive(PrimitiveElement e, const QStyleOption *o, QPainter *p, const QWidget *w) const {
    const Tokens &t = tk();
    switch (e) {
    case PE_FrameFocusRect:
        return;   // widgets draw their own rings
    case PE_PanelMenu:
        if (w && w->property("glassMenu").toBool()) return;   // GlassMenu paints the material itself
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(t.panelBorder, 1));
        p->setBrush(t.capsule);
        p->drawRoundedRect(QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        p->restore();
        return;
    case PE_FrameMenu: return;
    case PE_PanelMenuBar: return;
    case PE_PanelTipLabel:
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(t.dark ? Ui::withAlpha(Qt::white, 40) : Ui::withAlpha(Qt::black, 36), 1));
        p->setBrush(t.dark ? QColor("#3b3b3f") : QColor("#fbfbfc"));
        p->drawRoundedRect(QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        p->restore();
        return;
    case PE_PanelLineEdit:
        if (w && w->property("bare").toBool()) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(t.field);
        p->drawRoundedRect(QRectF(o->rect), 7, 7);
        if (o->state & State_HasFocus) {
            p->setPen(QPen(Ui::withAlpha(t.accent, 170), 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(o->rect).adjusted(1, 1, -1, -1), 6, 6);
        }
        p->restore();
        return;
    case PE_FrameLineEdit:
        if (w && w->property("bare").toBool()) return;
        return;
    case PE_IndicatorCheckBox: {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(o->rect).adjusted(1, 1, -1, -1);
        const bool on = o->state & State_On, mid = o->state & State_NoChange;
        p->setPen(on || mid ? Qt::NoPen : QPen(Ui::withAlpha(t.text, 70), 1));
        p->setBrush(on || mid ? (o->state & State_Enabled ? t.accent : t.textTertiary) : t.editor);
        p->drawRoundedRect(r, 4.5, 4.5);
        if (on) Icons::paint(p, "checkmark", r.adjusted(2, 2, -2, -2), Qt::white);
        if (mid) { p->setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap)); p->drawLine(QPointF(r.left() + 4, r.center().y()), QPointF(r.right() - 4, r.center().y())); }
        if (o->state & State_HasFocus) { p->setPen(QPen(Ui::withAlpha(t.accent, 170), 2)); p->setBrush(Qt::NoBrush); p->drawRoundedRect(r.adjusted(-1, -1, 1, 1), 5.5, 5.5); }
        p->restore();
        return;
    }
    case PE_IndicatorRadioButton: {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(o->rect).adjusted(1, 1, -1, -1);
        const bool on = o->state & State_On;
        p->setPen(on ? Qt::NoPen : QPen(Ui::withAlpha(t.text, 70), 1));
        p->setBrush(on ? t.accent : t.editor);
        p->drawEllipse(r);
        if (on) { p->setPen(Qt::NoPen); p->setBrush(Qt::white); p->drawEllipse(r.center(), 3.0, 3.0); }
        p->restore();
        return;
    }
    case PE_IndicatorBranch: return;
    default: break;
    }
    QProxyStyle::drawPrimitive(e, o, p, w);
}

void XcodeStyle::drawControl(ControlElement e, const QStyleOption *o, QPainter *p, const QWidget *w) const {
    const Tokens &t = tk();
    switch (e) {
    case CE_PushButtonBevel: {
        const auto *b = qstyleoption_cast<const QStyleOptionButton *>(o);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool def = b && (b->features & QStyleOptionButton::DefaultButton);
        const bool down = o->state & State_Sunken, hover = o->state & State_MouseOver, en = o->state & State_Enabled;
        QColor bg = def ? t.accent : t.field;
        if (down) bg = def ? t.accent.darker(125) : Ui::mix(t.field, t.text, 0.14);
        else if (hover) bg = def ? t.accent.lighter(110) : Ui::mix(t.field, t.text, 0.07);
        if (!en) bg = Ui::withAlpha(t.field, 150);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->drawRoundedRect(r, r.height() / 2, r.height() / 2);
        if (o->state & State_HasFocus) { p->setPen(QPen(Ui::withAlpha(t.accent, 170), 2)); p->setBrush(Qt::NoBrush); p->drawRoundedRect(r.adjusted(-1, -1, 1, 1), r.height() / 2 + 1, r.height() / 2 + 1); }
        p->restore();
        return;
    }
    case CE_PushButtonLabel: {
        QStyleOptionButton copy = *qstyleoption_cast<const QStyleOptionButton *>(o);
        const bool def = copy.features & QStyleOptionButton::DefaultButton;
        copy.palette.setColor(QPalette::ButtonText, !(o->state & State_Enabled) ? t.textTertiary : def ? t.accentText : t.text);
        QProxyStyle::drawControl(e, &copy, p, w);
        return;
    }
    case CE_MenuBarItem: {
        const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(o);
        if (!mi) break;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (o->state & (State_Selected | State_Sunken)) {
            p->setPen(Qt::NoPen);
            p->setBrush(t.dark ? Ui::withAlpha(Qt::white, 34) : Ui::withAlpha(Qt::black, 26));
            p->drawRoundedRect(QRectF(o->rect).adjusted(0, 1, 0, -1), 6, 6);
        }
        p->setFont(Theme::instance().ui(13, QFont::Medium));
        p->setPen(o->state & State_Enabled ? t.text : t.textTertiary);
        p->drawText(o->rect, Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextHideMnemonic, mi->text);
        p->restore();
        return;
    }
    case CE_MenuBarEmptyArea: return;
    case CE_MenuEmptyArea: return;
    case CE_MenuItem: {
        const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(o);
        if (!mi) break;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = o->rect;
        if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
            p->setPen(t.dark ? Ui::withAlpha(Qt::white, 34) : Ui::withAlpha(Qt::black, 28));
            p->drawLine(r.left() + 10, r.center().y(), r.right() - 10, r.center().y());
            p->restore();
            return;
        }
        const bool sel = (o->state & State_Selected) && (o->state & State_Enabled);
        const QRectF row = QRectF(r).adjusted(4, 1, -4, -1);
        if (sel) {
            p->setPen(Qt::NoPen);
            p->setBrush(t.accent);
            p->drawRoundedRect(row, 6, 6);
        }
        const QColor fg = !(o->state & State_Enabled) ? t.textTertiary : sel ? t.accentText : t.text;
        const QColor fg2 = !(o->state & State_Enabled) ? t.textTertiary : sel ? Ui::withAlpha(t.accentText, 200) : t.textSecondary;
        // check column
        if (mi->checked && mi->menuItemType != QStyleOptionMenuItem::SubMenu) Icons::paint(p, "checkmark", QRectF(r.left() + 10, r.center().y() - 6, 12, 12), fg);
        int x = r.left() + 28;
        if (!mi->icon.isNull()) {
            const QPixmap pm = mi->icon.pixmap(16, 16);
            p->drawPixmap(QRect(x, r.center().y() - 8, 16, 16), pm);
            x += 22;
        }
        const QString text = mi->text;
        const int tab = text.indexOf('\t');
        const QString label = tab >= 0 ? text.left(tab) : text;
        const QString shortcut = tab >= 0 ? text.mid(tab + 1) : QString();
        p->setFont(Theme::instance().ui(13));
        p->setPen(fg);
        p->drawText(QRect(x, r.top(), r.right() - x - 12, r.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextShowMnemonic | Qt::TextHideMnemonic, label);
        if (mi->menuItemType == QStyleOptionMenuItem::SubMenu) {
            Icons::paint(p, "chevron.right", QRectF(r.right() - 22, r.center().y() - 5, 10, 10), fg2);
        } else if (!shortcut.isEmpty()) {
            p->setFont(Theme::instance().ui(12));
            p->setPen(fg2);
            p->drawText(QRect(r.left(), r.top(), r.width() - 16, r.height()), Qt::AlignVCenter | Qt::AlignRight, shortcut);
        }
        p->restore();
        return;
    }
    case CE_ShapedFrame:
        if (const auto *f = qstyleoption_cast<const QStyleOptionFrame *>(o)) {
            if (f->frameShape == QFrame::HLine || f->frameShape == QFrame::VLine) {
                p->save();
                p->setPen(t.divider);
                if (f->frameShape == QFrame::HLine) p->drawLine(o->rect.left(), o->rect.center().y(), o->rect.right(), o->rect.center().y());
                else p->drawLine(o->rect.center().x(), o->rect.top(), o->rect.center().x(), o->rect.bottom());
                p->restore();
                return;
            }
            if (f->frameShape == QFrame::NoFrame) return;
        }
        break;
    default: break;
    }
    QProxyStyle::drawControl(e, o, p, w);
}

QRect XcodeStyle::subControlRect(ComplexControl c, const QStyleOptionComplex *o, SubControl s, const QWidget *w) const {
    if (c == CC_ScrollBar) {
        const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(o);
        if (!sb) return QProxyStyle::subControlRect(c, o, s, w);
        const bool horizontal = sb->orientation == Qt::Horizontal;
        const QRect groove = sb->rect;
        if (s == SC_ScrollBarAddLine || s == SC_ScrollBarSubLine) return QRect();
        if (s == SC_ScrollBarGroove || s == SC_ScrollBarAddPage || s == SC_ScrollBarSubPage || s == SC_ScrollBarFirst || s == SC_ScrollBarLast) {
            if (s == SC_ScrollBarGroove) return groove;
        }
        const int maxlen = horizontal ? groove.width() : groove.height();
        const int range = sb->maximum - sb->minimum;
        int sliderlen = maxlen;
        if (range > 0) sliderlen = qMax(pixelMetric(PM_ScrollBarSliderMin, o, w), int((qint64(sb->pageStep) * maxlen) / (range + sb->pageStep)));
        sliderlen = qMin(sliderlen, maxlen);
        const int pos = sliderPositionFromValue(sb->minimum, sb->maximum, sb->sliderPosition, maxlen - sliderlen, sb->upsideDown);
        const QRect slider = horizontal ? QRect(groove.x() + pos, groove.y(), sliderlen, groove.height()) : QRect(groove.x(), groove.y() + pos, groove.width(), sliderlen);
        if (s == SC_ScrollBarSlider) return slider;
        if (s == SC_ScrollBarSubPage) return horizontal ? QRect(groove.x(), groove.y(), pos, groove.height()) : QRect(groove.x(), groove.y(), groove.width(), pos);
        if (s == SC_ScrollBarAddPage) return horizontal ? QRect(slider.right() + 1, groove.y(), groove.right() - slider.right(), groove.height()) : QRect(groove.x(), slider.bottom() + 1, groove.width(), groove.bottom() - slider.bottom());
        return QRect();
    }
    return QProxyStyle::subControlRect(c, o, s, w);
}

void XcodeStyle::drawComplexControl(ComplexControl c, const QStyleOptionComplex *o, QPainter *p, const QWidget *w) const {
    if (c == CC_ScrollBar) {
        const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(o);
        if (!sb) return;
        if (sb->maximum <= sb->minimum) return;   // nothing to scroll: no bar at all
        const Tokens &t = tk();
        const QRect slider = subControlRect(c, o, SC_ScrollBarSlider, w);
        const bool horizontal = sb->orientation == Qt::Horizontal;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool active = (o->state & State_MouseOver) || (o->state & State_Sunken);
        const int thick = active ? 8 : 5;
        QRectF r;
        if (horizontal) r = QRectF(slider.left() + 1, slider.center().y() - thick / 2.0, slider.width() - 2, thick);
        else r = QRectF(slider.center().x() - thick / 2.0, slider.top() + 1, thick, slider.height() - 2);
        QColor c2 = t.dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
        c2.setAlpha((o->state & State_Sunken) ? 150 : active ? 120 : 70);
        p->setPen(Qt::NoPen);
        p->setBrush(c2);
        p->drawRoundedRect(r, thick / 2.0, thick / 2.0);
        p->restore();
        return;
    }
    QProxyStyle::drawComplexControl(c, o, p, w);
}

QSize XcodeStyle::sizeFromContents(ContentsType type, const QStyleOption *o, const QSize &s, const QWidget *w) const {
    if (type == CT_MenuItem) {
        const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(o);
        if (mi) {
            if (mi->menuItemType == QStyleOptionMenuItem::Separator) return QSize(120, 9);
            const QString text = mi->text;
            const int tab = text.indexOf('\t');
            const QFontMetrics fm(Theme::instance().ui(13)), fs(Theme::instance().ui(12));
            int wdt = 28 + fm.horizontalAdvance(tab >= 0 ? text.left(tab) : text) + 24;
            if (tab >= 0) wdt += 24 + fs.horizontalAdvance(text.mid(tab + 1));
            if (!mi->icon.isNull()) wdt += 22;
            if (mi->menuItemType == QStyleOptionMenuItem::SubMenu) wdt += 14;
            return QSize(qMax(wdt, 180), 24);
        }
    }
    if (type == CT_MenuBarItem) {
        const QFontMetrics fm(Theme::instance().ui(13, QFont::Medium));
        const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(o);
        QString text = mi ? mi->text : QString();
        text.remove('&');
        return QSize(fm.horizontalAdvance(text) + 18, 24);
    }
    if (type == CT_PushButton) {
        QSize sz = QProxyStyle::sizeFromContents(type, o, s, w);
        return QSize(sz.width() + 8, qMax(26, sz.height()));
    }
    return QProxyStyle::sizeFromContents(type, o, s, w);
}

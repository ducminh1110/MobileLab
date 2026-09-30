#pragma once
// Panel (floating rounded surface) and PaneHost (a centre widget with collapsible side panes, resizable by
// drag handles, with animated collapse / expand, snap-collapse past the minimum and double-click toggle).
#include <QFocusEvent>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>
#include "Theme.h"

class Panel : public QWidget {
    Q_OBJECT
public:
    enum class Role { Sidebar, Editor };
    explicit Panel(Role r, QWidget *parent = nullptr);
    void setRole(Role r) { m_role = r; update(); }
    QColor fillColor() const;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    Role m_role;
};

class PaneHost;

class PaneHandle : public QWidget {
    Q_OBJECT
public:
    PaneHandle(PaneHost *host, int id, QWidget *parent);
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
private:
    PaneHost *m_host;
    int m_id;
    bool m_hover = false, m_drag = false, m_kbFocus = false;
    QPoint m_press;
    int m_startSize = 0;
};

class PaneHost : public QWidget {
    Q_OBJECT
public:
    enum class Orientation { Horizontal, Vertical };
    enum class Side { Leading, Trailing };
    struct Spec {
        int minSize = 200;
        int maxSize = 600;
        int defaultSize = 300;
        int collapsedSize = 0;   // size while collapsed (the debug area keeps its 28px bar)
        int snap = 56;           // dragging this far below minSize snaps the pane closed
        QString name;
    };

    PaneHost(Orientation o, QWidget *parent = nullptr);
    // Floating mode: outer margin + gap between panels and soft shadows (window level).
    // Divider mode: no margin, a 1px divider line in the gap (inside a card).
    void setFloating(bool floating, int margin, int gap);
    void setCenter(QWidget *w, int minSize = 320);
    int addPane(Side side, QWidget *w, const Spec &spec);
    void setShown(int id, bool shown, bool animate = true);
    bool isShown(int id) const { return m_panes[id].shown; }
    bool isMoving(int id) const { return m_panes[id].anim && m_panes[id].anim->state() == QAbstractAnimation::Running; }
    void toggle(int id) { setShown(id, !isShown(id)); }
    int paneSize(int id) const { return m_panes[id].size; }
    void setPaneSize(int id, int px);
    // Effective (animated) size in pixels right now, 0 when fully collapsed.
    int currentSize(int id) const;
    QWidget *pane(int id) const { return m_panes[id].w; }
    const Spec &spec(int id) const { return m_panes[id].spec; }
    // Called by handles.
    void dragPane(int id, int rawSize);
    void endDrag(int id);

signals:
    void shownChanged(int id, bool shown);
    void sizeChanged(int id, int px);

protected:
    void resizeEvent(QResizeEvent *) override { layoutPanes(); }
    void paintEvent(QPaintEvent *) override;

private:
    struct PaneState {
        QWidget *w = nullptr;
        PaneHandle *handle = nullptr;
        Spec spec;
        Side side = Side::Leading;
        bool shown = true;
        int size = 300;
        qreal t = 1.0;  // 0 collapsed .. 1 fully shown
        QVariantAnimation *anim = nullptr;
    };
    void layoutPanes();
    void animateTo(int id, qreal target, bool animate);
    Orientation m_o;
    bool m_floating = true;
    int m_margin = 8, m_gap = 8, m_centerMin = 320;
    QWidget *m_center = nullptr;
    QVector<PaneState> m_panes;
};

#pragma once
// Editor jump bar: grid glyph, back / forward, breadcrumbs (each with a sibling popup), trailing controls.
#include <QVector>
#include <QWidget>
#include "GlassGroup.h"
#include "Location.h"

class JumpBar : public QWidget {
    Q_OBJECT
public:
    struct Sibling { QString text; QString icon; Location loc; bool current = false; };
    struct Crumb {
        QString text;
        QString icon;
        QColor iconColor;
        Location loc;                 // where clicking the crumb goes
        QVector<Sibling> siblings;    // the popup
    };
    JumpBar(QAction *back, QAction *forward, QAction *related, QAction *options, QAction *add, QWidget *parent = nullptr);
    void setCrumbs(const QVector<Crumb> &c);
    const QVector<Crumb> &crumbs() const { return m_crumbs; }
    QSize sizeHint() const override { return QSize(400, Metrics::jumpBarHeight); }
    GlassGroup *navGroup() const { return m_nav; }
    GlassGroup *trailing() const { return m_trail; }
    IconButton *gridButton() const { return m_grid; }
signals:
    void locationRequested(const Location &loc);
    void gridRequested(const QPoint &globalBelow);
protected:
    void resizeEvent(QResizeEvent *) override { relayout(); }
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override { m_hover = -1; update(); }
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override { QWidget::focusOutEvent(e); update(); }
private:
    struct Slot { QRect rect; int crumb; };
    void relayout();
    QVector<Slot> layoutCrumbs() const;
    int crumbAt(const QPoint &p) const;
    void openCrumb(int i);
    IconButton *m_grid;
    GlassGroup *m_nav, *m_trail;
    QVector<Crumb> m_crumbs;
    int m_hover = -1, m_pressed = -1, m_focus = -1;
    bool m_kbFocus = false;
    int m_left = 0, m_right = 0;
};

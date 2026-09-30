#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include "glass/Glass.h"

// The toolbar capsule: scheme > destination on the left, `State | detail` on the right.
class Capsule : public GlassPanel {
    Q_OBJECT
public:
    enum Region { None = -1, Scheme = 0, Destination = 1, Status = 2 };
    enum class Tone { Neutral, Pass, Fail, Warn };
    struct State {
        QString scheme = "Android Matrix";
        QString destination;
        QString destinationIcon = "iphone";
        QString state;
        QString detail;
        Tone tone = Tone::Neutral;
        bool spinning = false;
    };
    explicit Capsule(QWidget *parent = nullptr);
    void setState(const State &s);
    const State &state() const { return m_state; }
    QSize sizeHint() const override { return QSize(560, 40); }
    QSize minimumSizeHint() const override { return QSize(300, 40); }
    QRect regionRect(Region r) const;   // in widget coordinates
    void paintContent(QPainter &p, const QRect &shape) override;
    QString accessibleSummary() const;

signals:
    void schemeRequested(const QPoint &globalBelow);
    void destinationRequested(const QPoint &globalBelow);
    void statusRequested();

protected:
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    struct Layout { QRect scheme, destination, status; QString schemeText, destText, stateText, detailText; int stateW = 0; };
    Layout computeLayout(const QRect &shape) const;
    Region regionAt(const QPoint &p) const;
    void activate(Region r);
    void updateSpinner();
    State m_state;
    Region m_hover = None, m_pressed = None, m_focus = None;
    QTimer m_spin;
    QElapsedTimer m_clock;
};

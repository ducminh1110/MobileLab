#pragma once
#include <QWidget>
#include "Capsule.h"
#include "GlassGroup.h"

class QAction;

// Xcode 26 toolbar: [navigator] [stop|run] MobileLab ..... ( capsule ) ..... [debug|inspector]
class Toolbar : public QWidget {
    Q_OBJECT
public:
    struct Actions { QAction *navigator, *stop, *run, *debug, *inspector; };
    Toolbar(const Actions &a, QWidget *parent = nullptr);
    Capsule *capsule() const { return m_capsule; }
    GlassGroup *leftGroup() const { return m_nav; }
    GlassGroup *runGroup() const { return m_run; }
    GlassGroup *rightGroup() const { return m_right; }
    QSize sizeHint() const override { return QSize(1000, Metrics::toolbarHeight); }
protected:
    void resizeEvent(QResizeEvent *) override { relayout(); }
    void paintEvent(QPaintEvent *) override;
private:
    void relayout();
    GlassGroup *m_nav, *m_run, *m_right;
    Capsule *m_capsule;
    QRect m_titleRect;
    bool m_showTitle = true;
};

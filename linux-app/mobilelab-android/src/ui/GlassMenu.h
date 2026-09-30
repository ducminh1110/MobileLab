#pragma once
#include <QMenu>
#include <QImage>

// QMenu drawn as a Liquid Glass popover: it samples the window under it and renders the sheet material.
class GlassMenu : public QMenu {
    Q_OBJECT
public:
    explicit GlassMenu(QWidget *parent = nullptr);
    explicit GlassMenu(const QString &title, QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
private:
    void init();
    void captureBackdrop();
    QImage m_material;
    QSize m_materialSize;
    QImage m_backdrop;
};

// Renders the sheet material for a popup window from a snapshot of `underWindow` at `globalRect`.
QImage renderPopupMaterial(QWidget *underWindow, const QRect &globalRect, qreal dpr, qreal radius);

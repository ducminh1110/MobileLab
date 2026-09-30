#pragma once
// Canvas of a device: a phone frame with the live screenshot when adb can take one, an honest placeholder otherwise.
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include "AppContext.h"

class GlassGroup;
class PillButton;
class QAction;

class TargetEditor : public QWidget {
    Q_OBJECT
public:
    explicit TargetEditor(const AppContext &ctx, QWidget *parent = nullptr);
    void showTarget(const QString &id);
    QString targetId() const { return m_id; }
    void refresh();                 // the target's state changed
    void captureNow();
    bool hasImage() const { return !m_image.isNull(); }
    QString placeholderReason() const { return m_reason; }
    void zoomBy(int steps);

signals:
    void action(const QString &name, const QString &id);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    QRectF frameRect() const;
    void placeChildren();
    void updateTimer();
    QString stateText() const;
    AppContext m_ctx;
    QString m_id, m_reason, m_previewPath;
    QPixmap m_image;
    QTimer m_timer;
    bool m_inflight = false;
    int m_zoom = 0;
    GlassGroup *m_bar;
    PillButton *m_boot;
    QAction *m_toggle, *m_camera, *m_reload, *m_zoomOut, *m_zoomIn;
};

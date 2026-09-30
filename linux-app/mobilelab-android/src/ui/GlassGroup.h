#pragma once
#include <QHBoxLayout>
#include "UiUtil.h"
#include "IconButton.h"
#include "glass/Glass.h"

// A capsule of glass holding icon buttons separated by hairlines (toolbar button groups, tab bars).
class GlassGroup : public GlassPanel {
    Q_OBJECT
public:
    explicit GlassGroup(QWidget *parent = nullptr, Glass::Kind kind = Glass::Kind::Control);
    IconButton *addButton(QAction *action, const QString &icon, const QString &tooltip = {});
    IconButton *addButton(const QString &icon);
    const QVector<IconButton *> &buttons() const { return m_buttons; }
    void setButtonSize(int w, int h);
    void paintContent(QPainter &p, const QRect &shape) override;
    QSize sizeHint() const override;

private:
    QHBoxLayout *m_layout;
    QVector<IconButton *> m_buttons;
    int m_bw = 34, m_bh = 28;
};

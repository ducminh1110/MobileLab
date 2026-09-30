#pragma once
#include <QAbstractButton>

class QAction;

// Flat icon button drawn from the SVG icon set. Inside a GlassGroup it draws only its icon and the
// hover / pressed overlay; on a panel it is a plain toolbar button.
class IconButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit IconButton(const QString &icon, QWidget *parent = nullptr);
    void setIconName(const QString &name);
    QString iconName() const { return m_icon; }
    void setGlyphSize(int px) { m_glyph = px; update(); }
    void setFixedButtonSize(int w, int h);
    void setTint(const QColor &c) { m_tint = c; update(); }     // fixed colour (invalid = follow theme)
    void setCircular(bool c) { m_circular = c; update(); }
    void setCheckedTint(const QColor &c) { m_checkedTint = c; update(); }
    void setHoverEnabled(bool on) { m_hoverBg = on; update(); }
    void setAccentWhenChecked(bool on) { m_accentChecked = on; }
    // Keeps enabled / checked / tooltip / accessible name in sync with an action and triggers it on click.
    void bindAction(QAction *a, const QString &tooltipTitle = {});
    QSize sizeHint() const override { return m_size; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void syncFromAction();
    QString m_icon;
    QSize m_size{30, 28};
    int m_glyph = 16;
    bool m_hover = false, m_circular = false, m_accentChecked = true, m_kbFocus = false;
    QColor m_tint, m_checkedTint;
    bool m_hoverBg = true;
    QAction *m_action = nullptr;
    QString m_tipTitle;
};

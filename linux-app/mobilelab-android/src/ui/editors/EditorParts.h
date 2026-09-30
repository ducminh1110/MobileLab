#pragma once
// Building blocks of the editor pages: themed labels, pill buttons, clickable rows, stat bars.
#include <QAbstractButton>
#include <QFocusEvent>
#include <QLabel>
#include "Theme.h"

class ThemedLabel : public QLabel {
    Q_OBJECT
public:
    enum class Role { Text, Secondary, Tertiary, Fail, Pass, Warn, Accent };
    ThemedLabel(const QString &text, qreal px, QFont::Weight w = QFont::Normal, Role r = Role::Text, QWidget *parent = nullptr, bool mono = false);
    void setRole(Role r);
protected:
    void changeEvent(QEvent *e) override { QLabel::changeEvent(e); }
private:
    void applyPalette();
    Role m_role;
};

class PillButton : public QAbstractButton {
    Q_OBJECT
public:
    enum class Style { Primary, Secondary, Plain };
    explicit PillButton(const QString &text, Style s = Style::Secondary, QWidget *parent = nullptr, const QString &icon = {});
    QSize sizeHint() const override;
    void setStyleKind(Style s) { m_style = s; update(); }
protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override { QAbstractButton::focusOutEvent(e); m_kb = false; update(); }
    void keyPressEvent(QKeyEvent *e) override;
private:
    Style m_style;
    QString m_icon;
    bool m_hover = false, m_kb = false;
};

// A row with icon, title, secondary text and a trailing value; clickable.
class RowButton : public QAbstractButton {
    Q_OBJECT
public:
    RowButton(const QString &icon, const QString &title, const QString &sub, const QString &trailing, QWidget *parent = nullptr);
    void setIconColor(const QColor &c) { m_iconColor = c; update(); }
    void setLeftBar(const QColor &c) { m_bar = c; update(); }
    void setPlainRow(bool plain) { m_plain = plain; setFocusPolicy(plain ? Qt::NoFocus : Qt::StrongFocus); }
    void setTitle(const QString &t) { m_title = t; update(); }
    QSize sizeHint() const override { return QSize(300, m_sub.isEmpty() ? 26 : 40); }
protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void focusInEvent(QFocusEvent *e) override { QAbstractButton::focusInEvent(e); m_kb = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason; update(); }
    void focusOutEvent(QFocusEvent *e) override { QAbstractButton::focusOutEvent(e); m_kb = false; update(); }
    void keyPressEvent(QKeyEvent *e) override;
private:
    QString m_icon, m_title, m_sub, m_trailing;
    QColor m_iconColor, m_bar;
    bool m_hover = false, m_kb = false, m_plain = false;
};

// Passed / failed / skipped as one thin segmented bar.
class StatBar : public QWidget {
    Q_OBJECT
public:
    explicit StatBar(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(6); }
    void setCounts(int passed, int failed, int skipped) { m_p = passed; m_f = failed; m_s = skipped; update(); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    int m_p = 0, m_f = 0, m_s = 0;
};

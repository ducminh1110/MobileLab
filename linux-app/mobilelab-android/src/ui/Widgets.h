#pragma once
// Small reusable widgets: filter bar, segmented control, popup button, empty state.
#include <QAbstractButton>
#include <QLineEdit>
#include <QStringList>
#include "IconButton.h"
#include "glass/Glass.h"

class QMenu;

// Transparent line edit that lives on glass (filter bars) and hides while the backdrop is captured.
class GlassLineEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit GlassLineEdit(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *e) override;
};

// Glass pill with a filter glyph, a text field and optional trailing toggle buttons (recent / failed).
class FilterBar : public GlassPanel {
    Q_OBJECT
public:
    explicit FilterBar(QWidget *parent = nullptr);
    GlassLineEdit *edit() const { return m_edit; }
    IconButton *addToggle(const QString &icon, const QString &tip, bool checked = false);
    void setPlaceholder(const QString &s) { m_edit->setPlaceholderText(s); }
    QString text() const { return m_edit->text(); }
    QSize sizeHint() const override { return QSize(200, Metrics::filterBar); }
    void paintContent(QPainter &p, const QRect &shape) override;
signals:
    void textChanged(const QString &);
    void toggled(int index, bool on);
private:
    GlassLineEdit *m_edit;
    IconButton *m_clear;
    QVector<IconButton *> m_toggles;
};

// Xcode's segmented control (Summary | Tests | Logs): a glass capsule with a raised selected segment.
class SegmentedControl : public GlassPanel {
    Q_OBJECT
public:
    explicit SegmentedControl(const QStringList &labels, QWidget *parent = nullptr);
    int current() const { return m_current; }
    void setCurrent(int i, bool emitSignal = false);
    QSize sizeHint() const override;
    void paintContent(QPainter &p, const QRect &shape) override;
signals:
    void currentChanged(int index);
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override { GlassPanel::focusInEvent(e); update(); }
    void focusOutEvent(QFocusEvent *e) override { GlassPanel::focusOutEvent(e); update(); }
private:
    QRect segmentRect(int i) const;
    QStringList m_labels;
    int m_current = 0;
};

// "Auto", "All Output" style popup: text plus up/down chevrons, opens a glass menu below.
class PopupButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit PopupButton(QWidget *parent = nullptr);
    void setItems(const QStringList &items, int current = 0);
    int currentIndex() const { return m_current; }
    QString currentText() const { return m_items.value(m_current); }
    void setCurrentIndex(int i, bool emitSignal = false);
    void setFlat(bool f) { m_flat = f; update(); }
    QSize sizeHint() const override;
signals:
    void currentChanged(int index);
protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
private:
    void openMenu();
    QStringList m_items;
    int m_current = 0;
    bool m_flat = false;
};

// Centred explanation with one primary action, shown by trees and editors that have nothing to list.
class EmptyState : public QWidget {
    Q_OBJECT
public:
    explicit EmptyState(QWidget *parent = nullptr);
    void setContent(const QString &icon, const QString &title, const QString &body, const QString &action = {});
signals:
    void actionTriggered();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *) override { update(); }
private:
    QRect actionRect() const;
    QString m_icon, m_title, m_body, m_action;
    bool m_hover = false, m_down = false;
};

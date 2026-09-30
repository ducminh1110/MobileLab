#pragma once
// Shared pieces of the six navigators: tree view with Xcode's look, delegate, filter proxy, tab bar.
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeView>
#include <functional>
#include "GlassGroup.h"
#include "Location.h"
#include "Widgets.h"

namespace NavRole {
enum {
    Kind = Qt::UserRole + 1,  // QString: group | api | abi | target | run | suite | case | issue | ...
    Id,                       // QString, stable
    Icon,                     // QString icon name
    IconColor,                // QColor (invalid = default)
    Status,                   // QString: running | booting | stopped | error | pass | fail | spin | pending | skipped
    Trailing,                 // QString right aligned secondary text
    Badge,                    // QString small pill ("auto")
    Sub,                      // QString secondary text after the title
    Loc,                      // QVariant(Location)
    Expand,                   // bool: expanded the first time it is seen
    Dim,                      // bool: greyed
    Bold,                     // bool
    Square,                   // QString: letter drawn in a coloured badge square instead of an icon (variables view)
    SquareColor,              // QColor
    SubMono                   // bool: secondary text in the mono face (variable values)
};
}

class NavFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit NavFilterProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) { setRecursiveFilteringEnabled(true); }
    void setText(const QString &t) { m_text = t.trimmed(); invalidateFilter(); }
    QString text() const { return m_text; }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    bool matches(const QModelIndex &idx) const;
    QString m_text;
};

class NavTree;

class NavDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    NavDelegate(QObject *parent, NavTree *tree) : QStyledItemDelegate(parent), m_tree(tree) {}
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
private:
    NavTree *m_tree;
};

class NavTree : public QTreeView {
    Q_OBJECT
public:
    explicit NavTree(QWidget *parent = nullptr);
    // Replaces the content. Expansion, selection and scroll position survive when ids are stable, and nothing
    // is touched when the content is identical (the models are rebuilt from live data every few seconds).
    void rebuild(const std::function<void(QStandardItemModel &)> &fill);
    void setFilterText(const QString &t);
    QString filterText() const { return m_proxy->text(); }
    void setEmpty(const QString &icon, const QString &title, const QString &body, const QString &action = {});
    QString currentId() const;
    bool selectId(const QString &id);       // no signal
    void clearSelectionSilently();
    QModelIndex findId(const QString &id) const;
    Location locationOf(const QModelIndex &idx) const;
    qreal spinAngle() const { return m_spin; }
    QStandardItemModel *source() const { return m_model; }
    int visibleRows() const;
    void setShowSkeleton(bool on) { m_skeleton = on; viewport()->update(); }
    // Variables-view density: 20px rows, 11.5px text.
    void setCompact(bool on);
    bool compact() const { return m_compact; }

signals:
    void locationRequested(const Location &loc);
    void previewRequested(const Location &loc);
    void contextRequested(const QString &id, const QPoint &globalPos);
    void emptyActionTriggered();

protected:
    void drawRow(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    void drawBranches(QPainter *p, const QRect &rect, const QModelIndex &idx) const override;
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void focusInEvent(QFocusEvent *e) override { QTreeView::focusInEvent(e); viewport()->update(); }
    void focusOutEvent(QFocusEvent *e) override { QTreeView::focusOutEvent(e); viewport()->update(); }
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void currentChanged(const QModelIndex &cur, const QModelIndex &prev) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QString signatureOf(const QStandardItemModel &m) const;
    void collectExpanded(const QModelIndex &parent, QSet<QString> &out) const;
    void applyExpansion(const QModelIndex &parent, const QSet<QString> &expanded);
    void updateSpin();
    bool hasSpinning() const;
    QStandardItemModel *m_model = nullptr;
    NavFilterProxy *m_proxy;
    EmptyState *m_empty;
    QString m_sig;
    QSet<QString> m_userExpanded, m_userCollapsed;   // remembered per id, also while the row is not in the model
    QTimer m_spinTimer;
    qreal m_spin = 0;
    bool m_skeleton = false, m_silent = false, m_spinning = false, m_mouseSelect = false, m_compact = false;
    QHash<QString, qreal> m_arrow;              // animated disclosure angle 0..1 per item id
    void animateArrow(const QModelIndex &idx, bool open);
};

// Tab bar of the navigator: a glass pill with icon buttons and an accent circle behind the selected one.
class NavTabBar : public GlassPanel {
    Q_OBJECT
public:
    struct Tab { QString icon, name; QKeySequence shortcut; };
    explicit NavTabBar(QWidget *parent = nullptr);
    void setTabs(const QVector<Tab> &tabs);
    int current() const { return m_current; }
    void setCurrent(int i, bool emitSignal = false, bool animate = true);
    QSize sizeHint() const override { return QSize(280, Metrics::navTabBar - 4); }
    void paintContent(QPainter &p, const QRect &shape) override;
    IconButton *tabButton(int i) const { return m_buttons.value(i); }
    // Small count pill on a tab (issues). count 0 hides it.
    void setBadge(int tab, int count, const QColor &color);
signals:
    void currentChanged(int index);
protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    void relayout();
    QPointF centerOf(int i) const;
    QVector<Tab> m_tabs;
    QVector<IconButton *> m_buttons;
    int m_current = 0;
    qreal m_circleX = -1;
    QVariantAnimation *m_anim = nullptr;
    QHash<int, QPair<int, QColor>> m_badges;
};

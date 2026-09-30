#pragma once
#include <QDialog>
#include "AppContext.h"
#include "Location.h"

class NavTree;
class QLineEdit;

// Ctrl+Shift+O: fuzzy jump to any target, run, test case or command.
class OpenQuickly : public QDialog {
    Q_OBJECT
public:
    struct Item {
        QString title, sub, icon, kind;
        QColor color;
        Location loc;
        QString command;   // non-empty: run this action instead of navigating
    };
    OpenQuickly(const AppContext &ctx, QWidget *parent);
    void setQuery(const QString &q);
    QVector<Item> results() const { return m_results; }
    QImage grabPopup();
    static QVector<Item> collect(const AppContext &ctx);
    static QVector<Item> rank(const QVector<Item> &all, const QString &query, int limit = 40);
signals:
    void locationChosen(const Location &loc);
    void commandChosen(const QString &command);
protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    void refill();
    void choose();
    AppContext m_ctx;
    QVector<Item> m_all, m_results;
    QLineEdit *m_edit;
    NavTree *m_list;
    QImage m_material;
    QSize m_materialSize;
};

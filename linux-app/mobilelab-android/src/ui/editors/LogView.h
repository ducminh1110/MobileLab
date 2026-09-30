#pragma once
// Source-editor look for run output: gutter with line numbers, log syntax colours, execution line,
// failure lines with an inline pill, pass/fail diamonds and clickable breakpoint markers. Windowed painting:
// only visible lines are classified and drawn. Also used (in console mode) for the debug area console.
#include <QAbstractScrollArea>
#include <QHash>
#include <QSet>
#include <QStringList>
#include "LogClassifier.h"

class GlassPanel;

class LogView : public QAbstractScrollArea {
    Q_OBJECT
public:
    enum class Mode { Source, Console };
    explicit LogView(Mode mode = Mode::Source, QWidget *parent = nullptr);
    void setLines(const QStringList &lines);
    void appendLine(const QString &line);
    void clear();
    int lineCount() const { return m_lines.size(); }
    QString lineAt(int i) const { return m_lines.value(i); }
    void scrollToLine(int line, bool select = true);
    void setCurrentLine(int line, bool reveal = true);
    int currentLine() const { return m_current; }
    void setFollow(bool on);
    bool following() const { return m_follow; }
    void setHighlight(const QString &text);
    // Console mode normally classifies "[source] text" lines; run output uses the xcodebuild classifier.
    void setXcodeSyntax(bool on) { m_xcode = on; m_cache.clear(); viewport()->update(); }
    void setFilter(const std::function<bool(const QString &)> &accept);  // console filtering (rebuilds visible list)
    QString selectedText() const;
    void setEmptyText(const QString &t) { m_emptyText = t; viewport()->update(); }
    // Geometry helpers for tests / screenshots.
    int gutterWidth() const;
    QSet<int> breakpoints() const { return m_breakpoints; }
    QSize sizeHint() const override { return QSize(500, 300); }
    int firstVisibleLine() const;
    int visibleLineCount() const;

signals:
    void currentLineChanged(int line);
    void followChanged(bool following);
    void breakpointToggled(int line, bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override { QAbstractScrollArea::focusInEvent(e); viewport()->update(); }
    void focusOutEvent(QFocusEvent *e) override { QAbstractScrollArea::focusOutEvent(e); viewport()->update(); }
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    const LogSyntax::Line &info(int i) const;
    int lineAtY(int y) const;
    int rowHeight() const;
    int charWidth() const;
    void updateRange();
    void placePill();
    void goto_(int line, bool extend);
    Mode m_mode;
    QStringList m_lines;
    QVector<int> m_visible;                               // indices into m_lines when filtered
    std::function<bool(const QString &)> m_filter;
    mutable QHash<int, LogSyntax::Line> m_cache;
    int m_current = -1, m_anchor = -1, m_sel0 = -1, m_sel1 = -1, m_maxCols = 0;
    bool m_follow = true, m_dragging = false, m_xcode = false;
    QSet<int> m_breakpoints;
    QString m_highlight, m_emptyText;
    GlassPanel *m_pill = nullptr;
};

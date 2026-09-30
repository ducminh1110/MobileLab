#pragma once
// Frameless modal sheet drawn as Liquid Glass over a snapshot of its parent window.
#include <QDialog>
#include <QImage>
#include <QVBoxLayout>

class SheetDialog : public QDialog {
    Q_OBJECT
public:
    SheetDialog(const QString &title, QWidget *parent, const QSize &contentSize);
    QVBoxLayout *body() const { return m_body; }
    void setTitleText(const QString &t);
    static constexpr int kShadow = 20;
    // Re-centres over the parent window (top aligned like an Xcode sheet).
    void place();
    // Sheets are shown without exec() by the screenshot driver.
    QImage grabSheet();

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QString m_title;
    QVBoxLayout *m_body;
    QImage m_material;
    QSize m_materialSize;
    QSize m_content;
};

// A message with one dismiss button (used for "VS Code unavailable", failures, etc.).
class MessageSheet : public SheetDialog {
    Q_OBJECT
public:
    MessageSheet(const QString &title, const QString &message, QWidget *parent, bool warning = false);
};

// Help > Keyboard Shortcuts.
class ShortcutsSheet : public SheetDialog {
    Q_OBJECT
public:
    explicit ShortcutsSheet(QWidget *parent);
};

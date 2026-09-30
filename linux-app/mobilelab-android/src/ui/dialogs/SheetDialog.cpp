#include "SheetDialog.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include "EditorParts.h"
#include "GlassMenu.h"
#include "UiUtil.h"
#include "glass/Glass.h"

SheetDialog::SheetDialog(const QString &title, QWidget *parent, const QSize &contentSize) : QDialog(parent), m_title(title), m_content(contentSize) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    setAccessibleName(title);
    setWindowTitle(title);
    setFixedSize(contentSize.width() + 2 * kShadow, contentSize.height() + 2 * kShadow);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadow + 20, kShadow + 16, kShadow + 20, kShadow + 16);
    outer->setSpacing(10);
    auto *head = new ThemedLabel(title, 15, QFont::DemiBold, ThemedLabel::Role::Text, this);
    head->setObjectName("sheet-title");
    head->setTextInteractionFlags(Qt::NoTextInteraction);
    outer->addWidget(head);
    m_body = new QVBoxLayout;
    m_body->setSpacing(8);
    outer->addLayout(m_body, 1);
    connect(&Theme::instance(), &Theme::changed, this, [this] { m_materialSize = QSize(); update(); });
}

void SheetDialog::setTitleText(const QString &t) {
    m_title = t;
    if (auto *l = findChild<ThemedLabel *>("sheet-title")) l->setText(t);
    setAccessibleName(t);
}

void SheetDialog::place() {
    QWidget *p = parentWidget() ? parentWidget()->window() : nullptr;
    if (!p) return;
    const QRect pr = p->frameGeometry();
    move(pr.center().x() - width() / 2, pr.top() + qMin(80, pr.height() / 8) - kShadow);
}

void SheetDialog::showEvent(QShowEvent *e) {
    place();
    m_materialSize = QSize();
    QDialog::showEvent(e);
}

void SheetDialog::resizeEvent(QResizeEvent *e) {
    m_materialSize = QSize();
    QDialog::resizeEvent(e);
}

void SheetDialog::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { reject(); return; }
    QDialog::keyPressEvent(e);
}

void SheetDialog::paintEvent(QPaintEvent *) {
    const QRect sheet = rect().adjusted(kShadow, kShadow, -kShadow, -kShadow);
    if (m_materialSize != size()) {
        QWidget *under = parentWidget() ? parentWidget()->window() : nullptr;
        m_material = renderPopupMaterial(under, QRect(mapToGlobal(sheet.topLeft()), sheet.size()), devicePixelRatioF(), 18);
        m_materialSize = size();
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Ui::drawSoftShadow(&p, sheet, 18, 10, 6, tk().dark ? QColor(0, 0, 0, 170) : QColor(20, 30, 60, 90));
    p.drawImage(sheet.topLeft(), m_material);
}

QImage SheetDialog::grabSheet() {
    QImage img(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(devicePixelRatioF());
    img.fill(Qt::transparent);
    render(&img);
    return img;
}

MessageSheet::MessageSheet(const QString &title, const QString &message, QWidget *parent, bool warning) : SheetDialog(title, parent, QSize(440, 190)) {
    auto *l = new ThemedLabel(message, 13, QFont::Normal, warning ? ThemedLabel::Role::Text : ThemedLabel::Role::Secondary, this);
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body()->addWidget(l, 1);
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *ok = new PillButton("OK", PillButton::Style::Primary, this);
    connect(ok, &QAbstractButton::clicked, this, &QDialog::accept);
    row->addWidget(ok);
    body()->addLayout(row);
    ok->setFocus();
}

ShortcutsSheet::ShortcutsSheet(QWidget *parent) : SheetDialog("Keyboard Shortcuts", parent, QSize(520, 560)) {
    struct S { const char *action, *keys; };
    static const S rows[] = {
        {"Run matrix on the destination", "Ctrl+R"}, {"Stop", "Ctrl+."}, {"New virtual device", "Ctrl+N"},
        {"Show or hide navigator", "Ctrl+0"}, {"Show or hide inspector", "Ctrl+Alt+0"}, {"Show or hide debug area", "Ctrl+Shift+Y"},
        {"Devices navigator", "Ctrl+1"}, {"Tests navigator", "Ctrl+2"}, {"Issues navigator", "Ctrl+3"},
        {"Find navigator", "Ctrl+4"}, {"Debug navigator", "Ctrl+5"}, {"Reports navigator", "Ctrl+6"},
        {"Find in workspace", "Ctrl+Shift+F"}, {"Open Quickly", "Ctrl+Shift+O"}, {"Go back", "Ctrl+Alt+Left"},
        {"Go forward", "Ctrl+Alt+Right"}, {"Clear console", "Ctrl+K"}, {"Settings", "Ctrl+,"},
        {"Attributes / History / Quick Help inspector", "Ctrl+Alt+1, 2, 3"}, {"Refresh targets", "F5"}, {"Quit", "Ctrl+Q"},
    };
    for (const auto &r : rows) {
        auto *h = new QHBoxLayout;
        h->addWidget(new ThemedLabel(r.action, 13, QFont::Normal, ThemedLabel::Role::Text, this), 1);
        h->addWidget(new ThemedLabel(r.keys, 12, QFont::Medium, ThemedLabel::Role::Secondary, this, true));
        body()->addLayout(h);
    }
    body()->addStretch();
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *ok = new PillButton("Done", PillButton::Style::Primary, this);
    connect(ok, &QAbstractButton::clicked, this, &QDialog::accept);
    row->addWidget(ok);
    body()->addLayout(row);
}

#include "AvdWizard.h"
#include <QHBoxLayout>
#include <QRegularExpression>
#include "AndroidEmulator.h"
#include "AndroidPackageCatalog.h"
#include "AndroidRuntime.h"
#include "EditorParts.h"
#include "UiUtil.h"

bool AvdWizard::validName(const QString &name) {
    static const QRegularExpression re("^[A-Za-z0-9_.-]{1,64}$");
    return re.match(name).hasMatch();
}

AvdWizard::AvdWizard(const AppContext &ctx, QWidget *parent) : SheetDialog("New Virtual Device", parent, QSize(560, 400)), m_ctx(ctx) {
    auto *sub = new ThemedLabel("Provisioned with Google's avdmanager from an installed system image. MobileLab bundles no images.", 12, QFont::Normal, ThemedLabel::Role::Secondary, this);
    sub->setWordWrap(true);
    body()->addWidget(sub);
    auto field = [&](const QString &label, QWidget *w) {
        auto *row = new QHBoxLayout;
        auto *l = new ThemedLabel(label, 13, QFont::Normal, ThemedLabel::Role::Secondary, this);
        l->setFixedWidth(120);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(l);
        row->addWidget(w, 1);
        body()->addLayout(row);
    };
    const AndroidPackageCatalog catalog(m_ctx.emulator ? m_ctx.emulator->sdkRoot() : QString());
    QStringList names;
    for (const auto &p : catalog.systemImages()) { names << p.displayName; m_imageIds << p.id; }
    m_image = new PopupButton(this);
    m_image->setItems(names.isEmpty() ? QStringList{"No installed system images"} : names, 0);
    m_image->setEnabled(!names.isEmpty());
    m_image->setAccessibleName("System image");
    m_name = new QLineEdit(this);
    m_name->setFont(Theme::instance().ui(13));
    m_name->setAccessibleName("AVD name");
    m_name->setFixedHeight(28);
    m_device = new QLineEdit("pixel_8", this);
    m_device->setFont(Theme::instance().ui(13));
    m_device->setAccessibleName("Device profile");
    m_device->setFixedHeight(28);
    field("Name", m_name);
    field("System image", m_image);
    field("Device profile", m_device);
    auto suggest = [this] {
        const QString id = m_imageIds.value(m_image->currentIndex());
        const QStringList parts = id.split(';');
        if (parts.size() >= 4) m_name->setText(QString("mobilelab-%1-api%2").arg(parts[3], parts[1].mid(8)));
        else m_name->setText(m_ctx.runtime && m_ctx.runtime->x86_64Host() ? "mobilelab-x86_64" : "mobilelab-arm64");
    };
    suggest();
    connect(m_image, &PopupButton::currentChanged, this, suggest);
    m_info = new ThemedLabel(QString("SDK: %1\nEmulator: %2\nKVM: %3").arg(m_ctx.emulator ? m_ctx.emulator->sdkRoot() : "unknown",
                                                                         m_ctx.emulator && m_ctx.emulator->info().available ? "detected" : "not detected",
                                                                         m_ctx.runtime && m_ctx.runtime->kvmAvailable() ? "available" : "unavailable"),
                             11, QFont::Normal, ThemedLabel::Role::Tertiary, this, true);
    body()->addWidget(m_info);
    m_error = new ThemedLabel({}, 12, QFont::Normal, ThemedLabel::Role::Fail, this);
    m_error->setWordWrap(true);
    m_error->hide();
    body()->addWidget(m_error);
    body()->addStretch();
    auto *row = new QHBoxLayout;
    row->addStretch();
    m_cancel = new PillButton("Cancel", PillButton::Style::Secondary, this);
    m_ok = new PillButton("Create AVD", PillButton::Style::Primary, this);
    connect(m_cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(m_ok, &QAbstractButton::clicked, this, &AvdWizard::create);
    row->addWidget(m_cancel);
    row->addWidget(m_ok);
    body()->addLayout(row);
    connect(m_name, &QLineEdit::textChanged, this, &AvdWizard::updateState);
    connect(m_device, &QLineEdit::textChanged, this, &AvdWizard::updateState);
    connect(m_name, &QLineEdit::returnPressed, this, &AvdWizard::create);
    updateState();
    m_name->setFocus();
    m_name->selectAll();
}

void AvdWizard::updateState() {
    QString problem;
    if (m_imageIds.isEmpty()) problem = "No system image is installed. Install one with: sdkmanager \"system-images;android-35;google_apis;x86_64\"";
    else if (!validName(m_name->text())) problem = "The name may contain letters, digits, dot, dash and underscore.";
    else if (m_ctx.runtime && m_ctx.runtime->target(m_name->text())) problem = "A virtual device with this name already exists.";
    else if (m_device->text().trimmed().isEmpty()) problem = "Enter a hardware profile such as pixel_8.";
    m_ok->setEnabled(problem.isEmpty() && !m_busy);
    m_error->setText(problem);
    m_error->setVisible(!problem.isEmpty() && !m_busy);
}

void AvdWizard::create() {
    if (!m_ok->isEnabled() || !m_ctx.emulator) return;
    m_busy = true;
    m_ok->setEnabled(false);
    m_cancel->setEnabled(false);
    m_ok->setText("Creating...");
    m_error->hide();
    const QString name = m_name->text().trimmed();
    m_ctx.emulator->createAvdAsync(this, name, m_imageIds.value(m_image->currentIndex()), m_device->text().trimmed(), [this, name](bool ok, const QString &out) {
        m_busy = false;
        m_cancel->setEnabled(true);
        m_ok->setText("Create AVD");
        if (ok) {
            m_created = name;
            emit created(name);
            accept();
            return;
        }
        m_error->setText("avdmanager could not create the device.\n" + out.left(600));
        m_error->show();
        updateState();
        m_error->show();
    });
}

#pragma once
#include "AppContext.h"
#include "SheetDialog.h"
#include "Widgets.h"

class GlassLineEdit;
class PillButton;
class ThemedLabel;

// Create a Virtual Device: name, installed system image, hardware profile. avdmanager runs asynchronously.
class AvdWizard : public SheetDialog {
    Q_OBJECT
public:
    AvdWizard(const AppContext &ctx, QWidget *parent);
    QString createdName() const { return m_created; }
    // Exposed for tests / screenshots.
    static bool validName(const QString &name);
signals:
    void created(const QString &name);
private:
    void create();
    void updateState();
    AppContext m_ctx;
    QLineEdit *m_name, *m_device;
    PopupButton *m_image;
    ThemedLabel *m_info, *m_error;
    PillButton *m_ok, *m_cancel;
    QStringList m_imageIds;
    QString m_created;
    bool m_busy = false;
};

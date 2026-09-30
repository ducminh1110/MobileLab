#pragma once
#include <QWidget>
#include "AppContext.h"
#include "Location.h"

class WelcomeEditor : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeEditor(const AppContext &ctx, QWidget *parent = nullptr);
    void refresh();
signals:
    void locationRequested(const Location &loc);
    void action(const QString &name, const QString &id);
private:
    AppContext m_ctx;
    QWidget *m_content = nullptr;
    class QVBoxLayout *m_root;
};

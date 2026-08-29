#ifndef DRUPPC_CONNECTION_SETTINGS_H
#define DRUPPC_CONNECTION_SETTINGS_H

#include <QDialog>

namespace Ui {
class ConnectionSettingsDialog;
}

// Software-level connection parameters: response timeout and retry count.
// Values persist to QSettings on accept.
class ConnectionSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionSettingsDialog(QWidget *parent = nullptr);
    ~ConnectionSettingsDialog() override;

public slots:
    void accept() override;

private:
    Ui::ConnectionSettingsDialog *ui;
};

#endif // DRUPPC_CONNECTION_SETTINGS_H

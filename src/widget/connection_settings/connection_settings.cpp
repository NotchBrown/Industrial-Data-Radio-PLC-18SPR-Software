#include "widget/connection_settings/connection_settings.h"
#include "ui_connection_settings.h"

#include <QLayout>
#include <QSettings>

ConnectionSettingsDialog::ConnectionSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConnectionSettingsDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    QSettings s;
    ui->spinRespTimeout->setValue(qBound(100, s.value("connection/timeoutMs", 1000).toInt(), 10000));
    ui->spinRetries->setValue(qBound(0, s.value("connection/retries", 3).toInt(), 10));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ConnectionSettingsDialog::~ConnectionSettingsDialog()
{
    delete ui;
}

void ConnectionSettingsDialog::accept()
{
    QSettings s;
    s.setValue("connection/timeoutMs", ui->spinRespTimeout->value());
    s.setValue("connection/retries", ui->spinRetries->value());
    QDialog::accept();
}

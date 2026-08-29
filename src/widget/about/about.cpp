#include "widget/about/about.h"
#include "ui_about.h"

#include <QCoreApplication>
#include <QFile>
#include <QLayout>
#include <QPixmap>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    ui->lblVersion->setText(tr("Version %1").arg(QCoreApplication::applicationVersion()));

    const QString iconPath = QCoreApplication::applicationDirPath() + "/resource/icon/main.ico";
    if (QFile::exists(iconPath)) {
        ui->lblIcon->setPixmap(
                QPixmap(iconPath).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

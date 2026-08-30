#include "widget/about/about.h"
#include "ui_about.h"

#include <QCoreApplication>
#include <QIcon>
#include <QLayout>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    ui->lblVersion->setText(tr("Version %1").arg(QCoreApplication::applicationVersion()));

    // Show the app icon (QLabel + title bar). Fall back to the application
    // window icon (also loaded from resource/icon/main.ico in main.cpp).
    const QString iconPath =
            QCoreApplication::applicationDirPath() + "/resource/icon/main.ico";
    QIcon appIcon = QIcon(iconPath);
    if (appIcon.isNull())
        appIcon = QApplication::windowIcon();
    if (!appIcon.isNull()) {
        ui->lblIcon->setPixmap(appIcon.pixmap(48, 48));
        setWindowIcon(appIcon);
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

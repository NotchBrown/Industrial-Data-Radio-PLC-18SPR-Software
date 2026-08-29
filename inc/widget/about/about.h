#ifndef DRUPPC_ABOUT_H
#define DRUPPC_ABOUT_H

#include <QDialog>

namespace Ui {
class AboutDialog;
}

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override;

private:
    Ui::AboutDialog *ui;
};

#endif // DRUPPC_ABOUT_H

#include "ui/unfreezedialog.h"

#include "ui_unfreezedialog.h"

UnfreezeDialog::UnfreezeDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UnfreezeDialog)
{
    ui->setupUi(this);
    connect(ui->cancelUnfreezeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->confirmUnfreezeButton,
            &QPushButton::clicked,
            this,
            &UnfreezeDialog::validateAndAccept);
}

UnfreezeDialog::~UnfreezeDialog()
{
    delete ui;
}

QString UnfreezeDialog::password() const
{
    return ui->unfreezePasswordEdit->text();
}

void UnfreezeDialog::validateAndAccept()
{
    if (password().isEmpty()) {
        ui->unfreezeMessageLabel->setText(QStringLiteral("请输入当前密码。"));
        return;
    }
    accept();
}

#include "ui/passworddialog.h"

#include "ui_passworddialog.h"

PasswordDialog::PasswordDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PasswordDialog)
{
    ui->setupUi(this);
    connect(ui->cancelPasswordButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->confirmPasswordButton,
            &QPushButton::clicked,
            this,
            &PasswordDialog::validateAndAccept);
}

PasswordDialog::~PasswordDialog()
{
    delete ui;
}

QString PasswordDialog::currentPassword() const
{
    return ui->currentPasswordEdit->text();
}

QString PasswordDialog::newPassword() const
{
    return ui->newPasswordEdit->text();
}

QString PasswordDialog::passwordConfirmation() const
{
    return ui->confirmNewPasswordEdit->text();
}

void PasswordDialog::validateAndAccept()
{
    if (currentPassword().isEmpty() || newPassword().isEmpty()
        || passwordConfirmation().isEmpty()) {
        ui->passwordMessageLabel->setText(QStringLiteral("三个密码字段都必须填写。"));
        return;
    }
    if (newPassword() != passwordConfirmation()) {
        ui->passwordMessageLabel->setText(QStringLiteral("两次输入的新密码不一致。"));
        return;
    }
    accept();
}

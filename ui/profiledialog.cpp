// 资料编辑对话框实现：在提交前完成姓名和地址的界面级校验。
#include "ui/theme.h"
#include "ui/profiledialog.h"

#include "ui_profiledialog.h"

ProfileDialog::ProfileDialog(const QString &name,
                             const QString &address,
                             QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ProfileDialog)
{
    ui->setupUi(this);
    bank::ui::styleDialog(this);
    ui->profileNameEdit->setText(name);
    ui->profileAddressEdit->setText(address);
    connect(ui->cancelProfileButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->confirmProfileButton,
            &QPushButton::clicked,
            this,
            &ProfileDialog::validateAndAccept);
}

ProfileDialog::~ProfileDialog()
{
    delete ui;
}

QString ProfileDialog::name() const
{
    return ui->profileNameEdit->text().trimmed();
}

QString ProfileDialog::address() const
{
    return ui->profileAddressEdit->text().trimmed();
}

void ProfileDialog::validateAndAccept()
{
    if (name().isEmpty() || address().isEmpty()) {
        ui->profileMessageLabel->setText(QStringLiteral("姓名和地址不能为空。"));
        return;
    }
    accept();
}

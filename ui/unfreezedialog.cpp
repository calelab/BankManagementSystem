// 解除挂失对话框实现：阻止空密码提交，实际验证仍交给业务服务。
#include "ui/theme.h"
#include "ui/unfreezedialog.h"

#include "ui_unfreezedialog.h"

UnfreezeDialog::UnfreezeDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UnfreezeDialog)
{
    ui->setupUi(this);
    bank::ui::styleDialog(this);
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

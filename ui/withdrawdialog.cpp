#include "ui/withdrawdialog.h"

#include "services/bankservice.h"
#include "ui_withdrawdialog.h"
#include "utils/moneyutils.h"

#include <algorithm>
#include <utility>

WithdrawDialog::WithdrawDialog(bank::BankService *service,
                               QString depositId,
                               QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WithdrawDialog)
    , service_(service)
    , depositId_(std::move(depositId))
{
    ui->setupUi(this);
    ui->withdrawDepositIdLabel->setText(depositId_);

    if (service_) {
        const bank::AccountDetailsResult details = service_->currentAccountDetails();
        if (details.status.success && details.details) {
            const auto iterator = std::find_if(
                details.details->deposits.cbegin(),
                details.details->deposits.cend(),
                [this](const bank::FixedDeposit &deposit) {
                    return deposit.depositId() == depositId_;
                });
            if (iterator != details.details->deposits.cend()) {
                ui->withdrawRemainingPrincipalLabel->setText(
                    bank::MoneyUtils::formatCents(iterator->remainingPrincipalCents()));
            }
        }
    }

    connect(ui->withdrawAmountEdit,
            &QLineEdit::textChanged,
            this,
            &WithdrawDialog::refreshPreview);
    connect(ui->cancelWithdrawButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->confirmWithdrawButton,
            &QPushButton::clicked,
            this,
            &WithdrawDialog::confirmPreview);
}

WithdrawDialog::~WithdrawDialog()
{
    delete ui;
}

qint64 WithdrawDialog::principalCents() const
{
    return principalCents_;
}

void WithdrawDialog::refreshPreview()
{
    previewValid_ = false;
    ui->confirmWithdrawButton->setEnabled(false);
    ui->withdrawKindLabel->setText(QStringLiteral("等待有效金额"));
    ui->withdrawInterestLabel->setText(bank::MoneyUtils::formatCents(0));
    ui->withdrawPayoutLabel->setText(bank::MoneyUtils::formatCents(0));

    QString parseError;
    const auto parsed = bank::MoneyUtils::parseCents(ui->withdrawAmountEdit->text(),
                                                     &parseError);
    if (!parsed) {
        ui->withdrawMessageLabel->setText(parseError);
        return;
    }
    if (!service_) {
        ui->withdrawMessageLabel->setText(QStringLiteral("业务服务不可用"));
        return;
    }

    const bank::WithdrawalPreviewResult preview = service_->previewWithdrawal(depositId_,
                                                                               *parsed);
    if (!preview.status.success) {
        ui->withdrawMessageLabel->setText(preview.status.message);
        return;
    }

    principalCents_ = *parsed;
    previewValid_ = true;
    ui->withdrawKindLabel->setText(
        preview.calculation.kind == bank::WithdrawalKind::Early
            ? QStringLiteral("提前支取（未到期部分按活期规则计息）")
            : QStringLiteral("到期支取"));
    ui->withdrawInterestLabel->setText(
        bank::MoneyUtils::formatCents(preview.calculation.interestCents));
    ui->withdrawPayoutLabel->setText(
        bank::MoneyUtils::formatCents(preview.calculation.actualPayoutCents));
    ui->withdrawMessageLabel->setText(
        QStringLiteral("请核对本金、利息和实际支付金额后确认。"));
    ui->confirmWithdrawButton->setEnabled(true);
}

void WithdrawDialog::confirmPreview()
{
    if (previewValid_) {
        accept();
    }
}

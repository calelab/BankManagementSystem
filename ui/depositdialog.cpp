#include "ui/depositdialog.h"

#include "models/fixeddeposit.h"
#include "ui_depositdialog.h"
#include "utils/moneyutils.h"

DepositDialog::DepositDialog(const QDate &startDate, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DepositDialog)
    , startDate_(startDate)
{
    ui->setupUi(this);
    connect(ui->cancelDepositButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->confirmDepositButton,
            &QPushButton::clicked,
            this,
            &DepositDialog::validateAndAccept);
    connect(ui->depositTermButtonGroup,
            &QButtonGroup::idToggled,
            this,
            [this](int, bool checked) {
                if (checked) {
                    updateTermSummary();
                }
            });
    updateTermSummary();
}

DepositDialog::~DepositDialog()
{
    delete ui;
}

qint64 DepositDialog::principalCents() const
{
    return principalCents_;
}

bank::DepositTerm DepositDialog::selectedTerm() const
{
    if (ui->threeYearTermRadioButton->isChecked()) {
        return bank::DepositTerm::ThreeYears;
    }
    if (ui->fiveYearTermRadioButton->isChecked()) {
        return bank::DepositTerm::FiveYears;
    }
    return bank::DepositTerm::OneYear;
}

void DepositDialog::updateTermSummary()
{
    const bank::DepositTerm term = selectedTerm();
    ui->depositRateLabel->setText(
        bank::MoneyUtils::formatRateBasisPoints(bank::annualRateBasisPoints(term)));
    ui->depositStartDateLabel->setText(startDate_.toString(QStringLiteral("yyyy-MM-dd")));
    ui->depositMaturityDateLabel->setText(
        bank::FixedDeposit::calculateMaturityDate(startDate_, term)
            .toString(QStringLiteral("yyyy-MM-dd")));
}

void DepositDialog::validateAndAccept()
{
    QString errorMessage;
    const auto parsed = bank::MoneyUtils::parseCents(ui->depositAmountEdit->text(),
                                                     &errorMessage);
    if (!parsed) {
        ui->depositMessageLabel->setText(errorMessage);
        ui->depositAmountEdit->setFocus();
        return;
    }
    principalCents_ = *parsed;
    accept();
}

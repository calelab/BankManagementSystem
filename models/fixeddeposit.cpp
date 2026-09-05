// 定期存款实现：维护剩余本金，并由日期和余额推导当前状态。
#include "models/fixeddeposit.h"

#include <QRegularExpression>

#include <utility>

namespace bank {
namespace {

bool failValidation(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

} // namespace

FixedDeposit::FixedDeposit(QString depositId,
                           qint64 originalPrincipalCents,
                           qint64 remainingPrincipalCents,
                           QDate startDate,
                           DepositTerm term,
                           int annualRateBasisPoints,
                           QDate maturityDate,
                           QString openingEmployeeId,
                           QDateTime createdAt)
    : depositId_(std::move(depositId))
    , originalPrincipalCents_(originalPrincipalCents)
    , remainingPrincipalCents_(remainingPrincipalCents)
    , startDate_(std::move(startDate))
    , term_(term)
    , annualRateBasisPoints_(annualRateBasisPoints)
    , maturityDate_(std::move(maturityDate))
    , openingEmployeeId_(std::move(openingEmployeeId))
    , createdAt_(std::move(createdAt))
{
}

const QString &FixedDeposit::depositId() const
{
    return depositId_;
}

qint64 FixedDeposit::originalPrincipalCents() const
{
    return originalPrincipalCents_;
}

qint64 FixedDeposit::remainingPrincipalCents() const
{
    return remainingPrincipalCents_;
}

const QDate &FixedDeposit::startDate() const
{
    return startDate_;
}

DepositTerm FixedDeposit::term() const
{
    return term_;
}

int FixedDeposit::annualRateBasisPoints() const
{
    return annualRateBasisPoints_;
}

const QDate &FixedDeposit::maturityDate() const
{
    return maturityDate_;
}

const QString &FixedDeposit::openingEmployeeId() const
{
    return openingEmployeeId_;
}

const QDateTime &FixedDeposit::createdAt() const
{
    return createdAt_;
}

bool FixedDeposit::setRemainingPrincipalCents(qint64 cents)
{
    // 原始本金保留开户事实，部分支取只递减剩余本金。
    if (cents < 0 || cents > originalPrincipalCents_) {
        return false;
    }
    remainingPrincipalCents_ = cents;
    return true;
}

DepositStatus FixedDeposit::statusOn(const QDate &referenceDate) const
{
    // 本金为零即永久结清；否则再依据到期日区分未到期与已到期。
    if (remainingPrincipalCents_ == 0) {
        return DepositStatus::Closed;
    }
    return referenceDate < maturityDate_ ? DepositStatus::Active : DepositStatus::Matured;
}

bool FixedDeposit::isValid(QString *errorMessage) const
{
    // 重新计算利率和到期日，可发现持久化文件中被篡改或互相矛盾的字段。
    static const QRegularExpression depositIdPattern(QStringLiteral("^FD[0-9]{6,}$"));
    static const QRegularExpression employeeIdPattern(QStringLiteral("^E(?:0[1-9]|[1-9][0-9])$"));

    if (!depositIdPattern.match(depositId_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("存款编号格式无效"));
    }
    if (originalPrincipalCents_ <= 0) {
        return failValidation(errorMessage, QStringLiteral("原始本金必须大于零"));
    }
    if (remainingPrincipalCents_ < 0 || remainingPrincipalCents_ > originalPrincipalCents_) {
        return failValidation(errorMessage, QStringLiteral("剩余本金超出有效范围"));
    }
    if (!startDate_.isValid()) {
        return failValidation(errorMessage, QStringLiteral("存入日期无效"));
    }
    if (termYears(term_) == 0) {
        return failValidation(errorMessage, QStringLiteral("储种无效"));
    }
    if (annualRateBasisPoints_ != bank::annualRateBasisPoints(term_)) {
        return failValidation(errorMessage, QStringLiteral("存款利率与储种不一致"));
    }
    if (maturityDate_ != calculateMaturityDate(startDate_, term_)) {
        return failValidation(errorMessage, QStringLiteral("到期日与期限不一致"));
    }
    if (!employeeIdPattern.match(openingEmployeeId_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("开户营业员工号格式无效"));
    }
    if (!createdAt_.isValid()) {
        return failValidation(errorMessage, QStringLiteral("创建时间无效"));
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QDate FixedDeposit::calculateMaturityDate(const QDate &startDate, DepositTerm term)
{
    // 使用日历年而非固定天数，确保闰年附近的到期日符合业务直觉。
    const int years = termYears(term);
    return startDate.isValid() && years > 0 ? startDate.addYears(years) : QDate();
}

} // namespace bank

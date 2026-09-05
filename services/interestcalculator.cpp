// 计息服务实现：全程使用整数分和基点，避免浮点金额误差及中间值溢出。
#include "services/interestcalculator.h"

#include "models/fixeddeposit.h"

#include <limits>

namespace bank {
namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

std::optional<qint64> checkedMultiply(qint64 left, qint64 right)
{
    if (left < 0 || right < 0) {
        return std::nullopt;
    }
    if (left != 0 && right > std::numeric_limits<qint64>::max() / left) {
        return std::nullopt;
    }
    return left * right;
}

std::optional<qint64> roundedPositiveRatio(qint64 value,
                                          qint64 numerator,
                                          qint64 denominator)
{
    if (value < 0 || numerator < 0 || denominator <= 0) {
        return std::nullopt;
    }

    // 把本金拆成商和余数后再乘，结果等价于一次性计算，但不会让中间乘积无谓溢出。
    const qint64 whole = value / denominator;
    const qint64 remainder = value % denominator;
    const auto wholePart = checkedMultiply(whole, numerator);
    const auto remainderProduct = checkedMultiply(remainder, numerator);
    if (!wholePart || !remainderProduct
        || *remainderProduct > std::numeric_limits<qint64>::max() - denominator / 2) {
        return std::nullopt;
    }

    // 全部利息共用“加半个分母再整除”的正数四舍五入规则，结果直接落到整数分。
    const qint64 roundedRemainder = (*remainderProduct + denominator / 2) / denominator;
    if (*wholePart > std::numeric_limits<qint64>::max() - roundedRemainder) {
        return std::nullopt;
    }
    return *wholePart + roundedRemainder;
}

} // namespace

std::optional<qint64> InterestCalculator::earlyWithdrawalInterest(
    qint64 principalCents,
    const QDate &startDate,
    const QDate &withdrawalDate,
    QString *errorMessage)
{
    if (principalCents <= 0) {
        setError(errorMessage, QStringLiteral("支取本金必须大于零"));
        return std::nullopt;
    }
    if (!startDate.isValid() || !withdrawalDate.isValid()) {
        setError(errorMessage, QStringLiteral("计息日期无效"));
        return std::nullopt;
    }

    // 提前支取按实际持有天数和活期年利率计算，不使用约定定期利率。
    const qint64 days = startDate.daysTo(withdrawalDate);
    if (days < 0) {
        setError(errorMessage, QStringLiteral("支取日期不能早于存入日期"));
        return std::nullopt;
    }
    const auto numerator = checkedMultiply(EarlyWithdrawalRateBasisPoints, days);
    if (!numerator) {
        setError(errorMessage, QStringLiteral("计息天数超出可计算范围"));
        return std::nullopt;
    }

    const auto interest = roundedPositiveRatio(
        principalCents, *numerator, BasisPointsDenominator * DaysPerYear);
    if (!interest) {
        setError(errorMessage, QStringLiteral("利息计算超出可表示范围"));
        return std::nullopt;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return interest;
}

std::optional<qint64> InterestCalculator::maturedInterest(
    qint64 principalCents,
    int annualRateBasisPoints,
    int termYearsValue,
    QString *errorMessage)
{
    if (principalCents <= 0 || annualRateBasisPoints <= 0 || termYearsValue <= 0) {
        setError(errorMessage, QStringLiteral("本金、利率和期限必须大于零"));
        return std::nullopt;
    }
    const auto numerator = checkedMultiply(annualRateBasisPoints, termYearsValue);
    if (!numerator) {
        setError(errorMessage, QStringLiteral("利率或期限超出可计算范围"));
        return std::nullopt;
    }

    // 到期利息采用存入时锁定的年利率乘完整期限，不复利也不追加逾期利息。
    const auto interest = roundedPositiveRatio(
        principalCents, *numerator, BasisPointsDenominator);
    if (!interest) {
        setError(errorMessage, QStringLiteral("利息计算超出可表示范围"));
        return std::nullopt;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return interest;
}

std::optional<WithdrawalCalculation> InterestCalculator::withdrawal(
    const FixedDeposit &deposit,
    qint64 principalCents,
    const QDate &withdrawalDate,
    QString *errorMessage)
{
    if (principalCents <= 0 || principalCents > deposit.remainingPrincipalCents()) {
        setError(errorMessage, QStringLiteral("支取本金必须在剩余本金范围内"));
        return std::nullopt;
    }
    if (!withdrawalDate.isValid() || withdrawalDate < deposit.startDate()) {
        setError(errorMessage, QStringLiteral("支取日期无效"));
        return std::nullopt;
    }

    WithdrawalCalculation calculation;
    calculation.principalCents = principalCents;
    std::optional<qint64> interest;
    // 到期日当天及之后属于正常到期支取；此前统一走提前支取规则。
    if (withdrawalDate < deposit.maturityDate()) {
        calculation.kind = WithdrawalKind::Early;
        interest = earlyWithdrawalInterest(
            principalCents, deposit.startDate(), withdrawalDate, errorMessage);
    } else {
        calculation.kind = WithdrawalKind::Matured;
        interest = maturedInterest(principalCents,
                                    deposit.annualRateBasisPoints(),
                                    termYears(deposit.term()),
                                    errorMessage);
    }
    if (!interest) {
        return std::nullopt;
    }
    if (principalCents > std::numeric_limits<qint64>::max() - *interest) {
        setError(errorMessage, QStringLiteral("本息合计超出可表示范围"));
        return std::nullopt;
    }

    calculation.interestCents = *interest;
    calculation.actualPayoutCents = principalCents + *interest;
    if (errorMessage) {
        errorMessage->clear();
    }
    return calculation;
}

} // namespace bank

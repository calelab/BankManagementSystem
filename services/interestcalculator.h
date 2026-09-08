// 计息服务声明：集中定义提前支取、到期支取和统一整数舍入规则。
#ifndef INTERESTCALCULATOR_H
#define INTERESTCALCULATOR_H

#include "models/banktypes.h"

#include <QDate>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace bank {

class FixedDeposit;

struct WithdrawalCalculation {
    WithdrawalKind kind = WithdrawalKind::None;
    // 三个金额均以分表示，实付额等于本次本金与利息之和。
    qint64 principalCents = 0;
    qint64 interestCents = 0;
    qint64 actualPayoutCents = 0;
};

// InterestCalculator 集中执行所有计息和分级四舍五入，UI 与业务服务不复制公式。
class InterestCalculator
{
public:
    // 利率使用整数基点：1 基点 = 0.01%，100 基点 = 1%；提前支取按 365 天折算实际存款天数。
    static constexpr int EarlyWithdrawalRateBasisPoints = 5;
    static constexpr qint64 BasisPointsDenominator = 10000;
    static constexpr qint64 DaysPerYear = 365;

    // 未到期利息按实际存款天数计算；日期倒置或计算溢出时返回空值。
    static std::optional<qint64> earlyWithdrawalInterest(
        qint64 principalCents,
        const QDate &startDate,
        const QDate &withdrawalDate,
        QString *errorMessage = nullptr);

    // 到期利息按开户时固定年利率和完整期限计算，不追加逾期利息。
    static std::optional<qint64> maturedInterest(
        qint64 principalCents,
        int annualRateBasisPoints,
        int termYears,
        QString *errorMessage = nullptr);

    // 根据具体存款和支取日判断提前/到期支取，并计算本金、利息和实付额。
    static std::optional<WithdrawalCalculation> withdrawal(
        const FixedDeposit &deposit,
        qint64 principalCents,
        const QDate &withdrawalDate,
        QString *errorMessage = nullptr);
};

} // namespace bank

#endif // INTERESTCALCULATOR_H

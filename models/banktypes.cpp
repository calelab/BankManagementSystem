#include "models/banktypes.h"

namespace bank {

int termYears(DepositTerm term)
{
    switch (term) {
    case DepositTerm::OneYear:
        return 1;
    case DepositTerm::ThreeYears:
        return 3;
    case DepositTerm::FiveYears:
        return 5;
    }
    return 0;
}

int annualRateBasisPoints(DepositTerm term)
{
    switch (term) {
    case DepositTerm::OneYear:
        return 198;
    case DepositTerm::ThreeYears:
        return 225;
    case DepositTerm::FiveYears:
        return 350;
    }
    return 0;
}

QString depositTermDisplayName(DepositTerm term)
{
    switch (term) {
    case DepositTerm::OneYear:
        return QStringLiteral("一年期定期");
    case DepositTerm::ThreeYears:
        return QStringLiteral("三年期定期");
    case DepositTerm::FiveYears:
        return QStringLiteral("五年期定期");
    }
    return QStringLiteral("未知储种");
}

QString depositStatusDisplayName(DepositStatus status)
{
    switch (status) {
    case DepositStatus::Active:
        return QStringLiteral("未到期");
    case DepositStatus::Matured:
        return QStringLiteral("已到期待支取");
    case DepositStatus::Closed:
        return QStringLiteral("已结清");
    }
    return QStringLiteral("未知状态");
}

} // namespace bank

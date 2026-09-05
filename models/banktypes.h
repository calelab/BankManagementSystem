// 银行业务公共类型：定义储种、状态、交易类型及其稳定换算规则。
#ifndef BANKTYPES_H
#define BANKTYPES_H

#include <QString>

namespace bank {

// 定期储种决定期限和开户时采用的固定年利率，业务层不能让用户直接改利率。
enum class DepositTerm {
    OneYear,
    ThreeYears,
    FiveYears
};

// 存款状态由剩余本金和基准日期实时推导，避免持久化出互相矛盾的状态字段。
enum class DepositStatus {
    Active,
    Matured,
    Closed
};

enum class TransactionType {
    Deposit,
    Withdrawal
};

// None 只配合存款交易；支取必须明确区分提前支取或正常到期支取。
enum class WithdrawalKind {
    None,
    Early,
    Matured
};

// 返回储种对应的完整期限；遇到无效枚举值时返回 0。
int termYears(DepositTerm term);

// 利率使用整数基点保存，例如 198 表示 1.98%；无效储种返回 0。
int annualRateBasisPoints(DepositTerm term);

QString depositTermDisplayName(DepositTerm term);
QString depositStatusDisplayName(DepositStatus status);

} // namespace bank

#endif // BANKTYPES_H

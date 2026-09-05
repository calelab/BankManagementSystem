// 交易记录实现：验证交易类型、支取方式及本息金额的合法组合。
#include "models/transaction.h"

#include <QRegularExpression>

#include <limits>
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

Transaction::Transaction(QString transactionId,
                         QString accountNumber,
                         QString depositId,
                         QDateTime dateTime,
                         TransactionType type,
                         WithdrawalKind withdrawalKind,
                         qint64 principalAmountCents,
                         qint64 interestAmountCents,
                         QString employeeId)
    : transactionId_(std::move(transactionId))
    , accountNumber_(std::move(accountNumber))
    , depositId_(std::move(depositId))
    , dateTime_(std::move(dateTime))
    , type_(type)
    , withdrawalKind_(withdrawalKind)
    , principalAmountCents_(principalAmountCents)
    , interestAmountCents_(interestAmountCents)
    , employeeId_(std::move(employeeId))
{
}

const QString &Transaction::transactionId() const
{
    return transactionId_;
}

const QString &Transaction::accountNumber() const
{
    return accountNumber_;
}

const QString &Transaction::depositId() const
{
    return depositId_;
}

const QDateTime &Transaction::dateTime() const
{
    return dateTime_;
}

TransactionType Transaction::type() const
{
    return type_;
}

WithdrawalKind Transaction::withdrawalKind() const
{
    return withdrawalKind_;
}

qint64 Transaction::principalAmountCents() const
{
    return principalAmountCents_;
}

qint64 Transaction::interestAmountCents() const
{
    return interestAmountCents_;
}

const QString &Transaction::employeeId() const
{
    return employeeId_;
}

qint64 Transaction::actualPayoutCents() const
{
    // 构造后的对象只在 isValid() 通过后使用，因此本息相加已完成溢出校验。
    return principalAmountCents_ + interestAmountCents_;
}

bool Transaction::isValid(QString *errorMessage) const
{
    static const QRegularExpression transactionIdPattern(QStringLiteral("^TX[0-9]{6,}$"));
    static const QRegularExpression depositIdPattern(QStringLiteral("^FD[0-9]{6,}$"));
    static const QRegularExpression accountPattern(QStringLiteral("^[0-9]+$"));
    static const QRegularExpression employeeIdPattern(QStringLiteral("^E(?:0[1-9]|[1-9][0-9])$"));

    if (!transactionIdPattern.match(transactionId_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("交易编号格式无效"));
    }
    if (!accountPattern.match(accountNumber_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("账号格式无效"));
    }
    if (!depositIdPattern.match(depositId_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("关联存款编号格式无效"));
    }
    if (!dateTime_.isValid()) {
        return failValidation(errorMessage, QStringLiteral("交易时间无效"));
    }
    if (principalAmountCents_ <= 0 || interestAmountCents_ < 0) {
        return failValidation(errorMessage, QStringLiteral("交易金额无效"));
    }
    if (principalAmountCents_ > std::numeric_limits<qint64>::max() - interestAmountCents_) {
        return failValidation(errorMessage, QStringLiteral("交易支付金额溢出"));
    }
    if (!employeeIdPattern.match(employeeId_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("营业员工号格式无效"));
    }

    // 存款流水不含利息；支取流水必须明确记录提前或到期类型。
    if (type_ == TransactionType::Deposit) {
        if (withdrawalKind_ != WithdrawalKind::None || interestAmountCents_ != 0) {
            return failValidation(errorMessage, QStringLiteral("存款交易不能包含支取类型或利息"));
        }
    } else if (type_ == TransactionType::Withdrawal) {
        if (withdrawalKind_ != WithdrawalKind::Early
            && withdrawalKind_ != WithdrawalKind::Matured) {
            return failValidation(errorMessage, QStringLiteral("支取交易缺少有效支取类型"));
        }
    } else {
        return failValidation(errorMessage, QStringLiteral("交易类型无效"));
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace bank

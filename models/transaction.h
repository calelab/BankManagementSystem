#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "models/banktypes.h"

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace bank {

// Transaction 只记录一次本金变动及其利息，资金方向由业务类型表达。
class Transaction
{
public:
    Transaction() = default;
    Transaction(QString transactionId,
                QString accountNumber,
                QString depositId,
                QDateTime dateTime,
                TransactionType type,
                WithdrawalKind withdrawalKind,
                qint64 principalAmountCents,
                qint64 interestAmountCents,
                QString employeeId);

    const QString &transactionId() const;
    const QString &accountNumber() const;
    const QString &depositId() const;
    const QDateTime &dateTime() const;
    TransactionType type() const;
    WithdrawalKind withdrawalKind() const;
    qint64 principalAmountCents() const;
    qint64 interestAmountCents() const;
    const QString &employeeId() const;

    // 返回本金与利息之和；isValid() 会保证该加法不会溢出。
    qint64 actualPayoutCents() const;

    // 检查存款与支取记录各自必须满足的字段组合和金额约束。
    bool isValid(QString *errorMessage = nullptr) const;

private:
    QString transactionId_;
    QString accountNumber_;
    QString depositId_;
    QDateTime dateTime_;
    TransactionType type_ = TransactionType::Deposit;
    WithdrawalKind withdrawalKind_ = WithdrawalKind::None;
    qint64 principalAmountCents_ = 0;
    qint64 interestAmountCents_ = 0;
    QString employeeId_;
};

} // namespace bank

#endif // TRANSACTION_H

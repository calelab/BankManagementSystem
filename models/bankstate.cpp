#include "models/bankstate.h"

#include <QSet>

#include <limits>
#include <optional>
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

QString issuePrefixedId(const QString &prefix, quint64 &sequence)
{
    if (sequence == 0 || sequence == std::numeric_limits<quint64>::max()) {
        return {};
    }
    const QString number = QString::number(sequence).rightJustified(6, QLatin1Char('0'));
    ++sequence;
    return prefix + number;
}

std::optional<quint64> numericSuffix(const QString &identifier, qsizetype prefixLength)
{
    bool ok = false;
    const quint64 value = identifier.sliced(prefixLength).toULongLong(&ok);
    return ok ? std::optional<quint64>(value) : std::nullopt;
}

} // namespace

BankState::BankState(int schemaVersion,
                     quint64 nextAccountSequence,
                     quint64 nextDepositSequence,
                     quint64 nextTransactionSequence,
                     QVector<Depositor> depositors)
    : schemaVersion_(schemaVersion)
    , nextAccountSequence_(nextAccountSequence)
    , nextDepositSequence_(nextDepositSequence)
    , nextTransactionSequence_(nextTransactionSequence)
    , depositors_(std::move(depositors))
{
}

int BankState::schemaVersion() const
{
    return schemaVersion_;
}

quint64 BankState::nextAccountSequence() const
{
    return nextAccountSequence_;
}

quint64 BankState::nextDepositSequence() const
{
    return nextDepositSequence_;
}

quint64 BankState::nextTransactionSequence() const
{
    return nextTransactionSequence_;
}

const QVector<Depositor> &BankState::depositors() const
{
    return depositors_;
}

QString BankState::issueAccountNumber()
{
    constexpr quint64 accountNumberBase = 100000;
    if (nextAccountSequence_ == 0
        || nextAccountSequence_ > std::numeric_limits<quint64>::max() - accountNumberBase) {
        return {};
    }
    const QString accountNumber = QString::number(accountNumberBase + nextAccountSequence_);
    ++nextAccountSequence_;
    return accountNumber;
}

QString BankState::issueDepositId()
{
    return issuePrefixedId(QStringLiteral("FD"), nextDepositSequence_);
}

QString BankState::issueTransactionId()
{
    return issuePrefixedId(QStringLiteral("TX"), nextTransactionSequence_);
}

bool BankState::addDepositor(const Depositor &depositor, QString *errorMessage)
{
    QString childError;
    if (!depositor.isValid(&childError)) {
        return failValidation(errorMessage, QStringLiteral("储户无效：%1").arg(childError));
    }

    QSet<QString> existingDepositIds;
    QSet<QString> existingTransactionIds;
    for (const Depositor &existing : depositors_) {
        if (existing.accountNumber() == depositor.accountNumber()) {
            return failValidation(errorMessage, QStringLiteral("账号重复"));
        }
        for (const FixedDeposit &deposit : existing.deposits()) {
            existingDepositIds.insert(deposit.depositId());
        }
        for (const Transaction &transaction : existing.transactions()) {
            existingTransactionIds.insert(transaction.transactionId());
        }
    }

    for (const FixedDeposit &deposit : depositor.deposits()) {
        if (existingDepositIds.contains(deposit.depositId())) {
            return failValidation(errorMessage, QStringLiteral("存款编号全局重复"));
        }
    }
    for (const Transaction &transaction : depositor.transactions()) {
        if (existingTransactionIds.contains(transaction.transactionId())) {
            return failValidation(errorMessage, QStringLiteral("交易编号全局重复"));
        }
    }

    depositors_.append(depositor);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

Depositor *BankState::findDepositor(const QString &accountNumber)
{
    for (Depositor &depositor : depositors_) {
        if (depositor.accountNumber() == accountNumber) {
            return &depositor;
        }
    }
    return nullptr;
}

const Depositor *BankState::findDepositor(const QString &accountNumber) const
{
    for (const Depositor &depositor : depositors_) {
        if (depositor.accountNumber() == accountNumber) {
            return &depositor;
        }
    }
    return nullptr;
}

bool BankState::isValid(QString *errorMessage) const
{
    if (schemaVersion_ != CurrentSchemaVersion) {
        return failValidation(errorMessage, QStringLiteral("Schema 版本不受支持"));
    }
    if (nextAccountSequence_ == 0 || nextDepositSequence_ == 0
        || nextTransactionSequence_ == 0) {
        return failValidation(errorMessage, QStringLiteral("编号序列必须大于零"));
    }

    QSet<QString> accountNumbers;
    QSet<QString> depositIds;
    QSet<QString> transactionIds;
    quint64 greatestAccountSequence = 0;
    quint64 greatestDepositSequence = 0;
    quint64 greatestTransactionSequence = 0;
    for (const Depositor &depositor : depositors_) {
        QString childError;
        if (!depositor.isValid(&childError)) {
            return failValidation(errorMessage, QStringLiteral("储户无效：%1").arg(childError));
        }
        if (accountNumbers.contains(depositor.accountNumber())) {
            return failValidation(errorMessage, QStringLiteral("账号重复"));
        }
        accountNumbers.insert(depositor.accountNumber());

        bool accountOk = false;
        const quint64 accountValue = depositor.accountNumber().toULongLong(&accountOk);
        constexpr quint64 accountNumberBase = 100000;
        if (!accountOk || accountValue <= accountNumberBase) {
            return failValidation(errorMessage, QStringLiteral("账号超出有效编号范围"));
        }
        greatestAccountSequence = qMax(greatestAccountSequence, accountValue - accountNumberBase);

        for (const FixedDeposit &deposit : depositor.deposits()) {
            if (depositIds.contains(deposit.depositId())) {
                return failValidation(errorMessage, QStringLiteral("存款编号全局重复"));
            }
            depositIds.insert(deposit.depositId());
            const auto sequence = numericSuffix(deposit.depositId(), 2);
            if (!sequence || *sequence == 0) {
                return failValidation(errorMessage, QStringLiteral("存款编号序列无效"));
            }
            greatestDepositSequence = qMax(greatestDepositSequence, *sequence);
        }
        for (const Transaction &transaction : depositor.transactions()) {
            if (transactionIds.contains(transaction.transactionId())) {
                return failValidation(errorMessage, QStringLiteral("交易编号全局重复"));
            }
            transactionIds.insert(transaction.transactionId());
            const auto sequence = numericSuffix(transaction.transactionId(), 2);
            if (!sequence || *sequence == 0) {
                return failValidation(errorMessage, QStringLiteral("交易编号序列无效"));
            }
            greatestTransactionSequence = qMax(greatestTransactionSequence, *sequence);
        }
    }

    // 下一个序列必须严格大于已使用编号，否则重启后会生成重复编号。
    if (nextAccountSequence_ <= greatestAccountSequence
        || nextDepositSequence_ <= greatestDepositSequence
        || nextTransactionSequence_ <= greatestTransactionSequence) {
        return failValidation(errorMessage, QStringLiteral("下一个编号序列未超过已使用编号"));
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace bank

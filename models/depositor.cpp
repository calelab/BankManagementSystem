#include "models/depositor.h"

#include <QRegularExpression>
#include <QSet>

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

Depositor::Depositor(QString accountNumber,
                     QString name,
                     QByteArray passwordSalt,
                     QByteArray passwordHash,
                     int passwordKdfIterations,
                     QString address,
                     bool lost,
                     std::optional<QDate> lostDate,
                     QString openingEmployeeId,
                     QDateTime createdAt,
                     QVector<FixedDeposit> deposits,
                     QVector<Transaction> transactions,
                     QString passwordKdfAlgorithm)
    : accountNumber_(std::move(accountNumber))
    , name_(std::move(name))
    , passwordSalt_(std::move(passwordSalt))
    , passwordHash_(std::move(passwordHash))
    , passwordKdfIterations_(passwordKdfIterations)
    , passwordKdfAlgorithm_(std::move(passwordKdfAlgorithm))
    , address_(std::move(address))
    , lost_(lost)
    , lostDate_(std::move(lostDate))
    , openingEmployeeId_(std::move(openingEmployeeId))
    , createdAt_(std::move(createdAt))
    , deposits_(std::move(deposits))
    , transactions_(std::move(transactions))
{
}

QString Depositor::supportedPasswordKdfAlgorithm()
{
    return QStringLiteral("PBKDF2-HMAC-SHA256");
}

const QString &Depositor::accountNumber() const
{
    return accountNumber_;
}

const QString &Depositor::name() const
{
    return name_;
}

const QByteArray &Depositor::passwordSalt() const
{
    return passwordSalt_;
}

const QByteArray &Depositor::passwordHash() const
{
    return passwordHash_;
}

int Depositor::passwordKdfIterations() const
{
    return passwordKdfIterations_;
}

const QString &Depositor::passwordKdfAlgorithm() const
{
    return passwordKdfAlgorithm_;
}

const QString &Depositor::address() const
{
    return address_;
}

bool Depositor::isLost() const
{
    return lost_;
}

const std::optional<QDate> &Depositor::lostDate() const
{
    return lostDate_;
}

const QString &Depositor::openingEmployeeId() const
{
    return openingEmployeeId_;
}

const QDateTime &Depositor::createdAt() const
{
    return createdAt_;
}

const QVector<FixedDeposit> &Depositor::deposits() const
{
    return deposits_;
}

const QVector<Transaction> &Depositor::transactions() const
{
    return transactions_;
}

bool Depositor::updateProfile(QString name, QString address, QString *errorMessage)
{
    name = name.trimmed();
    address = address.trimmed();
    if (name.isEmpty()) {
        return failValidation(errorMessage, QStringLiteral("姓名不能为空"));
    }
    if (address.isEmpty()) {
        return failValidation(errorMessage, QStringLiteral("地址不能为空"));
    }
    name_ = std::move(name);
    address_ = std::move(address);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool Depositor::replacePasswordCredentials(QByteArray salt,
                                           QByteArray hash,
                                           int iterations,
                                           QString algorithm,
                                           QString *errorMessage)
{
    if (salt.size() != 16 || hash.size() != 32 || iterations != 210000
        || algorithm != supportedPasswordKdfAlgorithm()) {
        return failValidation(errorMessage, QStringLiteral("密码派生信息无效"));
    }
    passwordSalt_ = std::move(salt);
    passwordHash_ = std::move(hash);
    passwordKdfIterations_ = iterations;
    passwordKdfAlgorithm_ = std::move(algorithm);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool Depositor::reportLoss(const QDate &date, QString *errorMessage)
{
    if (lost_) {
        return failValidation(errorMessage, QStringLiteral("账户已经挂失"));
    }
    if (!date.isValid()) {
        return failValidation(errorMessage, QStringLiteral("挂失日期无效"));
    }
    lost_ = true;
    lostDate_ = date;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void Depositor::clearLoss()
{
    lost_ = false;
    lostDate_.reset();
}

bool Depositor::addDeposit(const FixedDeposit &deposit, QString *errorMessage)
{
    QString childError;
    if (!deposit.isValid(&childError)) {
        return failValidation(errorMessage, QStringLiteral("存款无效：%1").arg(childError));
    }
    if (findDeposit(deposit.depositId())) {
        return failValidation(errorMessage, QStringLiteral("存款编号重复"));
    }
    deposits_.append(deposit);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool Depositor::addTransaction(const Transaction &transaction, QString *errorMessage)
{
    QString childError;
    if (!transaction.isValid(&childError)) {
        return failValidation(errorMessage, QStringLiteral("交易无效：%1").arg(childError));
    }
    if (transaction.accountNumber() != accountNumber_) {
        return failValidation(errorMessage, QStringLiteral("交易账号与储户不一致"));
    }
    if (!findDeposit(transaction.depositId())) {
        return failValidation(errorMessage, QStringLiteral("交易关联的存款不存在"));
    }
    for (const Transaction &existing : transactions_) {
        if (existing.transactionId() == transaction.transactionId()) {
            return failValidation(errorMessage, QStringLiteral("交易编号重复"));
        }
    }
    transactions_.append(transaction);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

FixedDeposit *Depositor::findDeposit(const QString &depositId)
{
    for (FixedDeposit &deposit : deposits_) {
        if (deposit.depositId() == depositId) {
            return &deposit;
        }
    }
    return nullptr;
}

const FixedDeposit *Depositor::findDeposit(const QString &depositId) const
{
    for (const FixedDeposit &deposit : deposits_) {
        if (deposit.depositId() == depositId) {
            return &deposit;
        }
    }
    return nullptr;
}

bool Depositor::isValid(QString *errorMessage) const
{
    static const QRegularExpression accountPattern(QStringLiteral("^[0-9]+$"));
    static const QRegularExpression employeeIdPattern(QStringLiteral("^E(?:0[1-9]|[1-9][0-9])$"));

    if (!accountPattern.match(accountNumber_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("账号格式无效"));
    }
    if (name_.trimmed().isEmpty()) {
        return failValidation(errorMessage, QStringLiteral("姓名不能为空"));
    }
    if (address_.trimmed().isEmpty()) {
        return failValidation(errorMessage, QStringLiteral("地址不能为空"));
    }
    if (passwordSalt_.size() != 16 || passwordHash_.size() != 32
        || passwordKdfIterations_ != 210000
        || passwordKdfAlgorithm_ != supportedPasswordKdfAlgorithm()) {
        return failValidation(errorMessage, QStringLiteral("密码派生信息不完整"));
    }
    if (lost_ != lostDate_.has_value()) {
        return failValidation(errorMessage, QStringLiteral("挂失状态与挂失日期不一致"));
    }
    if (lostDate_ && !lostDate_->isValid()) {
        return failValidation(errorMessage, QStringLiteral("挂失日期无效"));
    }
    if (!employeeIdPattern.match(openingEmployeeId_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("开户营业员工号格式无效"));
    }
    if (!createdAt_.isValid()) {
        return failValidation(errorMessage, QStringLiteral("开户时间无效"));
    }

    QSet<QString> depositIds;
    for (const FixedDeposit &deposit : deposits_) {
        QString childError;
        if (!deposit.isValid(&childError)) {
            return failValidation(errorMessage, QStringLiteral("存款无效：%1").arg(childError));
        }
        if (depositIds.contains(deposit.depositId())) {
            return failValidation(errorMessage, QStringLiteral("存款编号重复"));
        }
        depositIds.insert(deposit.depositId());
    }

    QSet<QString> transactionIds;
    for (const Transaction &transaction : transactions_) {
        QString childError;
        if (!transaction.isValid(&childError)) {
            return failValidation(errorMessage, QStringLiteral("交易无效：%1").arg(childError));
        }
        if (transaction.accountNumber() != accountNumber_
            || !depositIds.contains(transaction.depositId())) {
            return failValidation(errorMessage, QStringLiteral("交易与账户或存款的关联无效"));
        }
        if (transactionIds.contains(transaction.transactionId())) {
            return failValidation(errorMessage, QStringLiteral("交易编号重复"));
        }
        transactionIds.insert(transaction.transactionId());
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace bank

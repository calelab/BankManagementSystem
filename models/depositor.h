#ifndef DEPOSITOR_H
#define DEPOSITOR_H

#include "models/fixeddeposit.h"
#include "models/transaction.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <optional>

namespace bank {

// Depositor 是个人账户聚合，统一拥有该账户的多笔存款和完整交易记录。
class Depositor
{
public:
    Depositor() = default;
    Depositor(QString accountNumber,
              QString name,
              QByteArray passwordSalt,
              QByteArray passwordHash,
              int passwordKdfIterations,
              QString address,
              bool lost,
              std::optional<QDate> lostDate,
              QString openingEmployeeId,
              QDateTime createdAt,
              QVector<FixedDeposit> deposits = {},
              QVector<Transaction> transactions = {},
              QString passwordKdfAlgorithm = QStringLiteral("PBKDF2-HMAC-SHA256"));

    static QString supportedPasswordKdfAlgorithm();

    const QString &accountNumber() const;
    const QString &name() const;
    const QByteArray &passwordSalt() const;
    const QByteArray &passwordHash() const;
    int passwordKdfIterations() const;
    const QString &passwordKdfAlgorithm() const;
    const QString &address() const;
    bool isLost() const;
    const std::optional<QDate> &lostDate() const;
    const QString &openingEmployeeId() const;
    const QDateTime &createdAt() const;
    const QVector<FixedDeposit> &deposits() const;
    const QVector<Transaction> &transactions() const;

    // 姓名和地址经过去空白校验后一起更新，失败时两个字段都保持原值。
    bool updateProfile(QString name, QString address, QString *errorMessage = nullptr);

    // 密码变更必须一次替换 Salt、派生结果、迭代次数和算法标识。
    bool replacePasswordCredentials(QByteArray salt,
                                    QByteArray hash,
                                    int iterations,
                                    QString algorithm,
                                    QString *errorMessage = nullptr);

    // 挂失日期必须有效且账户尚未挂失，失败时不改变状态。
    bool reportLoss(const QDate &date, QString *errorMessage = nullptr);

    // 解除挂失时同步清空日期，维持状态字段的一致性。
    void clearLoss();

    // 聚合内新增存款时检查编号重复，失败时不修改集合。
    bool addDeposit(const FixedDeposit &deposit, QString *errorMessage = nullptr);

    // 交易必须属于当前账户并关联已存在的存款。
    bool addTransaction(const Transaction &transaction, QString *errorMessage = nullptr);

    FixedDeposit *findDeposit(const QString &depositId);
    const FixedDeposit *findDeposit(const QString &depositId) const;

    // 检查账户字段、挂失状态以及子对象的完整性和编号唯一性。
    bool isValid(QString *errorMessage = nullptr) const;

private:
    QString accountNumber_;
    QString name_;
    QByteArray passwordSalt_;
    QByteArray passwordHash_;
    int passwordKdfIterations_ = 0;
    QString passwordKdfAlgorithm_ = QStringLiteral("PBKDF2-HMAC-SHA256");
    QString address_;
    bool lost_ = false;
    std::optional<QDate> lostDate_;
    QString openingEmployeeId_;
    QDateTime createdAt_;
    QVector<FixedDeposit> deposits_;
    QVector<Transaction> transactions_;
};

} // namespace bank

#endif // DEPOSITOR_H

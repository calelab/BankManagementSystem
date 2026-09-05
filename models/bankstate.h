// 银行状态聚合声明：集中保存储户集合和跨重启唯一的编号序列。
#ifndef BANKSTATE_H
#define BANKSTATE_H

#include "models/depositor.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace bank {

// BankState 是核心数据的聚合根，并持久保存所有永不回收的编号序列。
class BankState
{
public:
    static constexpr int CurrentSchemaVersion = 1;

    BankState() = default;
    BankState(int schemaVersion,
              quint64 nextAccountSequence,
              quint64 nextDepositSequence,
              quint64 nextTransactionSequence,
              QVector<Depositor> depositors = {});

    int schemaVersion() const;
    quint64 nextAccountSequence() const;
    quint64 nextDepositSequence() const;
    quint64 nextTransactionSequence() const;
    const QVector<Depositor> &depositors() const;

    // 生成编号后立即推进序列；返回空字符串表示序列已无法安全表示。
    QString issueAccountNumber();
    QString issueDepositId();
    QString issueTransactionId();

    // 新增储户时保证账号及其子对象的全局编号均不重复。
    bool addDepositor(const Depositor &depositor, QString *errorMessage = nullptr);

    Depositor *findDepositor(const QString &accountNumber);
    const Depositor *findDepositor(const QString &accountNumber) const;

    // 验证 Schema、序列号、账号以及存款和交易编号的全局唯一性。
    bool isValid(QString *errorMessage = nullptr) const;

private:
    int schemaVersion_ = CurrentSchemaVersion;
    // 三类序列随数据持久化且只递增，防止重启后重新使用历史编号。
    quint64 nextAccountSequence_ = 1;
    quint64 nextDepositSequence_ = 1;
    quint64 nextTransactionSequence_ = 1;
    // 储户是顶层聚合；每个储户内部再拥有多笔存款和对应流水。
    QVector<Depositor> depositors_;
};

} // namespace bank

#endif // BANKSTATE_H

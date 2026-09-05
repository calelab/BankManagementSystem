// 定期存款模型：描述一笔本金独立、期限和利率锁定的存款合同。
#ifndef FIXEDDEPOSIT_H
#define FIXEDDEPOSIT_H

#include "models/banktypes.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace bank {

// FixedDeposit 表示账户中的一笔独立定期存款，不负责跨存款凑款或执行支取业务。
class FixedDeposit
{
public:
    FixedDeposit() = default;
    FixedDeposit(QString depositId,
                 qint64 originalPrincipalCents,
                 qint64 remainingPrincipalCents,
                 QDate startDate,
                 DepositTerm term,
                 int annualRateBasisPoints,
                 QDate maturityDate,
                 QString openingEmployeeId,
                 QDateTime createdAt);

    const QString &depositId() const;
    qint64 originalPrincipalCents() const;
    qint64 remainingPrincipalCents() const;
    const QDate &startDate() const;
    DepositTerm term() const;
    int annualRateBasisPoints() const;
    const QDate &maturityDate() const;
    const QString &openingEmployeeId() const;
    const QDateTime &createdAt() const;

    // 只允许在原始本金范围内调整剩余本金；失败时对象保持不变。
    bool setRemainingPrincipalCents(qint64 cents);

    // 按给定日期和剩余本金推导状态，结清状态优先于日期判断。
    DepositStatus statusOn(const QDate &referenceDate) const;

    // 检查编号、金额、期限、利率和日期等持久化边界约束。
    bool isValid(QString *errorMessage = nullptr) const;

    static QDate calculateMaturityDate(const QDate &startDate, DepositTerm term);

private:
    QString depositId_;
    // original 在开户后保持不变；remaining 随部分或全部支取递减。
    qint64 originalPrincipalCents_ = 0;
    qint64 remainingPrincipalCents_ = 0;
    QDate startDate_;
    DepositTerm term_ = DepositTerm::OneYear;
    // 利率在存入时锁定，到期日由存入日和期限推导，加载时会重新核对。
    int annualRateBasisPoints_ = 0;
    QDate maturityDate_;
    QString openingEmployeeId_;
    QDateTime createdAt_;
};

} // namespace bank

#endif // FIXEDDEPOSIT_H

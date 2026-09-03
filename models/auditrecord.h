#ifndef AUDITRECORD_H
#define AUDITRECORD_H

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace bank {

enum class AuditResult {
    Success,
    Failure,
    Warning
};

// AuditRecord 只保存可追踪的业务事实，严禁包含密码、哈希、Salt 或主密钥。
class AuditRecord
{
public:
    AuditRecord() = default;
    AuditRecord(QDateTime dateTime,
                QString employeeId,
                QString accountNumber,
                QString action,
                qint64 principalAmountCents,
                qint64 interestAmountCents,
                AuditResult result,
                QString reasonCode);

    const QDateTime &dateTime() const;
    const QString &employeeId() const;
    const QString &accountNumber() const;
    const QString &action() const;
    qint64 principalAmountCents() const;
    qint64 interestAmountCents() const;
    AuditResult result() const;
    const QString &reasonCode() const;

    // 校验工号、可选账号、稳定动作码、金额和结果码等日志边界。
    bool isValid(QString *errorMessage = nullptr) const;

private:
    QDateTime dateTime_;
    QString employeeId_;
    QString accountNumber_;
    QString action_;
    qint64 principalAmountCents_ = 0;
    qint64 interestAmountCents_ = 0;
    AuditResult result_ = AuditResult::Success;
    QString reasonCode_;
};

} // namespace bank

#endif // AUDITRECORD_H

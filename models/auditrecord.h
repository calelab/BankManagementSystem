// 审计记录模型：描述可持久化的业务操作结果及其领域约束。
#ifndef AUDITRECORD_H
#define AUDITRECORD_H

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace bank {

enum class AuditResult {
    Success,
    Failure,
    // 核心业务成功但审计等附属步骤异常时使用警告，不伪装成业务失败。
    Warning
};

// AuditRecord 只保存可追踪的业务事实，严禁包含密码、哈希或 Salt。
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
    // 动作与原因使用稳定英文机器码持久化，中文仅在 UI 展示层映射。
    QString action_;
    qint64 principalAmountCents_ = 0;
    qint64 interestAmountCents_ = 0;
    AuditResult result_ = AuditResult::Success;
    QString reasonCode_;
};

} // namespace bank

#endif // AUDITRECORD_H

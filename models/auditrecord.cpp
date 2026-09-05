// 审计记录实现：校验日志字段格式，防止无效记录进入审计文件。
#include "models/auditrecord.h"

#include <QRegularExpression>

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

AuditRecord::AuditRecord(QDateTime dateTime,
                         QString employeeId,
                         QString accountNumber,
                         QString action,
                         qint64 principalAmountCents,
                         qint64 interestAmountCents,
                         AuditResult result,
                         QString reasonCode)
    : dateTime_(std::move(dateTime))
    , employeeId_(std::move(employeeId))
    , accountNumber_(std::move(accountNumber))
    , action_(std::move(action))
    , principalAmountCents_(principalAmountCents)
    , interestAmountCents_(interestAmountCents)
    , result_(result)
    , reasonCode_(std::move(reasonCode))
{
}

const QDateTime &AuditRecord::dateTime() const
{
    return dateTime_;
}

const QString &AuditRecord::employeeId() const
{
    return employeeId_;
}

const QString &AuditRecord::accountNumber() const
{
    return accountNumber_;
}

const QString &AuditRecord::action() const
{
    return action_;
}

qint64 AuditRecord::principalAmountCents() const
{
    return principalAmountCents_;
}

qint64 AuditRecord::interestAmountCents() const
{
    return interestAmountCents_;
}

AuditResult AuditRecord::result() const
{
    return result_;
}

const QString &AuditRecord::reasonCode() const
{
    return reasonCode_;
}

bool AuditRecord::isValid(QString *errorMessage) const
{
    // 稳定机器码和字段边界使日志可筛选，也能在加载时拒绝被污染的记录。
    static const QRegularExpression employeePattern(QStringLiteral("^E(?:0[1-9]|[1-9][0-9])$"));
    static const QRegularExpression accountPattern(QStringLiteral("^[0-9]+$"));
    static const QRegularExpression codePattern(QStringLiteral("^[A-Z][A-Z0-9_]*$"));

    if (!dateTime_.isValid()) {
        return failValidation(errorMessage, QStringLiteral("审计时间无效"));
    }
    if (!employeePattern.match(employeeId_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("审计营业员工号无效"));
    }
    if (!accountNumber_.isEmpty() && !accountPattern.match(accountNumber_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("审计账号格式无效"));
    }
    if (!codePattern.match(action_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("审计动作码无效"));
    }
    if (principalAmountCents_ < 0 || interestAmountCents_ < 0) {
        return failValidation(errorMessage, QStringLiteral("审计金额不能为负数"));
    }
    if (result_ != AuditResult::Success && result_ != AuditResult::Failure
        && result_ != AuditResult::Warning) {
        return failValidation(errorMessage, QStringLiteral("审计结果码无效"));
    }
    if (!codePattern.match(reasonCode_).hasMatch()) {
        return failValidation(errorMessage, QStringLiteral("审计原因码无效"));
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace bank

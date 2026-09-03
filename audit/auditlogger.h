#ifndef AUDITLOGGER_H
#define AUDITLOGGER_H

#include "models/auditrecord.h"
#include "persistence/datacodec.h"

#include <QString>
#include <QVector>

#include <memory>

namespace bank::audit {

struct AuditLoadResult {
    bool success = false;
    QVector<AuditRecord> records;
    QString errorMessage;
};

struct AuditAppendResult {
    bool success = false;
    QString errorMessage;
};

// AuditLogger 按营业员保存独立日志，并通过共享编码器为后续 AES 模式保留边界。
class AuditLogger
{
public:
    AuditLogger(QString dataDirectory,
                std::shared_ptr<const persistence::DataCodec> codec = nullptr);

    QString filePathForEmployee(const QString &employeeId) const;

    // 日志不存在时返回空集合；损坏、版本错误或工号混用时拒绝加载。
    AuditLoadResult loadForEmployee(const QString &employeeId) const;

    // 读取完整日志、追加记录并用 QSaveFile 原子替换，失败时保留旧日志。
    AuditAppendResult append(const AuditRecord &record) const;

private:
    bool ensureAuditDirectory(QString *errorMessage) const;
    bool serialize(const QVector<AuditRecord> &records,
                   QByteArray *plainJson,
                   QString *errorMessage) const;
    bool deserialize(const QByteArray &plainJson,
                     const QString &expectedEmployeeId,
                     QVector<AuditRecord> *records,
                     QString *errorMessage) const;

    QString dataDirectory_;
    std::shared_ptr<const persistence::DataCodec> codec_;
};

} // namespace bank::audit

#endif // AUDITLOGGER_H

// 审计日志实现：严格校验日志对象，并以 JSON 原子更新独立文件。
#include "audit/auditlogger.h"

#include "persistence/employeefilemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

#include <limits>
#include <utility>

namespace bank::audit {
namespace {

constexpr int AuditSchemaVersion = 1;

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

QString auditResultToString(AuditResult result)
{
    switch (result) {
    case AuditResult::Success:
        return QStringLiteral("SUCCESS");
    case AuditResult::Failure:
        return QStringLiteral("FAILURE");
    case AuditResult::Warning:
        return QStringLiteral("WARNING");
    }
    return {};
}

bool auditResultFromString(const QString &text, AuditResult *result)
{
    if (text == QStringLiteral("SUCCESS")) {
        *result = AuditResult::Success;
        return true;
    }
    if (text == QStringLiteral("FAILURE")) {
        *result = AuditResult::Failure;
        return true;
    }
    if (text == QStringLiteral("WARNING")) {
        *result = AuditResult::Warning;
        return true;
    }
    return false;
}

bool readString(const QJsonObject &object,
                const QString &key,
                QString *value,
                QString *errorMessage)
{
    const auto iterator = object.constFind(key);
    if (iterator == object.constEnd() || !iterator->isString()) {
        return fail(errorMessage, QStringLiteral("审计字段 %1 缺失或类型错误").arg(key));
    }
    *value = iterator->toString();
    return true;
}

bool readCents(const QJsonObject &object,
               const QString &key,
               qint64 *value,
               QString *errorMessage)
{
    QString text;
    static const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)$"));
    if (!readString(object, key, &text, errorMessage) || !pattern.match(text).hasMatch()) {
        return fail(errorMessage, QStringLiteral("审计金额字段 %1 无效").arg(key));
    }
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok);
    if (!ok || parsed > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
        return fail(errorMessage, QStringLiteral("审计金额字段 %1 超出范围").arg(key));
    }
    *value = static_cast<qint64>(parsed);
    return true;
}

QJsonObject recordToJson(const AuditRecord &record)
{
    return {
        {QStringLiteral("dateTime"), record.dateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("employeeId"), record.employeeId()},
        {QStringLiteral("accountNumber"), record.accountNumber()},
        {QStringLiteral("action"), record.action()},
        {QStringLiteral("principalAmountCents"), QString::number(record.principalAmountCents())},
        {QStringLiteral("interestAmountCents"), QString::number(record.interestAmountCents())},
        {QStringLiteral("result"), auditResultToString(record.result())},
        {QStringLiteral("reasonCode"), record.reasonCode()}
    };
}

bool recordFromJson(const QJsonValue &value,
                    const QString &expectedEmployeeId,
                    AuditRecord *record,
                    QString *errorMessage)
{
    if (!value.isObject()) {
        return fail(errorMessage, QStringLiteral("审计记录必须是对象"));
    }
    const QJsonObject object = value.toObject();
    QString dateTimeText;
    QString employeeId;
    QString accountNumber;
    QString action;
    qint64 principal = 0;
    qint64 interest = 0;
    QString resultText;
    QString reasonCode;
    if (!readString(object, QStringLiteral("dateTime"), &dateTimeText, errorMessage)
        || !readString(object, QStringLiteral("employeeId"), &employeeId, errorMessage)
        || !readString(object, QStringLiteral("accountNumber"), &accountNumber, errorMessage)
        || !readString(object, QStringLiteral("action"), &action, errorMessage)
        || !readCents(object, QStringLiteral("principalAmountCents"), &principal, errorMessage)
        || !readCents(object, QStringLiteral("interestAmountCents"), &interest, errorMessage)
        || !readString(object, QStringLiteral("result"), &resultText, errorMessage)
        || !readString(object, QStringLiteral("reasonCode"), &reasonCode, errorMessage)) {
        return false;
    }

    const QDateTime dateTime = QDateTime::fromString(dateTimeText, Qt::ISODateWithMs);
    if (!dateTime.isValid()) {
        return fail(errorMessage, QStringLiteral("审计日期时间无效"));
    }
    AuditResult auditResult = AuditResult::Failure;
    if (!auditResultFromString(resultText, &auditResult)) {
        return fail(errorMessage, QStringLiteral("审计结果码无效"));
    }
    // 文件名限定营业员，记录中的工号必须再次匹配，防止日志串户或被调包。
    if (employeeId != expectedEmployeeId) {
        return fail(errorMessage, QStringLiteral("审计文件混入其他营业员工号"));
    }

    AuditRecord candidate(dateTime,
                          employeeId,
                          accountNumber,
                          action,
                          principal,
                          interest,
                          auditResult,
                          reasonCode);
    QString validationError;
    if (!candidate.isValid(&validationError)) {
        return fail(errorMessage, QStringLiteral("审计记录无效：%1").arg(validationError));
    }
    *record = std::move(candidate);
    return true;
}

} // namespace

AuditLogger::AuditLogger(QString dataDirectory,
                         std::shared_ptr<const persistence::DataCodec> codec)
    : dataDirectory_(std::move(dataDirectory))
    , codec_(std::move(codec))
{
    if (!codec_) {
        codec_ = std::make_shared<persistence::PlainJsonCodec>();
    }
}

QString AuditLogger::filePathForEmployee(const QString &employeeId) const
{
    if (!persistence::EmployeeFileManager::isValidEmployeeId(employeeId)) {
        return {};
    }
    return QDir(dataDirectory_)
        .filePath(QStringLiteral("audit/%1%2").arg(employeeId, codec_->auditFileSuffix()));
}

AuditLoadResult AuditLogger::loadForEmployee(const QString &employeeId) const
{
    AuditLoadResult result;
    const QString path = filePathForEmployee(employeeId);
    if (path.isEmpty()) {
        result.errorMessage = QStringLiteral("营业员工号格式无效");
        return result;
    }
    // 尚无日志是正常空结果；已有文件一旦损坏则必须明确失败。
    if (!QFileInfo::exists(path)) {
        result.success = true;
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("无法读取审计日志：%1").arg(file.errorString());
        return result;
    }
    const QByteArray encodedData = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        result.errorMessage = QStringLiteral("读取审计日志失败：%1").arg(file.errorString());
        return result;
    }

    QByteArray plainJson;
    if (!codec_->decode(encodedData, &plainJson, &result.errorMessage)
        || !deserialize(plainJson, employeeId, &result.records, &result.errorMessage)) {
        return result;
    }
    result.success = true;
    return result;
}

AuditAppendResult AuditLogger::append(const AuditRecord &record) const
{
    AuditAppendResult result;
    QString validationError;
    if (!record.isValid(&validationError)) {
        result.errorMessage = QStringLiteral("审计记录无效：%1").arg(validationError);
        return result;
    }

    // 读取完整 JSON 日志、追加记录，再原子重写，避免留下不完整的日志。
    const AuditLoadResult existing = loadForEmployee(record.employeeId());
    if (!existing.success) {
        result.errorMessage = existing.errorMessage;
        return result;
    }
    QVector<AuditRecord> records = existing.records;
    records.append(record);

    QByteArray plainJson;
    QByteArray encodedData;
    if (!serialize(records, &plainJson, &result.errorMessage)
        || !codec_->encode(plainJson, &encodedData, &result.errorMessage)
        || !ensureAuditDirectory(&result.errorMessage)) {
        return result;
    }

    // 与核心数据相同，禁止直接覆盖回退，写入失败时保留上一版完整日志。
    QSaveFile file(filePathForEmployee(record.employeeId()));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.errorMessage = QStringLiteral("无法创建审计日志临时文件：%1").arg(file.errorString());
        return result;
    }
    if (file.write(encodedData) != encodedData.size()) {
        result.errorMessage = QStringLiteral("写入审计日志失败：%1").arg(file.errorString());
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) {
        result.errorMessage = QStringLiteral("原子替换审计日志失败：%1").arg(file.errorString());
        return result;
    }

    result.success = true;
    return result;
}

bool AuditLogger::ensureAuditDirectory(QString *errorMessage) const
{
    if (dataDirectory_.trimmed().isEmpty()) {
        return fail(errorMessage, QStringLiteral("数据目录不能为空"));
    }
    const QString auditDirectory = QDir(dataDirectory_).filePath(QStringLiteral("audit"));
    const QFileInfo info(auditDirectory);
    if (info.exists() && !info.isDir()) {
        return fail(errorMessage, QStringLiteral("审计目录路径不是文件夹"));
    }
    if (!info.exists() && !QDir().mkpath(auditDirectory)) {
        return fail(errorMessage, QStringLiteral("无法创建审计目录"));
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool AuditLogger::serialize(const QVector<AuditRecord> &records,
                            QByteArray *plainJson,
                            QString *errorMessage) const
{
    if (!plainJson) {
        return fail(errorMessage, QStringLiteral("审计 JSON 输出参数不能为空"));
    }
    QJsonArray array;
    for (const AuditRecord &record : records) {
        QString validationError;
        if (!record.isValid(&validationError)) {
            return fail(errorMessage, QStringLiteral("审计记录无效：%1").arg(validationError));
        }
        array.append(recordToJson(record));
    }
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), AuditSchemaVersion},
        {QStringLiteral("records"), array}
    };
    *plainJson = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool AuditLogger::deserialize(const QByteArray &plainJson,
                              const QString &expectedEmployeeId,
                              QVector<AuditRecord> *records,
                              QString *errorMessage) const
{
    if (!records) {
        return fail(errorMessage, QStringLiteral("审计记录输出参数不能为空"));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(plainJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(errorMessage, QStringLiteral("审计日志 JSON 损坏或格式无效"));
    }
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("schemaVersion"));
    if (!version.isDouble()
        || version.toDouble() != static_cast<double>(AuditSchemaVersion)) {
        return fail(errorMessage, QStringLiteral("审计日志 Schema 版本不受支持"));
    }
    const QJsonValue recordsValue = root.value(QStringLiteral("records"));
    if (!recordsValue.isArray()) {
        return fail(errorMessage, QStringLiteral("审计日志缺少 records 数组"));
    }

    // 解析到局部集合并逐条验证，任何坏记录都不会产生可见的半解析结果。
    QVector<AuditRecord> parsedRecords;
    const QJsonArray array = recordsValue.toArray();
    parsedRecords.reserve(array.size());
    for (const QJsonValue &value : array) {
        AuditRecord record;
        if (!recordFromJson(value, expectedEmployeeId, &record, errorMessage)) {
            return false;
        }
        parsedRecords.append(std::move(record));
    }
    *records = std::move(parsedRecords);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace bank::audit

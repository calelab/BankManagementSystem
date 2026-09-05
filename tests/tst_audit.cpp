// 审计测试：验证按营业员隔离、筛选、原子追加以及加密文件的失败保护。
#include "audit/auditlogger.h"
#include "persistence/datacodec.h"
#include "security/securityutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace bank;
using namespace bank::audit;
using namespace bank::persistence;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("无法读取审计测试文件：%s", qPrintable(file.errorString()));
    }
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qFatal("无法写入审计测试文件：%s", qPrintable(file.errorString()));
    }
    if (file.write(contents) != contents.size()) {
        qFatal("审计测试文件写入不完整");
    }
}

std::shared_ptr<const DataCodec> plainCodec()
{
    return std::make_shared<PlainJsonCodec>();
}

AuditRecord makeRecord(const QString &employeeId,
                       const QString &action = QStringLiteral("DEPOSIT"),
                       qint64 principalCents = 10000,
                       qint64 interestCents = 0,
                       AuditResult result = AuditResult::Success,
                       const QString &reasonCode = QStringLiteral("NONE"))
{
    return AuditRecord(QDateTime(QDate(2026, 1, 2), QTime(10, 30, 0, 125)),
                       employeeId,
                       QStringLiteral("100001"),
                       action,
                       principalCents,
                       interestCents,
                       result,
                       reasonCode);
}

class RejectingAuditCodec final : public DataCodec
{
public:
    bool rejectEncoding = false;

    QString fileName() const override
    {
        return QStringLiteral("bank_data.json");
    }

    QString auditFileSuffix() const override
    {
        return QStringLiteral(".audit.json");
    }

    QString displayName() const override
    {
        return QStringLiteral("审计故障测试编码器");
    }

    bool encode(const QByteArray &plainJson,
                QByteArray *encodedData,
                QString *errorMessage) const override
    {
        if (rejectEncoding) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("测试审计编码失败");
            }
            return false;
        }
        if (!encodedData) {
            return false;
        }
        *encodedData = plainJson;
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }

    bool decode(const QByteArray &encodedData,
                QByteArray *plainJson,
                QString *errorMessage) const override
    {
        if (!plainJson) {
            return false;
        }
        *plainJson = encodedData;
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
};

} // namespace

// 每个用例都在临时目录运行，避免读取或改写真实营业员审计数据。
class AuditLoggerTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesAuditRecordBoundary();
    void createsOnFirstEventAndRoundTrips();
    void isolatesEmployeeFiles();
    void rejectsDamagedAndUnsupportedSchema();
    void rejectsMixedEmployeeLogWithoutOverwrite();
    void preservesExistingLogWhenEncodingFails();
    void encryptsAuditAndKeepsCompatibilityLogSeparate();
};

void AuditLoggerTest::validatesAuditRecordBoundary()
{
    QString error;
    QVERIFY(makeRecord(QStringLiteral("E01")).isValid(&error));
    QVERIFY(error.isEmpty());
    QVERIFY(!makeRecord(QStringLiteral("E00")).isValid(&error));
    QVERIFY(!makeRecord(QStringLiteral("E01"), QStringLiteral("lower-case")).isValid(&error));
    QVERIFY(!makeRecord(QStringLiteral("E01"),
                        QStringLiteral("DEPOSIT"),
                        -1)
                 .isValid(&error));

    const AuditRecord invalidAccount(QDateTime(QDate(2026, 1, 2), QTime(10, 30)),
                                     QStringLiteral("E01"),
                                     QStringLiteral("A100001"),
                                     QStringLiteral("LOGIN"),
                                     0,
                                     0,
                                     AuditResult::Failure,
                                     QStringLiteral("AUTH_FAILED"));
    QVERIFY(!invalidAccount.isValid(&error));
}

void AuditLoggerTest::createsOnFirstEventAndRoundTrips()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AuditLogger logger(temporaryDirectory.path(), plainCodec());
    const QString path = logger.filePathForEmployee(QStringLiteral("E03"));

    const AuditLoadResult empty = logger.loadForEmployee(QStringLiteral("E03"));
    QVERIFY(empty.success);
    QVERIFY(empty.records.isEmpty());
    QVERIFY(!QFileInfo::exists(path));

    const AuditRecord deposited = makeRecord(QStringLiteral("E03"));
    QVERIFY2(logger.append(deposited).success, "首条审计记录应创建日志");
    QVERIFY(QFileInfo::exists(path));
    const AuditRecord withdrawn = makeRecord(QStringLiteral("E03"),
                                             QStringLiteral("MATURED_WITHDRAW"),
                                             4000,
                                             79);
    QVERIFY(logger.append(withdrawn).success);

    const AuditLoadResult loaded = logger.loadForEmployee(QStringLiteral("E03"));
    QVERIFY2(loaded.success, qPrintable(loaded.errorMessage));
    QCOMPARE(loaded.records.size(), 2);
    QCOMPARE(loaded.records.at(0).dateTime(), deposited.dateTime());
    QCOMPARE(loaded.records.at(0).principalAmountCents(), qint64(10000));
    QCOMPARE(loaded.records.at(1).action(), QStringLiteral("MATURED_WITHDRAW"));
    QCOMPARE(loaded.records.at(1).interestAmountCents(), qint64(79));

    const QByteArray diskContents = readFile(path).toLower();
    QVERIFY(!diskContents.contains("password"));
    QVERIFY(!diskContents.contains("salt"));
    QVERIFY(!diskContents.contains("hash"));
    QVERIFY(!diskContents.contains("master.key"));
}

void AuditLoggerTest::isolatesEmployeeFiles()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AuditLogger logger(temporaryDirectory.path(), plainCodec());
    QVERIFY(logger.append(makeRecord(QStringLiteral("E01"),
                                    QStringLiteral("OPEN_ACCOUNT")))
                .success);
    QVERIFY(logger.append(makeRecord(QStringLiteral("E02"),
                                    QStringLiteral("LOGIN")))
                .success);

    const AuditLoadResult employeeOne = logger.loadForEmployee(QStringLiteral("E01"));
    const AuditLoadResult employeeTwo = logger.loadForEmployee(QStringLiteral("E02"));
    QVERIFY(employeeOne.success);
    QVERIFY(employeeTwo.success);
    QCOMPARE(employeeOne.records.size(), 1);
    QCOMPARE(employeeTwo.records.size(), 1);
    QCOMPARE(employeeOne.records.first().employeeId(), QStringLiteral("E01"));
    QCOMPARE(employeeTwo.records.first().employeeId(), QStringLiteral("E02"));
    QVERIFY(logger.filePathForEmployee(QStringLiteral("E01"))
            != logger.filePathForEmployee(QStringLiteral("E02")));
}

void AuditLoggerTest::rejectsDamagedAndUnsupportedSchema()
{
    // 损坏 JSON 和未知 Schema 都必须明确拒绝，不能当作“尚无日志”。
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AuditLogger logger(temporaryDirectory.path(), plainCodec());
    QVERIFY(QDir().mkpath(QDir(temporaryDirectory.path()).filePath(QStringLiteral("audit"))));
    const QString path = logger.filePathForEmployee(QStringLiteral("E06"));

    const QByteArray damaged("{broken-json");
    writeFile(path, damaged);
    const AuditLoadResult damagedResult = logger.loadForEmployee(QStringLiteral("E06"));
    QVERIFY(!damagedResult.success);
    QVERIFY(damagedResult.errorMessage.contains(QStringLiteral("JSON")));
    QVERIFY(!logger.append(makeRecord(QStringLiteral("E06"))).success);
    QCOMPARE(readFile(path), damaged);

    const QByteArray unsupported("{\"schemaVersion\":2,\"records\":[]}");
    writeFile(path, unsupported);
    const AuditLoadResult unsupportedResult = logger.loadForEmployee(QStringLiteral("E06"));
    QVERIFY(!unsupportedResult.success);
    QVERIFY(unsupportedResult.errorMessage.contains(QStringLiteral("Schema")));
    QCOMPARE(readFile(path), unsupported);
}

void AuditLoggerTest::rejectsMixedEmployeeLogWithoutOverwrite()
{
    // 文件中混入其他营业员工号时，读取和后续追加都不能覆盖证据。
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AuditLogger logger(temporaryDirectory.path(), plainCodec());
    QVERIFY(logger.append(makeRecord(QStringLiteral("E01"))).success);
    const QString path = logger.filePathForEmployee(QStringLiteral("E01"));
    QByteArray mixedContents = readFile(path);
    mixedContents.replace("\"employeeId\": \"E01\"", "\"employeeId\": \"E02\"");
    writeFile(path, mixedContents);

    const AuditLoadResult loaded = logger.loadForEmployee(QStringLiteral("E01"));
    QVERIFY(!loaded.success);
    QVERIFY(loaded.errorMessage.contains(QStringLiteral("其他营业员")));
    const QByteArray beforeAppend = readFile(path);
    QVERIFY(!logger.append(makeRecord(QStringLiteral("E01"))).success);
    QCOMPARE(readFile(path), beforeAppend);
}

void AuditLoggerTest::preservesExistingLogWhenEncodingFails()
{
    // 编码故障发生后仍应保留上一版日志，验证读改写的原子性。
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const auto codec = std::make_shared<RejectingAuditCodec>();
    AuditLogger logger(temporaryDirectory.path(), codec);
    QVERIFY(logger.append(makeRecord(QStringLiteral("E05"))).success);
    const QString path = logger.filePathForEmployee(QStringLiteral("E05"));
    const QByteArray original = readFile(path);

    codec->rejectEncoding = true;
    const AuditAppendResult result = logger.append(
        makeRecord(QStringLiteral("E05"), QStringLiteral("REPORT_LOSS")));
    QVERIFY(!result.success);
    QCOMPARE(readFile(path), original);
}

void AuditLoggerTest::encryptsAuditAndKeepsCompatibilityLogSeparate()
{
    // 综合覆盖加密日志、兼容文件隔离、篡改认证和主密钥缺失保护。
#ifndef BANK_HAS_OPENSSL
    QSKIP("当前是无 OpenSSL 的明文兼容构建", nullptr);
#else
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AuditLogger encryptedLogger(temporaryDirectory.path());
    const AuditRecord first = makeRecord(QStringLiteral("E03"));
    QVERIFY(encryptedLogger.append(first).success);
    const QString encryptedPath = encryptedLogger.filePathForEmployee(QStringLiteral("E03"));
    QVERIFY(encryptedPath.endsWith(QStringLiteral(".audit.enc")));
    const QByteArray encryptedBeforeCompatibility = readFile(encryptedPath);
    QVERIFY(!encryptedBeforeCompatibility.contains("DEPOSIT"));
    QVERIFY(!encryptedBeforeCompatibility.contains("100001"));

    const AuditLoadResult restored = AuditLogger(temporaryDirectory.path())
                                         .loadForEmployee(QStringLiteral("E03"));
    QVERIFY2(restored.success, qPrintable(restored.errorMessage));
    QCOMPARE(restored.records.size(), 1);
    QCOMPARE(restored.records.first().action(), QStringLiteral("DEPOSIT"));

    AuditLogger compatibilityLogger(temporaryDirectory.path(), plainCodec());
    QVERIFY(compatibilityLogger.append(
                makeRecord(QStringLiteral("E03"), QStringLiteral("OPEN_ACCOUNT")))
                .success);
    const QString compatibilityPath = compatibilityLogger.filePathForEmployee(
        QStringLiteral("E03"));
    QVERIFY(compatibilityPath.endsWith(QStringLiteral(".audit.json")));
    QVERIFY(compatibilityPath != encryptedPath);
    QCOMPARE(readFile(encryptedPath), encryptedBeforeCompatibility);

    const QByteArray compatibilityBeforeEncryptedAppend = readFile(compatibilityPath);
    QVERIFY(encryptedLogger.append(
                makeRecord(QStringLiteral("E03"), QStringLiteral("REPORT_LOSS")))
                .success);
    QCOMPARE(readFile(compatibilityPath), compatibilityBeforeEncryptedAppend);

    QByteArray tampered = readFile(encryptedPath);
    tampered[tampered.size() - 1] = static_cast<char>(tampered.back() ^ 0x01);
    writeFile(encryptedPath, tampered);
    const AuditLoadResult damaged = AuditLogger(temporaryDirectory.path())
                                        .loadForEmployee(QStringLiteral("E03"));
    QVERIFY(!damaged.success);
    QVERIFY(damaged.errorMessage.contains(QStringLiteral("认证失败")));
    QVERIFY(!AuditLogger(temporaryDirectory.path()).append(first).success);
    QCOMPARE(readFile(encryptedPath), tampered);

    const QString keyPath = security::SecurityUtils::masterKeyPath(
        temporaryDirectory.path());
    QVERIFY(QFile::remove(keyPath));
    const AuditLoadResult missingKey = AuditLogger(temporaryDirectory.path())
                                           .loadForEmployee(QStringLiteral("E03"));
    QVERIFY(!missingKey.success);
    QVERIFY(missingKey.errorMessage.contains(QStringLiteral("主密钥缺失")));
    QVERIFY(!QFileInfo::exists(keyPath));
#endif
}

int main(int argc, char *argv[])
{
    AuditLoggerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_audit.moc"

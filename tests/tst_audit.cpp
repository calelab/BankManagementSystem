#include "audit/auditlogger.h"
#include "persistence/datacodec.h"

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
    AuditLogger logger(temporaryDirectory.path());
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
    AuditLogger logger(temporaryDirectory.path());
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
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AuditLogger logger(temporaryDirectory.path());
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
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AuditLogger logger(temporaryDirectory.path());
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

int main(int argc, char *argv[])
{
    AuditLoggerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_audit.moc"

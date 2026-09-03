#include "models/bankstate.h"
#include "persistence/bankstatejsonserializer.h"
#include "persistence/datacodec.h"
#include "persistence/filemanager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <limits>
#include <optional>

using namespace bank;
using namespace bank::persistence;

namespace {

BankState makeState()
{
    const QDate startDate(2026, 3, 2);
    const FixedDeposit deposit(QStringLiteral("FD000001"),
                               1000050,
                               750050,
                               startDate,
                               DepositTerm::ThreeYears,
                               annualRateBasisPoints(DepositTerm::ThreeYears),
                               startDate.addYears(3),
                               QStringLiteral("E03"),
                               QDateTime(startDate, QTime(8, 30, 0, 125)));
    const Transaction depositTransaction(
        QStringLiteral("TX000001"),
        QStringLiteral("100001"),
        QStringLiteral("FD000001"),
        QDateTime(startDate, QTime(8, 31, 0, 250)),
        TransactionType::Deposit,
        WithdrawalKind::None,
        1000050,
        0,
        QStringLiteral("E03"));
    const Transaction withdrawalTransaction(
        QStringLiteral("TX000002"),
        QStringLiteral("100001"),
        QStringLiteral("FD000001"),
        QDateTime(QDate(2026, 4, 2), QTime(10, 0, 0, 375)),
        TransactionType::Withdrawal,
        WithdrawalKind::Early,
        250000,
        106,
        QStringLiteral("E04"));
    const Depositor depositor(QStringLiteral("100001"),
                              QStringLiteral("张三"),
                              QByteArray::fromHex("00112233445566778899aabbccddeeff"),
                              QByteArray(32, 'h'),
                              210000,
                              QStringLiteral("上海市浦东新区"),
                              true,
                              QDate(2026, 5, 1),
                              QStringLiteral("E03"),
                              QDateTime(startDate, QTime(8, 0, 0, 500)),
                              {deposit},
                              {depositTransaction, withdrawalTransaction});
    return BankState(BankState::CurrentSchemaVersion, 2, 2, 3, {depositor});
}

QByteArray serializeState(const BankState &state)
{
    QByteArray json;
    QString error;
    const bool success = BankStateJsonSerializer::serialize(state, &json, &error);
    if (!success) {
        qFatal("测试夹具序列化失败：%s", qPrintable(error));
    }
    return json;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("无法读取测试文件：%s", qPrintable(file.errorString()));
    }
    return file.readAll();
}

// 该测试编码器模拟未来加密层在编码阶段失败，用于验证旧文件不会被覆盖。
class RejectingCodec final : public DataCodec
{
public:
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
        return QStringLiteral("故障注入编码器");
    }

    bool encode(const QByteArray &, QByteArray *, QString *errorMessage) const override
    {
        if (errorMessage) {
            *errorMessage = QStringLiteral("测试编码失败");
        }
        return false;
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

// 持久化测试全部使用临时目录，不会接触用户真实的应用数据目录。
class PersistenceTest : public QObject
{
    Q_OBJECT

private slots:
    void serializesVersionedJsonLosslessly();
    void preservesFullWidthIntegerPrecision();
    void savesAndLoadsThroughInjectedDirectory();
    void initializesEmptyStateWhenFileIsMissing();
    void rejectsMalformedOrUnsupportedJson_data();
    void rejectsMalformedOrUnsupportedJson();
    void rejectsBrokenDomainConstraintsWithoutChangingOutput();
    void rejectsDuplicateGlobalIdentifiers();
    void rejectsInvalidDiskDataWithoutOverwritingIt();
    void rejectsInvalidStateBeforeCreatingFile();
    void codecFailurePreservesExistingFileAndMemory();
    void atomicWriteFailurePreservesExistingFile();
    void rejectsDataDirectoryThatIsAFile();
};

void PersistenceTest::serializesVersionedJsonLosslessly()
{
    const BankState original = makeState();
    const QByteArray json = serializeState(original);
    const QJsonObject root = QJsonDocument::fromJson(json).object();

    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(),
             BankState::CurrentSchemaVersion);
    QVERIFY(root.value(QStringLiteral("nextAccountSequence")).isString());
    QCOMPARE(root.value(QStringLiteral("nextAccountSequence")).toString(),
             QStringLiteral("2"));
    const QJsonObject depositor = root.value(QStringLiteral("depositors"))
                                      .toArray()
                                      .first()
                                      .toObject();
    const QJsonObject deposit = depositor.value(QStringLiteral("deposits"))
                                    .toArray()
                                    .first()
                                    .toObject();
    QVERIFY(deposit.value(QStringLiteral("originalPrincipalCents")).isString());
    QCOMPARE(deposit.value(QStringLiteral("originalPrincipalCents")).toString(),
             QStringLiteral("1000050"));
    QVERIFY(!json.contains("password\""));

    BankState restored;
    QString error;
    QVERIFY2(BankStateJsonSerializer::deserialize(json, &restored, &error), qPrintable(error));
    QCOMPARE(serializeState(restored), json);
}

void PersistenceTest::preservesFullWidthIntegerPrecision()
{
    const qint64 maximumAmount = std::numeric_limits<qint64>::max();
    const quint64 maximumSequence = std::numeric_limits<quint64>::max();
    const QDate startDate(2026, 6, 1);
    const FixedDeposit deposit(QStringLiteral("FD000001"),
                               maximumAmount,
                               maximumAmount,
                               startDate,
                               DepositTerm::OneYear,
                               annualRateBasisPoints(DepositTerm::OneYear),
                               startDate.addYears(1),
                               QStringLiteral("E01"),
                               QDateTime(startDate, QTime(8, 0)));
    const Transaction transaction(QStringLiteral("TX000001"),
                                  QStringLiteral("100001"),
                                  QStringLiteral("FD000001"),
                                  QDateTime(startDate, QTime(8, 1)),
                                  TransactionType::Deposit,
                                  WithdrawalKind::None,
                                  maximumAmount,
                                  0,
                                  QStringLiteral("E01"));
    const Depositor depositor(QStringLiteral("100001"),
                              QStringLiteral("精度测试"),
                              QByteArray(16, 's'),
                              QByteArray(32, 'h'),
                              210000,
                              QStringLiteral("测试地址"),
                              false,
                              std::nullopt,
                              QStringLiteral("E01"),
                              QDateTime(startDate, QTime(7, 59)),
                              {deposit},
                              {transaction});
    const BankState original(BankState::CurrentSchemaVersion,
                             maximumSequence,
                             maximumSequence,
                             maximumSequence,
                             {depositor});

    BankState restored;
    QString error;
    QVERIFY2(BankStateJsonSerializer::deserialize(
                 serializeState(original), &restored, &error),
             qPrintable(error));
    QCOMPARE(restored.nextAccountSequence(), maximumSequence);
    QCOMPARE(restored.nextDepositSequence(), maximumSequence);
    QCOMPARE(restored.nextTransactionSequence(), maximumSequence);
    QCOMPARE(restored.depositors().first().deposits().first().originalPrincipalCents(),
             maximumAmount);
    QCOMPARE(restored.depositors().first().transactions().first().principalAmountCents(),
             maximumAmount);
}

void PersistenceTest::savesAndLoadsThroughInjectedDirectory()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    const QString dataDirectory = QDir(temporaryRoot.path()).filePath(QStringLiteral("nested/data"));
    const FileManager manager(dataDirectory);

    const FileSaveResult saveResult = manager.save(makeState());
    QVERIFY2(saveResult.success, qPrintable(saveResult.errorMessage));
    QCOMPARE(QFileInfo(manager.dataFilePath()).fileName(), QStringLiteral("bank_data.json"));
    QVERIFY(QFileInfo::exists(manager.dataFilePath()));

    const FileLoadResult loadResult = manager.load();
    QVERIFY2(loadResult.success, qPrintable(loadResult.errorMessage));
    QVERIFY(!loadResult.initializedEmptyState);
    QCOMPARE(serializeState(loadResult.state), serializeState(makeState()));

    const QStringList files = QDir(dataDirectory).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QCOMPARE(files, QStringList{QStringLiteral("bank_data.json")});
}

void PersistenceTest::initializesEmptyStateWhenFileIsMissing()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    const QString dataDirectory = QDir(temporaryRoot.path()).filePath(QStringLiteral("first-run"));
    const FileManager manager(dataDirectory);

    const FileLoadResult result = manager.load();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.initializedEmptyState);
    QVERIFY(result.state.depositors().isEmpty());
    QCOMPARE(result.state.nextAccountSequence(), quint64(1));
    QVERIFY(QFileInfo(dataDirectory).isDir());
    QVERIFY(!QFileInfo::exists(manager.dataFilePath()));
}

void PersistenceTest::rejectsMalformedOrUnsupportedJson_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("syntax error") << QByteArray("{not-json");
    QTest::newRow("root is array") << QByteArray("[]");
    QTest::newRow("missing fields") << QByteArray(R"({"schemaVersion":1})");
    QTest::newRow("sequence has wrong type")
        << QByteArray(R"({"schemaVersion":1,"nextAccountSequence":1,"nextDepositSequence":"1","nextTransactionSequence":"1","depositors":[]})");
    QTest::newRow("zero sequence")
        << QByteArray(R"({"schemaVersion":1,"nextAccountSequence":"0","nextDepositSequence":"1","nextTransactionSequence":"1","depositors":[]})");
    QTest::newRow("unsupported schema")
        << QByteArray(R"({"schemaVersion":2,"nextAccountSequence":"1","nextDepositSequence":"1","nextTransactionSequence":"1","depositors":[]})");
}

void PersistenceTest::rejectsMalformedOrUnsupportedJson()
{
    QFETCH(QByteArray, json);
    BankState output;
    output.issueAccountNumber();
    QString error;

    QVERIFY(!BankStateJsonSerializer::deserialize(json, &output, &error));
    QVERIFY(!error.isEmpty());
    // 失败前已有的输出对象保持不变。
    QCOMPARE(output.nextAccountSequence(), quint64(2));
}

void PersistenceTest::rejectsBrokenDomainConstraintsWithoutChangingOutput()
{
    QJsonObject root = QJsonDocument::fromJson(serializeState(makeState())).object();
    QJsonArray depositors = root.value(QStringLiteral("depositors")).toArray();
    QJsonObject depositor = depositors.first().toObject();
    QJsonArray deposits = depositor.value(QStringLiteral("deposits")).toArray();
    QJsonObject deposit = deposits.first().toObject();
    deposit.insert(QStringLiteral("remainingPrincipalCents"), QStringLiteral("1000051"));
    deposits.replace(0, deposit);
    depositor.insert(QStringLiteral("deposits"), deposits);
    depositors.replace(0, depositor);
    root.insert(QStringLiteral("depositors"), depositors);

    BankState output;
    output.issueAccountNumber();
    QString error;
    QVERIFY(!BankStateJsonSerializer::deserialize(
        QJsonDocument(root).toJson(), &output, &error));
    QVERIFY(error.contains(QStringLiteral("剩余本金")));
    QCOMPARE(output.nextAccountSequence(), quint64(2));
}

void PersistenceTest::rejectsDuplicateGlobalIdentifiers()
{
    QJsonObject root = QJsonDocument::fromJson(serializeState(makeState())).object();
    QJsonArray depositors = root.value(QStringLiteral("depositors")).toArray();
    QJsonObject secondDepositor = depositors.first().toObject();
    secondDepositor.insert(QStringLiteral("accountNumber"), QStringLiteral("100002"));
    QJsonArray transactions = secondDepositor.value(QStringLiteral("transactions")).toArray();
    for (qsizetype index = 0; index < transactions.size(); ++index) {
        QJsonObject transaction = transactions.at(index).toObject();
        transaction.insert(QStringLiteral("accountNumber"), QStringLiteral("100002"));
        transaction.insert(QStringLiteral("transactionId"),
                           QStringLiteral("TX%1").arg(index + 3, 6, 10, QLatin1Char('0')));
        transactions.replace(index, transaction);
    }
    secondDepositor.insert(QStringLiteral("transactions"), transactions);
    depositors.append(secondDepositor);
    root.insert(QStringLiteral("depositors"), depositors);
    root.insert(QStringLiteral("nextAccountSequence"), QStringLiteral("3"));
    root.insert(QStringLiteral("nextTransactionSequence"), QStringLiteral("5"));

    BankState output;
    QString error;
    QVERIFY(!BankStateJsonSerializer::deserialize(
        QJsonDocument(root).toJson(), &output, &error));
    QVERIFY(error.contains(QStringLiteral("存款编号全局重复")));
}

void PersistenceTest::rejectsInvalidDiskDataWithoutOverwritingIt()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    const FileManager manager(temporaryRoot.path());
    const QByteArray invalidData("{\"schemaVersion\":1,\"depositors\":BROKEN}");
    QFile file(manager.dataFilePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(invalidData), invalidData.size());
    file.close();

    const FileLoadResult result = manager.load();
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("JSON 语法错误")));
    QCOMPARE(readFile(manager.dataFilePath()), invalidData);
}

void PersistenceTest::rejectsInvalidStateBeforeCreatingFile()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    const FileManager manager(temporaryRoot.path());
    const BankState invalidState(99, 1, 1, 1);

    const FileSaveResult result = manager.save(invalidState);
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("Schema")));
    QVERIFY(!QFileInfo::exists(manager.dataFilePath()));
}

void PersistenceTest::codecFailurePreservesExistingFileAndMemory()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    const FileManager plainManager(temporaryRoot.path());
    const BankState currentState = makeState();
    QVERIFY(plainManager.save(currentState).success);
    const QByteArray originalFile = readFile(plainManager.dataFilePath());

    BankState candidate = makeState();
    candidate.issueAccountNumber();
    const FileManager failingManager(temporaryRoot.path(), std::make_shared<RejectingCodec>());
    const FileSaveResult result = failingManager.save(candidate);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("编码失败")));
    QCOMPARE(readFile(plainManager.dataFilePath()), originalFile);
    QCOMPARE(currentState.nextAccountSequence(), quint64(2));
    QCOMPARE(candidate.nextAccountSequence(), quint64(3));
}

void PersistenceTest::atomicWriteFailurePreservesExistingFile()
{
#ifdef Q_OS_UNIX
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    const FileManager manager(temporaryRoot.path());
    QVERIFY(manager.save(makeState()).success);
    const QByteArray originalFile = readFile(manager.dataFilePath());

    BankState candidate = makeState();
    candidate.issueAccountNumber();
    const QFileDevice::Permissions originalPermissions = QFileInfo(temporaryRoot.path()).permissions();
    QVERIFY(QFile::setPermissions(temporaryRoot.path(),
                                  QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const FileSaveResult result = manager.save(candidate);
    const bool restored = QFile::setPermissions(temporaryRoot.path(), originalPermissions);

    QVERIFY(restored);
    QVERIFY(!result.success);
    QCOMPARE(readFile(manager.dataFilePath()), originalFile);
#else
    QSKIP("该原子写入权限测试只适用于 Unix 文件权限语义");
#endif
}

void PersistenceTest::rejectsDataDirectoryThatIsAFile()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    const QString path = QDir(temporaryRoot.path()).filePath(QStringLiteral("not-a-directory"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("occupied"), qint64(8));
    file.close();

    const FileManager manager(path);
    const FileSaveResult saveResult = manager.save(makeState());
    QVERIFY(!saveResult.success);
    QVERIFY(saveResult.errorMessage.contains(QStringLiteral("不是文件夹")));

    const FileLoadResult loadResult = manager.load();
    QVERIFY(!loadResult.success);
    QVERIFY(loadResult.errorMessage.contains(QStringLiteral("不是文件夹")));
    QCOMPARE(readFile(path), QByteArray("occupied"));
}

int main(int argc, char *argv[])
{
    PersistenceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_persistence.moc"

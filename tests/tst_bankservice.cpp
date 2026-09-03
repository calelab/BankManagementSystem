#include "persistence/bankstatejsonserializer.h"
#include "persistence/datacodec.h"
#include "persistence/filemanager.h"
#include "security/securityutils.h"
#include "services/bankservice.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <utility>

using namespace bank;
using namespace bank::persistence;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("无法读取测试文件：%s", qPrintable(file.errorString()));
    }
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qFatal("无法写入测试文件：%s", qPrintable(file.errorString()));
    }
    if (file.write(contents) != contents.size()) {
        qFatal("测试文件写入不完整");
    }
}

QByteArray serializeState(const BankState &state)
{
    QByteArray json;
    QString error;
    if (!BankStateJsonSerializer::serialize(state, &json, &error)) {
        qFatal("测试状态序列化失败：%s", qPrintable(error));
    }
    return json;
}

// 可切换故障的编码器让业务测试能在同一会话中验证成功保存和失败回滚。
class ToggleCodec final : public DataCodec
{
public:
    bool rejectEncoding = false;

    QString fileName() const override
    {
        return QStringLiteral("bank_data.json");
    }

    QString displayName() const override
    {
        return QStringLiteral("业务测试编码器");
    }

    bool encode(const QByteArray &plainJson,
                QByteArray *encodedData,
                QString *errorMessage) const override
    {
        if (rejectEncoding) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("测试保存失败");
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

class ServiceHarness
{
public:
    explicit ServiceHarness(std::shared_ptr<ToggleCodec> selectedCodec = nullptr)
        : now(QDateTime(QDate(2026, 1, 1), QTime(9, 0)))
        , codec(selectedCodec ? std::move(selectedCodec) : std::make_shared<ToggleCodec>())
    {
        if (!temporaryDirectory.isValid()) {
            qFatal("无法创建业务测试临时目录");
        }
        fileManager = std::make_shared<FileManager>(temporaryDirectory.path(), codec);
        service = std::make_unique<BankService>(fileManager, [this] { return now; });
        const ServiceResult initialization = service->initialize();
        if (!initialization.success) {
            qFatal("业务服务初始化失败：%s", qPrintable(initialization.message));
        }
        const ServiceResult employeeLogin = service->enterEmployeeSession(QStringLiteral("E03"));
        if (!employeeLogin.success) {
            qFatal("营业员测试会话建立失败：%s", qPrintable(employeeLogin.message));
        }
    }

    QString openAndLogin(const QString &password = QStringLiteral("SafePass123"))
    {
        const OpenAccountResult opened = service->openAccount(QStringLiteral("张三"),
                                                              QStringLiteral("上海市"),
                                                              password,
                                                              password);
        if (!opened.status.success) {
            qFatal("测试开户失败：%s", qPrintable(opened.status.message));
        }
        const ServiceResult loggedIn = service->loginDepositor(opened.accountNumber, password);
        if (!loggedIn.success) {
            qFatal("测试储户登录失败：%s", qPrintable(loggedIn.message));
        }
        return opened.accountNumber;
    }

    QTemporaryDir temporaryDirectory;
    QDateTime now;
    std::shared_ptr<ToggleCodec> codec;
    std::shared_ptr<FileManager> fileManager;
    std::unique_ptr<BankService> service;
};

} // namespace

// BankService 测试使用可控时钟和临时目录，锁定、到期与回滚结果不依赖真实环境。
class BankServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void initializesEmployeesAndEnforcesSessionOrder();
    void validatesExistingEmployeeFile();
    void reportsDamagedCoreDataAtInitialization();
    void opensAccountAndPersistsSecureCredentials();
    void locksRepeatedPasswordFailuresForSixtySeconds();
    void createsIndependentFixedDeposits();
    void previewsAndExecutesEarlyWithdrawal();
    void appliesMaturedInterestWithoutLateAccrual();
    void rejectsInvalidWithdrawalRequests();
    void updatesProfileAndReplacesPasswordSalt();
    void enforcesLossRestrictionsAndPasswordUnfreeze();
    void clearsDepositorWhenEmployeeChanges();
    void rollsBackEveryMutationWhenSavingFails();
};

void BankServiceTest::initializesEmployeesAndEnforcesSessionOrder()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const auto fileManager = std::make_shared<FileManager>(temporaryDirectory.path());
    BankService service(fileManager, [] {
        return QDateTime(QDate(2026, 1, 1), QTime(9, 0));
    });

    const ServiceResult initialization = service.initialize();
    QVERIFY2(initialization.success, qPrintable(initialization.message));
    QCOMPARE(service.validEmployeeIds().size(), 10);
    QVERIFY(service.validEmployeeIds().contains(QStringLiteral("E01")));
    QVERIFY(service.validEmployeeIds().contains(QStringLiteral("E10")));
    QVERIFY(QFileInfo::exists(QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("employees.dat"))));

    const OpenAccountResult noEmployee = service.openAccount(
        QStringLiteral("张三"), QStringLiteral("上海市"), QStringLiteral("p"), QStringLiteral("p"));
    QCOMPARE(noEmployee.status.error, ServiceError::EmployeeSessionRequired);
    QCOMPARE(service.enterEmployeeSession(QStringLiteral("E23")).error,
             ServiceError::InvalidEmployeeId);
    QVERIFY(service.enterEmployeeSession(QStringLiteral(" E03 ")).success);
    QCOMPARE(service.currentEmployeeId(), QStringLiteral("E03"));
    QCOMPARE(service.addFixedDeposit(10000, DepositTerm::OneYear).status.error,
             ServiceError::DepositorSessionRequired);
}

void BankServiceTest::validatesExistingEmployeeFile()
{
    QTemporaryDir validDirectory;
    QVERIFY(validDirectory.isValid());
    writeFile(QDir(validDirectory.path()).filePath(QStringLiteral("employees.dat")),
              QByteArray(" E01 \nE01\n\nE10\n"));
    BankService validService(std::make_shared<FileManager>(validDirectory.path()));
    QVERIFY(validService.initialize().success);
    QCOMPARE(validService.validEmployeeIds(),
             QSet<QString>({QStringLiteral("E01"), QStringLiteral("E10")}));

    QTemporaryDir damagedDirectory;
    QVERIFY(damagedDirectory.isValid());
    const QString damagedPath = QDir(damagedDirectory.path()).filePath(
        QStringLiteral("employees.dat"));
    const QByteArray damagedContents("E01\nBROKEN\n");
    writeFile(damagedPath, damagedContents);
    BankService damagedService(std::make_shared<FileManager>(damagedDirectory.path()));
    const ServiceResult result = damagedService.initialize();
    QCOMPARE(result.error, ServiceError::EmployeeDataFailure);
    QCOMPARE(readFile(damagedPath), damagedContents);
}

void BankServiceTest::reportsDamagedCoreDataAtInitialization()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    writeFile(QDir(temporaryDirectory.path()).filePath(QStringLiteral("bank_data.json")),
              QByteArray("{broken-json"));
    BankService service(std::make_shared<FileManager>(temporaryDirectory.path()));

    const ServiceResult result = service.initialize();
    QCOMPARE(result.error, ServiceError::DataLoadFailure);
    QVERIFY(!service.isInitialized());
    QCOMPARE(readFile(QDir(temporaryDirectory.path()).filePath(QStringLiteral("bank_data.json"))),
             QByteArray("{broken-json"));
}

void BankServiceTest::opensAccountAndPersistsSecureCredentials()
{
    ServiceHarness harness;
    const OpenAccountResult emptyName = harness.service->openAccount(
        QStringLiteral("  "), QStringLiteral("地址"), QStringLiteral("a"), QStringLiteral("a"));
    QCOMPARE(emptyName.status.error, ServiceError::InvalidInput);
    const OpenAccountResult mismatch = harness.service->openAccount(
        QStringLiteral("张三"), QStringLiteral("地址"), QStringLiteral("a"), QStringLiteral("b"));
    QCOMPARE(mismatch.status.error, ServiceError::InvalidInput);

    const QString password = QStringLiteral("SafePass123");
    const OpenAccountResult opened = harness.service->openAccount(
        QStringLiteral(" 张三 "), QStringLiteral(" 上海市 "), password, password);
    QVERIFY2(opened.status.success, qPrintable(opened.status.message));
    QCOMPARE(opened.accountNumber, QStringLiteral("100001"));
    QVERIFY(harness.service->currentDepositorAccount().isEmpty());
    const Depositor *depositor = harness.service->state().findDepositor(opened.accountNumber);
    QVERIFY(depositor);
    QCOMPARE(depositor->name(), QStringLiteral("张三"));
    QCOMPARE(depositor->address(), QStringLiteral("上海市"));
    QCOMPARE(depositor->passwordSalt().size(), security::SecurityUtils::PasswordSaltSize);
    QCOMPARE(depositor->passwordHash().size(), security::SecurityUtils::PasswordHashSize);
    QCOMPARE(depositor->passwordKdfIterations(),
             security::SecurityUtils::PasswordKdfIterations);
    QCOMPARE(depositor->passwordKdfAlgorithm(),
             Depositor::supportedPasswordKdfAlgorithm());
    QVERIFY(!readFile(harness.fileManager->dataFilePath()).contains(password.toUtf8()));

    const ServiceResult wrongPassword = harness.service->loginDepositor(
        opened.accountNumber, QStringLiteral("wrong"));
    const ServiceResult missingAccount = harness.service->loginDepositor(
        QStringLiteral("999999"), QStringLiteral("wrong"));
    QCOMPARE(wrongPassword.error, ServiceError::AuthenticationFailed);
    QCOMPARE(missingAccount.error, ServiceError::AuthenticationFailed);
    QCOMPARE(missingAccount.message, wrongPassword.message);
    QVERIFY(harness.service->loginDepositor(opened.accountNumber, password).success);
    QVERIFY(harness.service->logoutDepositor().success);
    QCOMPARE(harness.service->currentEmployeeId(), QStringLiteral("E03"));

    BankService restarted(harness.fileManager, [&harness] { return harness.now; });
    QVERIFY(restarted.initialize().success);
    QVERIFY(restarted.enterEmployeeSession(QStringLiteral("E04")).success);
    QVERIFY(restarted.loginDepositor(opened.accountNumber, password).success);
    QVERIFY(restarted.logoutDepositor().success);
    const OpenAccountResult second = restarted.openAccount(
        QStringLiteral("李四"), QStringLiteral("北京市"), password, password);
    QCOMPARE(second.accountNumber, QStringLiteral("100002"));
}

void BankServiceTest::locksRepeatedPasswordFailuresForSixtySeconds()
{
    ServiceHarness harness;
    const QString account = harness.openAndLogin();
    QVERIFY(harness.service->logoutDepositor().success);

    QCOMPARE(harness.service->loginDepositor(account, QStringLiteral("bad1")).error,
             ServiceError::AuthenticationFailed);
    QCOMPARE(harness.service->loginDepositor(account, QStringLiteral("bad2")).error,
             ServiceError::AuthenticationFailed);
    QVERIFY(harness.service->loginDepositor(account, QStringLiteral("SafePass123")).success);
    QVERIFY(harness.service->logoutDepositor().success);

    for (int attempt = 0; attempt < 4; ++attempt) {
        QCOMPARE(harness.service->loginDepositor(account, QStringLiteral("wrong")).error,
                 ServiceError::AuthenticationFailed);
    }
    QCOMPARE(harness.service->loginDepositor(account, QStringLiteral("wrong")).error,
             ServiceError::AccountTemporarilyLocked);
    harness.now = harness.now.addSecs(59);
    QCOMPARE(harness.service->loginDepositor(account, QStringLiteral("SafePass123")).error,
             ServiceError::AccountTemporarilyLocked);
    harness.now = harness.now.addSecs(1);
    QVERIFY(harness.service->loginDepositor(account, QStringLiteral("SafePass123")).success);
}

void BankServiceTest::createsIndependentFixedDeposits()
{
    ServiceHarness harness;
    const QString account = harness.openAndLogin();
    const DepositResult oneYear = harness.service->addFixedDeposit(
        1000000, DepositTerm::OneYear);
    const DepositResult threeYears = harness.service->addFixedDeposit(
        2500050, DepositTerm::ThreeYears);
    QVERIFY(oneYear.status.success);
    QVERIFY(threeYears.status.success);
    QCOMPARE(oneYear.depositId, QStringLiteral("FD000001"));
    QCOMPARE(threeYears.depositId, QStringLiteral("FD000002"));
    QCOMPARE(oneYear.transactionId, QStringLiteral("TX000001"));
    QCOMPARE(threeYears.transactionId, QStringLiteral("TX000002"));

    const Depositor *depositor = harness.service->state().findDepositor(account);
    QVERIFY(depositor);
    QCOMPARE(depositor->deposits().size(), 2);
    QCOMPARE(depositor->transactions().size(), 2);
    QCOMPARE(depositor->deposits().at(0).maturityDate(), QDate(2027, 1, 1));
    QCOMPARE(depositor->deposits().at(1).maturityDate(), QDate(2029, 1, 1));
    QCOMPARE(depositor->deposits().at(0).annualRateBasisPoints(), 198);
    QCOMPARE(depositor->deposits().at(1).annualRateBasisPoints(), 225);

    BankService restarted(harness.fileManager, [&harness] { return harness.now; });
    QVERIFY(restarted.initialize().success);
    QVERIFY(restarted.enterEmployeeSession(QStringLiteral("E05")).success);
    QVERIFY(restarted.loginDepositor(account, QStringLiteral("SafePass123")).success);
    QCOMPARE(restarted.state().findDepositor(account)->deposits().size(), 2);
    const DepositResult afterRestart = restarted.addFixedDeposit(
        500000, DepositTerm::FiveYears);
    QCOMPARE(afterRestart.depositId, QStringLiteral("FD000003"));
    QCOMPARE(afterRestart.transactionId, QStringLiteral("TX000003"));
}

void BankServiceTest::previewsAndExecutesEarlyWithdrawal()
{
    ServiceHarness harness;
    const QString account = harness.openAndLogin();
    const DepositResult deposited = harness.service->addFixedDeposit(
        1000000, DepositTerm::OneYear);
    harness.now = harness.now.addDays(1);

    const QByteArray beforePreview = serializeState(harness.service->state());
    const WithdrawalPreviewResult preview = harness.service->previewWithdrawal(
        deposited.depositId, 400000);
    QVERIFY(preview.status.success);
    QCOMPARE(preview.calculation.kind, WithdrawalKind::Early);
    QCOMPARE(preview.calculation.interestCents, qint64(1));
    QCOMPARE(preview.calculation.actualPayoutCents, qint64(400001));
    QCOMPARE(serializeState(harness.service->state()), beforePreview);

    const WithdrawalResult result = harness.service->withdraw(deposited.depositId, 400000);
    QVERIFY2(result.status.success, qPrintable(result.status.message));
    QCOMPARE(result.remainingPrincipalCents, qint64(600000));
    QCOMPARE(result.calculation.kind, WithdrawalKind::Early);
    const Depositor *depositor = harness.service->state().findDepositor(account);
    QCOMPARE(depositor->findDeposit(deposited.depositId)->remainingPrincipalCents(),
             qint64(600000));
    QCOMPARE(depositor->transactions().size(), 2);
    QCOMPARE(depositor->transactions().last().principalAmountCents(), qint64(400000));
    QCOMPARE(depositor->transactions().last().interestAmountCents(), qint64(1));
}

void BankServiceTest::appliesMaturedInterestWithoutLateAccrual()
{
    ServiceHarness harness;
    harness.openAndLogin();
    const DepositResult deposited = harness.service->addFixedDeposit(
        1000000, DepositTerm::OneYear);

    harness.now = QDateTime(QDate(2027, 1, 1), QTime(9, 0));
    const WithdrawalResult atMaturity = harness.service->withdraw(deposited.depositId, 400000);
    QVERIFY(atMaturity.status.success);
    QCOMPARE(atMaturity.calculation.kind, WithdrawalKind::Matured);
    QCOMPARE(atMaturity.calculation.interestCents, qint64(7920));

    harness.now = QDateTime(QDate(2028, 1, 1), QTime(9, 0));
    const WithdrawalResult late = harness.service->withdraw(deposited.depositId, 200000);
    QVERIFY(late.status.success);
    QCOMPARE(late.calculation.kind, WithdrawalKind::Matured);
    QCOMPARE(late.calculation.interestCents, qint64(3960));
    QCOMPARE(late.remainingPrincipalCents, qint64(400000));
}

void BankServiceTest::rejectsInvalidWithdrawalRequests()
{
    ServiceHarness harness;
    harness.openAndLogin();
    const DepositResult deposited = harness.service->addFixedDeposit(
        100000, DepositTerm::FiveYears);
    const QByteArray original = serializeState(harness.service->state());

    QCOMPARE(harness.service->previewWithdrawal(QStringLiteral("FD999999"), 1).status.error,
             ServiceError::DepositNotFound);
    QCOMPARE(harness.service->withdraw(deposited.depositId, 0).status.error,
             ServiceError::InvalidInput);
    QCOMPARE(harness.service->withdraw(deposited.depositId, 100001).status.error,
             ServiceError::InsufficientPrincipal);
    QCOMPARE(harness.service->addFixedDeposit(100,
                                              static_cast<DepositTerm>(99))
                 .status.error,
             ServiceError::InvalidInput);
    harness.now = QDateTime();
    QCOMPARE(harness.service->previewWithdrawal(deposited.depositId, 1).status.error,
             ServiceError::InternalStateError);
    QCOMPARE(serializeState(harness.service->state()), original);
}

void BankServiceTest::updatesProfileAndReplacesPasswordSalt()
{
    ServiceHarness harness;
    const QString oldPassword = QStringLiteral("OldPassword");
    const QString newPassword = QStringLiteral("NewPassword");
    const QString account = harness.openAndLogin(oldPassword);
    const QByteArray oldSalt = harness.service->currentDepositor()->passwordSalt();

    QVERIFY(harness.service->updateProfile(QStringLiteral(" 李四 "),
                                           QStringLiteral(" 北京市朝阳区 "))
                .success);
    QCOMPARE(harness.service->currentDepositor()->name(), QStringLiteral("李四"));
    QCOMPARE(harness.service->currentDepositor()->address(), QStringLiteral("北京市朝阳区"));
    QCOMPARE(harness.service->changePassword(QStringLiteral("wrong"),
                                             newPassword,
                                             newPassword)
                 .error,
             ServiceError::AuthenticationFailed);
    QCOMPARE(harness.service->changePassword(oldPassword,
                                             newPassword,
                                             QStringLiteral("different"))
                 .error,
             ServiceError::InvalidInput);
    QVERIFY(harness.service->changePassword(oldPassword, newPassword, newPassword).success);
    QVERIFY(harness.service->currentDepositor()->passwordSalt() != oldSalt);

    QVERIFY(harness.service->logoutDepositor().success);
    QCOMPARE(harness.service->loginDepositor(account, oldPassword).error,
             ServiceError::AuthenticationFailed);
    QVERIFY(harness.service->loginDepositor(account, newPassword).success);
    const QByteArray diskData = readFile(harness.fileManager->dataFilePath());
    QVERIFY(!diskData.contains(oldPassword.toUtf8()));
    QVERIFY(!diskData.contains(newPassword.toUtf8()));
}

void BankServiceTest::enforcesLossRestrictionsAndPasswordUnfreeze()
{
    ServiceHarness harness;
    const QString password = QStringLiteral("SafePass123");
    const QString account = harness.openAndLogin(password);
    const DepositResult deposited = harness.service->addFixedDeposit(
        100000, DepositTerm::OneYear);

    QVERIFY(harness.service->reportLoss().success);
    QVERIFY(harness.service->currentDepositor()->isLost());
    QCOMPARE(*harness.service->currentDepositor()->lostDate(), harness.now.date());
    QCOMPARE(harness.service->addFixedDeposit(1, DepositTerm::OneYear).status.error,
             ServiceError::AccountLost);
    QCOMPARE(harness.service->withdraw(deposited.depositId, 1).status.error,
             ServiceError::AccountLost);
    QCOMPARE(harness.service->updateProfile(QStringLiteral("新姓名"), QStringLiteral("新地址")).error,
             ServiceError::AccountLost);
    QCOMPARE(harness.service->changePassword(password, QStringLiteral("new"), QStringLiteral("new")).error,
             ServiceError::AccountLost);

    QVERIFY(harness.service->logoutDepositor().success);
    QVERIFY(harness.service->loginDepositor(account, password).success);
    QCOMPARE(harness.service->unfreezeAccount(QStringLiteral("wrong")).error,
             ServiceError::AuthenticationFailed);
    QVERIFY(harness.service->currentDepositor()->isLost());
    QVERIFY(harness.service->unfreezeAccount(password).success);
    QVERIFY(!harness.service->currentDepositor()->isLost());
    QVERIFY(!harness.service->currentDepositor()->lostDate().has_value());
}

void BankServiceTest::clearsDepositorWhenEmployeeChanges()
{
    ServiceHarness harness;
    harness.openAndLogin();
    QVERIFY(!harness.service->currentDepositorAccount().isEmpty());

    QVERIFY(harness.service->enterEmployeeSession(QStringLiteral("E04")).success);
    QCOMPARE(harness.service->currentEmployeeId(), QStringLiteral("E04"));
    QVERIFY(harness.service->currentDepositorAccount().isEmpty());
    QCOMPARE(harness.service->addFixedDeposit(100, DepositTerm::OneYear).status.error,
             ServiceError::DepositorSessionRequired);

    QVERIFY(harness.service->switchEmployee().success);
    QVERIFY(harness.service->currentEmployeeId().isEmpty());
    QVERIFY(harness.service->currentDepositorAccount().isEmpty());
}

void BankServiceTest::rollsBackEveryMutationWhenSavingFails()
{
    const auto codec = std::make_shared<ToggleCodec>();
    ServiceHarness harness(codec);
    const QString password = QStringLiteral("SafePass123");
    harness.openAndLogin(password);
    const DepositResult deposited = harness.service->addFixedDeposit(
        1000000, DepositTerm::OneYear);
    const QByteArray originalState = serializeState(harness.service->state());
    const QByteArray originalFile = readFile(harness.fileManager->dataFilePath());

    codec->rejectEncoding = true;
    QCOMPARE(harness.service->openAccount(QStringLiteral("李四"),
                                          QStringLiteral("北京市"),
                                          password,
                                          password)
                 .status.error,
             ServiceError::PersistenceFailure);
    QCOMPARE(harness.service->addFixedDeposit(500000, DepositTerm::ThreeYears).status.error,
             ServiceError::PersistenceFailure);
    QCOMPARE(harness.service->withdraw(deposited.depositId, 200000).status.error,
             ServiceError::PersistenceFailure);
    QCOMPARE(harness.service->updateProfile(QStringLiteral("新姓名"), QStringLiteral("新地址")).error,
             ServiceError::PersistenceFailure);
    QCOMPARE(harness.service->changePassword(password,
                                             QStringLiteral("new-password"),
                                             QStringLiteral("new-password"))
                 .error,
             ServiceError::PersistenceFailure);
    QCOMPARE(harness.service->reportLoss().error, ServiceError::PersistenceFailure);
    QCOMPARE(serializeState(harness.service->state()), originalState);
    QCOMPARE(readFile(harness.fileManager->dataFilePath()), originalFile);

    codec->rejectEncoding = false;
    QVERIFY(harness.service->reportLoss().success);
    const QByteArray lostState = serializeState(harness.service->state());
    const QByteArray lostFile = readFile(harness.fileManager->dataFilePath());
    codec->rejectEncoding = true;
    QCOMPARE(harness.service->unfreezeAccount(password).error,
             ServiceError::PersistenceFailure);
    QCOMPARE(serializeState(harness.service->state()), lostState);
    QCOMPARE(readFile(harness.fileManager->dataFilePath()), lostFile);
}

int main(int argc, char *argv[])
{
    BankServiceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_bankservice.moc"

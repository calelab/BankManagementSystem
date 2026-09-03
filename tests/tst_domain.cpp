#include "models/bankstate.h"
#include "models/banktypes.h"
#include "models/depositor.h"
#include "models/fixeddeposit.h"
#include "models/transaction.h"
#include "services/interestcalculator.h"
#include "utils/moneyutils.h"

#include <QTest>

#include <limits>
#include <optional>

using namespace bank;

namespace {

FixedDeposit makeDeposit(const QString &depositId = QStringLiteral("FD000001"),
                         qint64 originalPrincipalCents = 1000000,
                         qint64 remainingPrincipalCents = 1000000,
                         const QDate &startDate = QDate(2026, 1, 15),
                         DepositTerm term = DepositTerm::OneYear)
{
    return FixedDeposit(depositId,
                        originalPrincipalCents,
                        remainingPrincipalCents,
                        startDate,
                        term,
                        annualRateBasisPoints(term),
                        FixedDeposit::calculateMaturityDate(startDate, term),
                        QStringLiteral("E03"),
                        QDateTime(startDate, QTime(9, 30)));
}

Transaction makeDepositTransaction(const QString &accountNumber = QStringLiteral("100001"),
                                   const QString &depositId = QStringLiteral("FD000001"),
                                   const QString &transactionId = QStringLiteral("TX000001"))
{
    return Transaction(transactionId,
                       accountNumber,
                       depositId,
                       QDateTime(QDate(2026, 1, 15), QTime(9, 31)),
                       TransactionType::Deposit,
                       WithdrawalKind::None,
                       1000000,
                       0,
                       QStringLiteral("E03"));
}

Depositor makeDepositor(const QString &accountNumber = QStringLiteral("100001"),
                        const QString &depositId = QStringLiteral("FD000001"),
                        const QString &transactionId = QStringLiteral("TX000001"))
{
    QVector<FixedDeposit> deposits{makeDeposit(depositId)};
    QVector<Transaction> transactions{
        makeDepositTransaction(accountNumber, depositId, transactionId)};
    return Depositor(accountNumber,
                     QStringLiteral("张三"),
                     QByteArray(16, 's'),
                     QByteArray(32, 'h'),
                     210000,
                     QStringLiteral("上海市"),
                     false,
                     std::nullopt,
                     QStringLiteral("E03"),
                     QDateTime(QDate(2026, 1, 15), QTime(9, 0)),
                     deposits,
                     transactions);
}

} // namespace

// 领域层测试不读写真实数据目录，固定日期保证每次运行结果一致。
class DomainTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesValidMoney_data();
    void parsesValidMoney();
    void rejectsInvalidMoney_data();
    void rejectsInvalidMoney();
    void handlesMoneyBoundaries();
    void formatsMoneyAndRates();
    void mapsTermsAndMaturityDates();
    void derivesDepositStatus();
    void validatesFixedDeposit();
    void calculatesMaturedInterest_data();
    void calculatesMaturedInterest();
    void calculatesEarlyInterest_data();
    void calculatesEarlyInterest();
    void roundsInterestToNearestCent();
    void classifiesWithdrawalAtMaturityBoundary();
    void rejectsInterestOverflowAndInvalidDates();
    void validatesTransactions();
    void validatesDepositorAggregate();
    void issuesPersistentIdentifiers();
    void validatesBankStateGlobalUniqueness();
};

void DomainTest::parsesValidMoney_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("expectedCents");

    QTest::newRow("whole yuan") << QStringLiteral("10000") << qint64(1000000);
    QTest::newRow("one decimal") << QStringLiteral("10000.5") << qint64(1000050);
    QTest::newRow("two decimals") << QStringLiteral("10000.50") << qint64(1000050);
    QTest::newRow("less than one yuan") << QStringLiteral("0.01") << qint64(1);
}

void DomainTest::parsesValidMoney()
{
    QFETCH(QString, text);
    QFETCH(qint64, expectedCents);

    QString error;
    const auto cents = MoneyUtils::parseCents(text, &error);
    QVERIFY2(cents.has_value(), qPrintable(error));
    QCOMPARE(*cents, expectedCents);
    QVERIFY(error.isEmpty());
}

void DomainTest::rejectsInvalidMoney_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("zero") << QStringLiteral("0");
    QTest::newRow("negative") << QStringLiteral("-1");
    QTest::newRow("three decimals") << QStringLiteral("1.234");
    QTest::newRow("letters") << QStringLiteral("abc");
    QTest::newRow("scientific notation") << QStringLiteral("1e3");
    QTest::newRow("comma") << QStringLiteral("1,000.00");
    QTest::newRow("surrounding spaces") << QStringLiteral(" 1.00 ");
    QTest::newRow("leading zero") << QStringLiteral("01.00");
    QTest::newRow("overflow") << QStringLiteral("92233720368547758.08");
}

void DomainTest::rejectsInvalidMoney()
{
    QFETCH(QString, text);

    QString error;
    QVERIFY(!MoneyUtils::parseCents(text, &error).has_value());
    QVERIFY(!error.isEmpty());
}

void DomainTest::handlesMoneyBoundaries()
{
    const auto maximum = MoneyUtils::parseCents(QStringLiteral("92233720368547758.07"));
    QVERIFY(maximum.has_value());
    QCOMPARE(*maximum, std::numeric_limits<qint64>::max());
}

void DomainTest::formatsMoneyAndRates()
{
    QCOMPARE(MoneyUtils::formatCents(1000050), QStringLiteral("¥10,000.50"));
    QCOMPARE(MoneyUtils::formatCents(-1), QStringLiteral("-¥0.01"));
    QCOMPARE(MoneyUtils::formatCents(std::numeric_limits<qint64>::min()),
             QStringLiteral("-¥92,233,720,368,547,758.08"));
    QCOMPARE(MoneyUtils::formatRateBasisPoints(198), QStringLiteral("1.98%"));
    QCOMPARE(MoneyUtils::formatRateBasisPoints(5), QStringLiteral("0.05%"));
}

void DomainTest::mapsTermsAndMaturityDates()
{
    QCOMPARE(termYears(DepositTerm::OneYear), 1);
    QCOMPARE(termYears(DepositTerm::ThreeYears), 3);
    QCOMPARE(termYears(DepositTerm::FiveYears), 5);
    QCOMPARE(annualRateBasisPoints(DepositTerm::OneYear), 198);
    QCOMPARE(annualRateBasisPoints(DepositTerm::ThreeYears), 225);
    QCOMPARE(annualRateBasisPoints(DepositTerm::FiveYears), 350);

    QCOMPARE(FixedDeposit::calculateMaturityDate(QDate(2024, 2, 29), DepositTerm::OneYear),
             QDate(2025, 2, 28));
    QCOMPARE(FixedDeposit::calculateMaturityDate(QDate(2026, 1, 15), DepositTerm::FiveYears),
             QDate(2031, 1, 15));
}

void DomainTest::derivesDepositStatus()
{
    FixedDeposit deposit = makeDeposit();
    QCOMPARE(deposit.statusOn(QDate(2027, 1, 14)), DepositStatus::Active);
    QCOMPARE(deposit.statusOn(QDate(2027, 1, 15)), DepositStatus::Matured);
    QCOMPARE(deposit.statusOn(QDate(2028, 1, 15)), DepositStatus::Matured);

    QVERIFY(deposit.setRemainingPrincipalCents(0));
    QCOMPARE(deposit.statusOn(QDate(2026, 1, 16)), DepositStatus::Closed);
    QVERIFY(!deposit.setRemainingPrincipalCents(-1));
    QVERIFY(!deposit.setRemainingPrincipalCents(1000001));
}

void DomainTest::validatesFixedDeposit()
{
    QString error;
    QVERIFY2(makeDeposit().isValid(&error), qPrintable(error));

    FixedDeposit wrongRate(QStringLiteral("FD000002"),
                           10000,
                           10000,
                           QDate(2026, 1, 1),
                           DepositTerm::ThreeYears,
                           198,
                           QDate(2029, 1, 1),
                           QStringLiteral("E03"),
                           QDateTime(QDate(2026, 1, 1), QTime(8, 0)));
    QVERIFY(!wrongRate.isValid(&error));
    QVERIFY(error.contains(QStringLiteral("利率")));
}

void DomainTest::calculatesMaturedInterest_data()
{
    QTest::addColumn<int>("rateBasisPoints");
    QTest::addColumn<int>("years");
    QTest::addColumn<qint64>("expectedInterest");

    QTest::newRow("one year") << 198 << 1 << qint64(19800);
    QTest::newRow("three years") << 225 << 3 << qint64(67500);
    QTest::newRow("five years") << 350 << 5 << qint64(175000);
}

void DomainTest::calculatesMaturedInterest()
{
    QFETCH(int, rateBasisPoints);
    QFETCH(int, years);
    QFETCH(qint64, expectedInterest);

    const auto interest = InterestCalculator::maturedInterest(
        1000000, rateBasisPoints, years);
    QVERIFY(interest.has_value());
    QCOMPARE(*interest, expectedInterest);
}

void DomainTest::calculatesEarlyInterest_data()
{
    QTest::addColumn<int>("days");
    QTest::addColumn<qint64>("expectedInterest");

    QTest::newRow("zero days") << 0 << qint64(0);
    QTest::newRow("one day") << 1 << qint64(1);
    QTest::newRow("full year") << 365 << qint64(500);
}

void DomainTest::calculatesEarlyInterest()
{
    QFETCH(int, days);
    QFETCH(qint64, expectedInterest);
    const QDate startDate(2026, 1, 1);

    const auto interest = InterestCalculator::earlyWithdrawalInterest(
        1000000, startDate, startDate.addDays(days));
    QVERIFY(interest.has_value());
    QCOMPARE(*interest, expectedInterest);
}

void DomainTest::roundsInterestToNearestCent()
{
    const auto roundsDown = InterestCalculator::maturedInterest(2, 2500, 1);
    const auto roundsUp = InterestCalculator::maturedInterest(2, 2501, 1);
    QVERIFY(roundsDown.has_value());
    QVERIFY(roundsUp.has_value());
    QCOMPARE(*roundsDown, qint64(1));
    QCOMPARE(*roundsUp, qint64(1));

    const auto belowHalfCent = InterestCalculator::maturedInterest(1, 4999, 1);
    const auto atHalfCent = InterestCalculator::maturedInterest(1, 5000, 1);
    QCOMPARE(*belowHalfCent, qint64(0));
    QCOMPARE(*atHalfCent, qint64(1));
}

void DomainTest::classifiesWithdrawalAtMaturityBoundary()
{
    const FixedDeposit deposit = makeDeposit();
    const auto early = InterestCalculator::withdrawal(deposit, 400000, QDate(2027, 1, 14));
    const auto matured = InterestCalculator::withdrawal(deposit, 400000, QDate(2027, 1, 15));
    const auto late = InterestCalculator::withdrawal(deposit, 400000, QDate(2028, 1, 15));

    QVERIFY(early.has_value());
    QVERIFY(matured.has_value());
    QVERIFY(late.has_value());
    QCOMPARE(early->kind, WithdrawalKind::Early);
    QCOMPARE(matured->kind, WithdrawalKind::Matured);
    QCOMPARE(late->kind, WithdrawalKind::Matured);
    QCOMPARE(matured->interestCents, qint64(7920));
    QCOMPARE(late->interestCents, matured->interestCents);
    QCOMPARE(matured->actualPayoutCents, qint64(407920));
}

void DomainTest::rejectsInterestOverflowAndInvalidDates()
{
    QString error;
    const auto largeButSafe = InterestCalculator::earlyWithdrawalInterest(
        std::numeric_limits<qint64>::max(), QDate(2026, 1, 1), QDate(2026, 1, 2), &error);
    QVERIFY2(largeButSafe.has_value(), qPrintable(error));
    QVERIFY(*largeButSafe > 0);

    QVERIFY(!InterestCalculator::earlyWithdrawalInterest(
                 10000, QDate(2026, 1, 2), QDate(2026, 1, 1), &error)
                 .has_value());
    QVERIFY(!error.isEmpty());

    QVERIFY(!InterestCalculator::maturedInterest(
                 std::numeric_limits<qint64>::max(), 10000, 2, &error)
                 .has_value());
    QVERIFY(!error.isEmpty());
}

void DomainTest::validatesTransactions()
{
    QString error;
    const Transaction deposit = makeDepositTransaction();
    QVERIFY2(deposit.isValid(&error), qPrintable(error));
    QCOMPARE(deposit.actualPayoutCents(), qint64(1000000));

    const Transaction invalidDeposit(QStringLiteral("TX000002"),
                                     QStringLiteral("100001"),
                                     QStringLiteral("FD000001"),
                                     QDateTime(QDate(2026, 1, 15), QTime(10, 0)),
                                     TransactionType::Deposit,
                                     WithdrawalKind::Early,
                                     10000,
                                     1,
                                     QStringLiteral("E03"));
    QVERIFY(!invalidDeposit.isValid(&error));
}

void DomainTest::validatesDepositorAggregate()
{
    Depositor depositor = makeDepositor();
    QString error;
    QVERIFY2(depositor.isValid(&error), qPrintable(error));

    QVERIFY(!depositor.addDeposit(makeDeposit(), &error));
    QVERIFY(error.contains(QStringLiteral("重复")));

    const Transaction wrongAccount = makeDepositTransaction(
        QStringLiteral("100002"), QStringLiteral("FD000001"), QStringLiteral("TX000002"));
    QVERIFY(!depositor.addTransaction(wrongAccount, &error));

    Depositor inconsistentLoss(QStringLiteral("100002"),
                               QStringLiteral("李四"),
                               QByteArray(16, 's'),
                               QByteArray(32, 'h'),
                               210000,
                               QStringLiteral("北京市"),
                               true,
                               std::nullopt,
                               QStringLiteral("E01"),
                               QDateTime(QDate(2026, 2, 1), QTime(8, 0)));
    QVERIFY(!inconsistentLoss.isValid(&error));
    QVERIFY(error.contains(QStringLiteral("挂失")));
}

void DomainTest::issuesPersistentIdentifiers()
{
    BankState state;
    QCOMPARE(state.issueAccountNumber(), QStringLiteral("100001"));
    QCOMPARE(state.issueAccountNumber(), QStringLiteral("100002"));
    QCOMPARE(state.issueDepositId(), QStringLiteral("FD000001"));
    QCOMPARE(state.issueTransactionId(), QStringLiteral("TX000001"));
    QCOMPARE(state.nextAccountSequence(), quint64(3));
    QCOMPARE(state.nextDepositSequence(), quint64(2));
    QCOMPARE(state.nextTransactionSequence(), quint64(2));
}

void DomainTest::validatesBankStateGlobalUniqueness()
{
    BankState validState(BankState::CurrentSchemaVersion, 2, 2, 2, {makeDepositor()});
    QString error;
    QVERIFY2(validState.isValid(&error), qPrintable(error));

    const Depositor duplicateDeposit = makeDepositor(
        QStringLiteral("100002"), QStringLiteral("FD000001"), QStringLiteral("TX000002"));
    BankState duplicateState(BankState::CurrentSchemaVersion,
                             3,
                             2,
                             3,
                             {makeDepositor(), duplicateDeposit});
    QVERIFY(!duplicateState.isValid(&error));
    QVERIFY(error.contains(QStringLiteral("存款编号")));

    BankState staleSequenceState(BankState::CurrentSchemaVersion,
                                 1,
                                 1,
                                 1,
                                 {makeDepositor()});
    QVERIFY(!staleSequenceState.isValid(&error));
    QVERIFY(error.contains(QStringLiteral("下一个编号序列")));

    QVERIFY(!validState.addDepositor(makeDepositor(), &error));
    QVERIFY(error.contains(QStringLiteral("账号")));
}

QTEST_APPLESS_MAIN(DomainTest)

#include "tst_domain.moc"

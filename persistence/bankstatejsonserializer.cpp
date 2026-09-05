// JSON 序列化实现：完整保存对象图，并在加载时拒绝缺失、越界或矛盾数据。
#include "persistence/bankstatejsonserializer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace bank::persistence {
namespace {

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

bool readRequiredValue(const QJsonObject &object,
                       const QString &key,
                       QJsonValue *value,
                       QString *errorMessage)
{
    const auto iterator = object.constFind(key);
    if (iterator == object.constEnd() || iterator->isUndefined()) {
        return fail(errorMessage, QStringLiteral("缺少必需字段：%1").arg(key));
    }
    *value = *iterator;
    return true;
}

bool readString(const QJsonObject &object,
                const QString &key,
                QString *value,
                QString *errorMessage)
{
    QJsonValue jsonValue;
    if (!readRequiredValue(object, key, &jsonValue, errorMessage)) {
        return false;
    }
    if (!jsonValue.isString()) {
        return fail(errorMessage, QStringLiteral("字段 %1 必须是字符串").arg(key));
    }
    *value = jsonValue.toString();
    return true;
}

bool readBoolean(const QJsonObject &object,
                 const QString &key,
                 bool *value,
                 QString *errorMessage)
{
    QJsonValue jsonValue;
    if (!readRequiredValue(object, key, &jsonValue, errorMessage)) {
        return false;
    }
    if (!jsonValue.isBool()) {
        return fail(errorMessage, QStringLiteral("字段 %1 必须是布尔值").arg(key));
    }
    *value = jsonValue.toBool();
    return true;
}

bool readInteger(const QJsonObject &object,
                 const QString &key,
                 int *value,
                 QString *errorMessage)
{
    QJsonValue jsonValue;
    if (!readRequiredValue(object, key, &jsonValue, errorMessage)) {
        return false;
    }
    if (!jsonValue.isDouble()) {
        return fail(errorMessage, QStringLiteral("字段 %1 必须是整数").arg(key));
    }

    const double number = jsonValue.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) {
        return fail(errorMessage, QStringLiteral("字段 %1 超出整数范围").arg(key));
    }
    *value = static_cast<int>(number);
    return true;
}

bool readArray(const QJsonObject &object,
               const QString &key,
               QJsonArray *value,
               QString *errorMessage)
{
    QJsonValue jsonValue;
    if (!readRequiredValue(object, key, &jsonValue, errorMessage)) {
        return false;
    }
    if (!jsonValue.isArray()) {
        return fail(errorMessage, QStringLiteral("字段 %1 必须是数组").arg(key));
    }
    *value = jsonValue.toArray();
    return true;
}

bool parseUnsignedDecimal(const QString &text,
                          quint64 *value,
                          const QString &fieldName,
                          QString *errorMessage)
{
    static const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)$"));
    if (!pattern.match(text).hasMatch()) {
        return fail(errorMessage, QStringLiteral("字段 %1 不是有效的非负十进制整数").arg(fieldName));
    }
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok);
    if (!ok) {
        return fail(errorMessage, QStringLiteral("字段 %1 超出整数范围").arg(fieldName));
    }
    *value = parsed;
    return true;
}

bool readSequence(const QJsonObject &object,
                  const QString &key,
                  quint64 *value,
                  QString *errorMessage)
{
    QString text;
    return readString(object, key, &text, errorMessage)
           && parseUnsignedDecimal(text, value, key, errorMessage)
           && (*value > 0 || fail(errorMessage, QStringLiteral("字段 %1 必须大于零").arg(key)));
}

bool readCents(const QJsonObject &object,
               const QString &key,
               qint64 *value,
               QString *errorMessage)
{
    QString text;
    quint64 unsignedValue = 0;
    if (!readString(object, key, &text, errorMessage)
        || !parseUnsignedDecimal(text, &unsignedValue, key, errorMessage)) {
        return false;
    }
    if (unsignedValue > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
        return fail(errorMessage, QStringLiteral("字段 %1 超出金额范围").arg(key));
    }
    *value = static_cast<qint64>(unsignedValue);
    return true;
}

bool parseDate(const QString &text,
               QDate *date,
               const QString &fieldName,
               QString *errorMessage)
{
    const QDate parsed = QDate::fromString(text, Qt::ISODate);
    if (!parsed.isValid() || parsed.toString(Qt::ISODate) != text) {
        return fail(errorMessage, QStringLiteral("字段 %1 不是有效 ISO 日期").arg(fieldName));
    }
    *date = parsed;
    return true;
}

bool readDate(const QJsonObject &object,
              const QString &key,
              QDate *date,
              QString *errorMessage)
{
    QString text;
    return readString(object, key, &text, errorMessage)
           && parseDate(text, date, key, errorMessage);
}

bool readDateTime(const QJsonObject &object,
                  const QString &key,
                  QDateTime *dateTime,
                  QString *errorMessage)
{
    QString text;
    if (!readString(object, key, &text, errorMessage)) {
        return false;
    }
    const QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        return fail(errorMessage, QStringLiteral("字段 %1 不是有效 ISO 日期时间").arg(key));
    }
    *dateTime = parsed;
    return true;
}

bool decodeBase64(const QString &text,
                  QByteArray *decoded,
                  const QString &fieldName,
                  QString *errorMessage)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"));
    if (!pattern.match(text).hasMatch()) {
        return fail(errorMessage, QStringLiteral("字段 %1 不是有效 Base64").arg(fieldName));
    }
    const QByteArray encoded = text.toLatin1();
    const QByteArray bytes = QByteArray::fromBase64(encoded);
    if (bytes.toBase64() != encoded) {
        return fail(errorMessage, QStringLiteral("字段 %1 不是规范 Base64").arg(fieldName));
    }
    *decoded = bytes;
    return true;
}

QString termToJson(DepositTerm term)
{
    switch (term) {
    case DepositTerm::OneYear:
        return QStringLiteral("OneYear");
    case DepositTerm::ThreeYears:
        return QStringLiteral("ThreeYears");
    case DepositTerm::FiveYears:
        return QStringLiteral("FiveYears");
    }
    return {};
}

std::optional<DepositTerm> termFromJson(const QString &text)
{
    if (text == QStringLiteral("OneYear")) {
        return DepositTerm::OneYear;
    }
    if (text == QStringLiteral("ThreeYears")) {
        return DepositTerm::ThreeYears;
    }
    if (text == QStringLiteral("FiveYears")) {
        return DepositTerm::FiveYears;
    }
    return std::nullopt;
}

QString transactionTypeToJson(TransactionType type)
{
    return type == TransactionType::Deposit ? QStringLiteral("Deposit")
                                            : QStringLiteral("Withdrawal");
}

std::optional<TransactionType> transactionTypeFromJson(const QString &text)
{
    if (text == QStringLiteral("Deposit")) {
        return TransactionType::Deposit;
    }
    if (text == QStringLiteral("Withdrawal")) {
        return TransactionType::Withdrawal;
    }
    return std::nullopt;
}

QString withdrawalKindToJson(WithdrawalKind kind)
{
    switch (kind) {
    case WithdrawalKind::None:
        return QStringLiteral("None");
    case WithdrawalKind::Early:
        return QStringLiteral("Early");
    case WithdrawalKind::Matured:
        return QStringLiteral("Matured");
    }
    return {};
}

std::optional<WithdrawalKind> withdrawalKindFromJson(const QString &text)
{
    if (text == QStringLiteral("None")) {
        return WithdrawalKind::None;
    }
    if (text == QStringLiteral("Early")) {
        return WithdrawalKind::Early;
    }
    if (text == QStringLiteral("Matured")) {
        return WithdrawalKind::Matured;
    }
    return std::nullopt;
}

QJsonObject serializeDeposit(const FixedDeposit &deposit)
{
    return {
        {QStringLiteral("depositId"), deposit.depositId()},
        {QStringLiteral("originalPrincipalCents"), QString::number(deposit.originalPrincipalCents())},
        {QStringLiteral("remainingPrincipalCents"), QString::number(deposit.remainingPrincipalCents())},
        {QStringLiteral("startDate"), deposit.startDate().toString(Qt::ISODate)},
        {QStringLiteral("term"), termToJson(deposit.term())},
        {QStringLiteral("annualRateBasisPoints"), deposit.annualRateBasisPoints()},
        {QStringLiteral("maturityDate"), deposit.maturityDate().toString(Qt::ISODate)},
        {QStringLiteral("openingEmployeeId"), deposit.openingEmployeeId()},
        {QStringLiteral("createdAt"), deposit.createdAt().toString(Qt::ISODateWithMs)}
    };
}

QJsonObject serializeTransaction(const Transaction &transaction)
{
    return {
        {QStringLiteral("transactionId"), transaction.transactionId()},
        {QStringLiteral("accountNumber"), transaction.accountNumber()},
        {QStringLiteral("depositId"), transaction.depositId()},
        {QStringLiteral("dateTime"), transaction.dateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("type"), transactionTypeToJson(transaction.type())},
        {QStringLiteral("withdrawalKind"), withdrawalKindToJson(transaction.withdrawalKind())},
        {QStringLiteral("principalAmountCents"), QString::number(transaction.principalAmountCents())},
        {QStringLiteral("interestAmountCents"), QString::number(transaction.interestAmountCents())},
        {QStringLiteral("employeeId"), transaction.employeeId()}
    };
}

QJsonObject serializeDepositor(const Depositor &depositor)
{
    // 一个储户在同一 JSON 对象内嵌多笔独立存款及其完整交易历史。
    QJsonArray deposits;
    for (const FixedDeposit &deposit : depositor.deposits()) {
        deposits.append(serializeDeposit(deposit));
    }

    QJsonArray transactions;
    for (const Transaction &transaction : depositor.transactions()) {
        transactions.append(serializeTransaction(transaction));
    }

    QJsonObject object{
        {QStringLiteral("accountNumber"), depositor.accountNumber()},
        {QStringLiteral("name"), depositor.name()},
        {QStringLiteral("passwordSalt"), QString::fromLatin1(depositor.passwordSalt().toBase64())},
        {QStringLiteral("passwordHash"), QString::fromLatin1(depositor.passwordHash().toBase64())},
        {QStringLiteral("passwordKdfIterations"), depositor.passwordKdfIterations()},
        {QStringLiteral("passwordKdfAlgorithm"), depositor.passwordKdfAlgorithm()},
        {QStringLiteral("address"), depositor.address()},
        {QStringLiteral("lost"), depositor.isLost()},
        {QStringLiteral("openingEmployeeId"), depositor.openingEmployeeId()},
        {QStringLiteral("createdAt"), depositor.createdAt().toString(Qt::ISODateWithMs)},
        {QStringLiteral("deposits"), deposits},
        {QStringLiteral("transactions"), transactions}
    };
    object.insert(QStringLiteral("lostDate"),
                  depositor.lostDate()
                      ? QJsonValue(depositor.lostDate()->toString(Qt::ISODate))
                      : QJsonValue(QJsonValue::Null));
    return object;
}

bool deserializeDeposit(const QJsonValue &value,
                        FixedDeposit *deposit,
                        QString *errorMessage)
{
    if (!value.isObject()) {
        return fail(errorMessage, QStringLiteral("存款数组成员必须是对象"));
    }
    const QJsonObject object = value.toObject();
    QString depositId;
    qint64 originalPrincipal = 0;
    qint64 remainingPrincipal = 0;
    QDate startDate;
    QString termText;
    int rate = 0;
    QDate maturityDate;
    QString employeeId;
    QDateTime createdAt;
    if (!readString(object, QStringLiteral("depositId"), &depositId, errorMessage)
        || !readCents(object, QStringLiteral("originalPrincipalCents"), &originalPrincipal, errorMessage)
        || !readCents(object, QStringLiteral("remainingPrincipalCents"), &remainingPrincipal, errorMessage)
        || !readDate(object, QStringLiteral("startDate"), &startDate, errorMessage)
        || !readString(object, QStringLiteral("term"), &termText, errorMessage)
        || !readInteger(object, QStringLiteral("annualRateBasisPoints"), &rate, errorMessage)
        || !readDate(object, QStringLiteral("maturityDate"), &maturityDate, errorMessage)
        || !readString(object, QStringLiteral("openingEmployeeId"), &employeeId, errorMessage)
        || !readDateTime(object, QStringLiteral("createdAt"), &createdAt, errorMessage)) {
        return false;
    }
    const auto term = termFromJson(termText);
    if (!term) {
        return fail(errorMessage, QStringLiteral("存款储种无效"));
    }

    FixedDeposit candidate(depositId,
                           originalPrincipal,
                           remainingPrincipal,
                           startDate,
                           *term,
                           rate,
                           maturityDate,
                           employeeId,
                           createdAt);
    QString validationError;
    if (!candidate.isValid(&validationError)) {
        return fail(errorMessage, QStringLiteral("存款数据无效：%1").arg(validationError));
    }
    *deposit = std::move(candidate);
    return true;
}

bool deserializeTransaction(const QJsonValue &value,
                            Transaction *transaction,
                            QString *errorMessage)
{
    if (!value.isObject()) {
        return fail(errorMessage, QStringLiteral("交易数组成员必须是对象"));
    }
    const QJsonObject object = value.toObject();
    QString transactionId;
    QString accountNumber;
    QString depositId;
    QDateTime dateTime;
    QString typeText;
    QString kindText;
    qint64 principal = 0;
    qint64 interest = 0;
    QString employeeId;
    if (!readString(object, QStringLiteral("transactionId"), &transactionId, errorMessage)
        || !readString(object, QStringLiteral("accountNumber"), &accountNumber, errorMessage)
        || !readString(object, QStringLiteral("depositId"), &depositId, errorMessage)
        || !readDateTime(object, QStringLiteral("dateTime"), &dateTime, errorMessage)
        || !readString(object, QStringLiteral("type"), &typeText, errorMessage)
        || !readString(object, QStringLiteral("withdrawalKind"), &kindText, errorMessage)
        || !readCents(object, QStringLiteral("principalAmountCents"), &principal, errorMessage)
        || !readCents(object, QStringLiteral("interestAmountCents"), &interest, errorMessage)
        || !readString(object, QStringLiteral("employeeId"), &employeeId, errorMessage)) {
        return false;
    }
    const auto type = transactionTypeFromJson(typeText);
    const auto kind = withdrawalKindFromJson(kindText);
    if (!type || !kind) {
        return fail(errorMessage, QStringLiteral("交易类型或支取类型无效"));
    }

    Transaction candidate(transactionId,
                          accountNumber,
                          depositId,
                          dateTime,
                          *type,
                          *kind,
                          principal,
                          interest,
                          employeeId);
    QString validationError;
    if (!candidate.isValid(&validationError)) {
        return fail(errorMessage, QStringLiteral("交易数据无效：%1").arg(validationError));
    }
    *transaction = std::move(candidate);
    return true;
}

bool deserializeDepositor(const QJsonValue &value,
                          Depositor *depositor,
                          QString *errorMessage)
{
    if (!value.isObject()) {
        return fail(errorMessage, QStringLiteral("储户数组成员必须是对象"));
    }
    const QJsonObject object = value.toObject();
    QString accountNumber;
    QString name;
    QString saltText;
    QString hashText;
    QByteArray salt;
    QByteArray hash;
    int iterations = 0;
    QString algorithm = Depositor::supportedPasswordKdfAlgorithm();
    QString address;
    bool lost = false;
    QString employeeId;
    QDateTime createdAt;
    QJsonArray depositsArray;
    QJsonArray transactionsArray;
    if (!readString(object, QStringLiteral("accountNumber"), &accountNumber, errorMessage)
        || !readString(object, QStringLiteral("name"), &name, errorMessage)
        || !readString(object, QStringLiteral("passwordSalt"), &saltText, errorMessage)
        || !decodeBase64(saltText, &salt, QStringLiteral("passwordSalt"), errorMessage)
        || !readString(object, QStringLiteral("passwordHash"), &hashText, errorMessage)
        || !decodeBase64(hashText, &hash, QStringLiteral("passwordHash"), errorMessage)
        || !readInteger(object, QStringLiteral("passwordKdfIterations"), &iterations, errorMessage)
        || !readString(object, QStringLiteral("address"), &address, errorMessage)
        || !readBoolean(object, QStringLiteral("lost"), &lost, errorMessage)
        || !readString(object, QStringLiteral("openingEmployeeId"), &employeeId, errorMessage)
        || !readDateTime(object, QStringLiteral("createdAt"), &createdAt, errorMessage)
        || !readArray(object, QStringLiteral("deposits"), &depositsArray, errorMessage)
        || !readArray(object, QStringLiteral("transactions"), &transactionsArray, errorMessage)) {
        return false;
    }

    // 早期 Schema 1 文件没有该字段，读取时按唯一受支持算法兼容迁移。
    if (object.contains(QStringLiteral("passwordKdfAlgorithm"))
        && !readString(object,
                       QStringLiteral("passwordKdfAlgorithm"),
                       &algorithm,
                       errorMessage)) {
        return false;
    }

    QJsonValue lostDateValue;
    if (!readRequiredValue(object, QStringLiteral("lostDate"), &lostDateValue, errorMessage)) {
        return false;
    }
    std::optional<QDate> lostDate;
    if (!lostDateValue.isNull()) {
        if (!lostDateValue.isString()) {
            return fail(errorMessage, QStringLiteral("字段 lostDate 必须是日期字符串或 null"));
        }
        QDate parsedLostDate;
        if (!parseDate(lostDateValue.toString(),
                       &parsedLostDate,
                       QStringLiteral("lostDate"),
                       errorMessage)) {
            return false;
        }
        lostDate = parsedLostDate;
    }

    QVector<FixedDeposit> deposits;
    deposits.reserve(depositsArray.size());
    for (const QJsonValue &depositValue : depositsArray) {
        FixedDeposit deposit;
        if (!deserializeDeposit(depositValue, &deposit, errorMessage)) {
            return false;
        }
        deposits.append(std::move(deposit));
    }

    QVector<Transaction> transactions;
    transactions.reserve(transactionsArray.size());
    for (const QJsonValue &transactionValue : transactionsArray) {
        Transaction transaction;
        if (!deserializeTransaction(transactionValue, &transaction, errorMessage)) {
            return false;
        }
        transactions.append(std::move(transaction));
    }

    Depositor candidate(accountNumber,
                        name,
                        salt,
                        hash,
                        iterations,
                        address,
                        lost,
                        lostDate,
                        employeeId,
                        createdAt,
                        deposits,
                        transactions,
                        algorithm);
    QString validationError;
    if (!candidate.isValid(&validationError)) {
        return fail(errorMessage, QStringLiteral("储户数据无效：%1").arg(validationError));
    }
    *depositor = std::move(candidate);
    return true;
}

} // namespace

bool BankStateJsonSerializer::serialize(const BankState &state,
                                        QByteArray *jsonData,
                                        QString *errorMessage)
{
    if (!jsonData) {
        return fail(errorMessage, QStringLiteral("JSON 输出参数不能为空"));
    }

    QString validationError;
    if (!state.isValid(&validationError)) {
        return fail(errorMessage, QStringLiteral("银行状态无效：%1").arg(validationError));
    }

    QJsonArray depositors;
    for (const Depositor &depositor : state.depositors()) {
        depositors.append(serializeDepositor(depositor));
    }
    // JSON 数值无法无损覆盖全部 64 位整数，金额与序列因此保存为十进制字符串。
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), state.schemaVersion()},
        {QStringLiteral("nextAccountSequence"), QString::number(state.nextAccountSequence())},
        {QStringLiteral("nextDepositSequence"), QString::number(state.nextDepositSequence())},
        {QStringLiteral("nextTransactionSequence"), QString::number(state.nextTransactionSequence())},
        {QStringLiteral("depositors"), depositors}
    };
    *jsonData = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool BankStateJsonSerializer::deserialize(const QByteArray &jsonData,
                                          BankState *state,
                                          QString *errorMessage)
{
    if (!state) {
        return fail(errorMessage, QStringLiteral("银行状态输出参数不能为空"));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(errorMessage,
                    QStringLiteral("JSON 语法错误（位置 %1）：%2")
                        .arg(parseError.offset)
                        .arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return fail(errorMessage, QStringLiteral("核心数据根节点必须是对象"));
    }

    const QJsonObject root = document.object();
    int schemaVersion = 0;
    quint64 nextAccountSequence = 0;
    quint64 nextDepositSequence = 0;
    quint64 nextTransactionSequence = 0;
    QJsonArray depositorsArray;
    if (!readInteger(root, QStringLiteral("schemaVersion"), &schemaVersion, errorMessage)) {
        return false;
    }
    // 先判断版本，再按当前结构读取其余字段，保证未来版本得到明确的不兼容提示。
    if (schemaVersion != BankState::CurrentSchemaVersion) {
        return fail(errorMessage,
                    QStringLiteral("不支持的 Schema 版本：%1").arg(schemaVersion));
    }
    if (!readSequence(root, QStringLiteral("nextAccountSequence"), &nextAccountSequence, errorMessage)
        || !readSequence(root, QStringLiteral("nextDepositSequence"), &nextDepositSequence, errorMessage)
        || !readSequence(root,
                         QStringLiteral("nextTransactionSequence"),
                         &nextTransactionSequence,
                         errorMessage)
        || !readArray(root, QStringLiteral("depositors"), &depositorsArray, errorMessage)) {
        return false;
    }
    QVector<Depositor> depositors;
    depositors.reserve(depositorsArray.size());
    for (const QJsonValue &depositorValue : depositorsArray) {
        Depositor depositor;
        if (!deserializeDepositor(depositorValue, &depositor, errorMessage)) {
            return false;
        }
        depositors.append(std::move(depositor));
    }

    // 全部字段与子对象验证完成前只构造局部候选，失败时不污染调用者原状态。
    BankState candidate(schemaVersion,
                        nextAccountSequence,
                        nextDepositSequence,
                        nextTransactionSequence,
                        depositors);
    QString validationError;
    if (!candidate.isValid(&validationError)) {
        return fail(errorMessage, QStringLiteral("银行状态数据无效：%1").arg(validationError));
    }

    *state = std::move(candidate);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace bank::persistence

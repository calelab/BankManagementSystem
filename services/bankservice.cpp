#include "services/bankservice.h"

#include "persistence/employeefilemanager.h"
#include "security/securityutils.h"

#include <QRegularExpression>

#include <optional>
#include <utility>

namespace bank {

BankService::BankService(std::shared_ptr<persistence::FileManager> fileManager, Clock clock)
    : fileManager_(std::move(fileManager))
    , clock_(std::move(clock))
{
}

ServiceResult BankService::initialize()
{
    initialized_ = false;
    currentEmployeeId_.clear();
    currentDepositorAccount_.clear();
    validEmployeeIds_.clear();
    loginAttempts_.clear();
    state_ = BankState();

    if (!fileManager_) {
        return failed(ServiceError::DataLoadFailure, QStringLiteral("核心数据文件管理器不可用"));
    }

    const persistence::FileLoadResult stateResult = fileManager_->load();
    if (!stateResult.success) {
        return failed(ServiceError::DataLoadFailure,
                      QStringLiteral("加载核心数据失败：%1").arg(stateResult.errorMessage));
    }

    const persistence::EmployeeFileManager employeeFileManager(fileManager_->dataDirectory());
    const persistence::EmployeeLoadResult employeeResult = employeeFileManager.loadOrCreate();
    if (!employeeResult.success) {
        return failed(ServiceError::EmployeeDataFailure,
                      QStringLiteral("加载营业员数据失败：%1").arg(employeeResult.errorMessage));
    }

    state_ = stateResult.state;
    validEmployeeIds_ = employeeResult.employeeIds;
    currentEmployeeId_.clear();
    currentDepositorAccount_.clear();
    loginAttempts_.clear();
    initialized_ = true;
    return succeeded(QStringLiteral("系统数据加载成功"));
}

bool BankService::isInitialized() const
{
    return initialized_;
}

const QSet<QString> &BankService::validEmployeeIds() const
{
    return validEmployeeIds_;
}

const QString &BankService::currentEmployeeId() const
{
    return currentEmployeeId_;
}

const QString &BankService::currentDepositorAccount() const
{
    return currentDepositorAccount_;
}

const BankState &BankService::state() const
{
    return state_;
}

const Depositor *BankService::currentDepositor() const
{
    if (currentDepositorAccount_.isEmpty()) {
        return nullptr;
    }
    return state_.findDepositor(currentDepositorAccount_);
}

ServiceResult BankService::enterEmployeeSession(const QString &employeeId)
{
    if (!initialized_) {
        return failed(ServiceError::NotInitialized, QStringLiteral("系统尚未完成初始化"));
    }
    const QString normalizedId = employeeId.trimmed();
    if (!validEmployeeIds_.contains(normalizedId)) {
        return failed(ServiceError::InvalidEmployeeId, QStringLiteral("营业员工号无效"));
    }

    // 每次建立营业员会话都先清除储户，防止新营业员继承上一会话。
    currentDepositorAccount_.clear();
    currentEmployeeId_ = normalizedId;
    return succeeded(QStringLiteral("营业员已进入系统"));
}

ServiceResult BankService::switchEmployee()
{
    if (!initialized_) {
        return failed(ServiceError::NotInitialized, QStringLiteral("系统尚未完成初始化"));
    }
    currentDepositorAccount_.clear();
    currentEmployeeId_.clear();
    return succeeded(QStringLiteral("已退出当前营业员会话"));
}

ServiceResult BankService::logoutDepositor()
{
    const ServiceResult employeeRequirement = requireEmployeeSession();
    if (!employeeRequirement.success) {
        return employeeRequirement;
    }
    currentDepositorAccount_.clear();
    return succeeded(QStringLiteral("已退出储户账户"));
}

OpenAccountResult BankService::openAccount(const QString &name,
                                           const QString &address,
                                           const QString &password,
                                           const QString &passwordConfirmation)
{
    OpenAccountResult result;
    result.status = requireEmployeeSession();
    if (!result.status.success) {
        return result;
    }

    const QString normalizedName = name.trimmed();
    const QString normalizedAddress = address.trimmed();
    if (normalizedName.isEmpty() || normalizedAddress.isEmpty()) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("姓名和地址不能为空"));
        return result;
    }
    if (password != passwordConfirmation) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("两次输入的密码不一致"));
        return result;
    }

    QString credentialError;
    const auto credentials = security::SecurityUtils::createPasswordCredentials(
        password, &credentialError);
    if (!credentials) {
        result.status = failed(ServiceError::InvalidInput, credentialError);
        return result;
    }
    const QDateTime now = currentDateTime();
    if (!now.isValid()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("系统时间无效"));
        return result;
    }

    BankState candidate = state_;
    const QString accountNumber = candidate.issueAccountNumber();
    if (accountNumber.isEmpty()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("账号序列已经耗尽"));
        return result;
    }
    const Depositor depositor(accountNumber,
                              normalizedName,
                              credentials->salt,
                              credentials->hash,
                              credentials->iterations,
                              normalizedAddress,
                              false,
                              std::nullopt,
                              currentEmployeeId_,
                              now,
                              {},
                              {},
                              credentials->algorithm);
    QString modelError;
    if (!candidate.addDepositor(depositor, &modelError)) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("创建储户失败：%1").arg(modelError));
        return result;
    }

    result.status = commitCandidate(std::move(candidate), QStringLiteral("开户成功"));
    if (result.status.success) {
        result.accountNumber = accountNumber;
    }
    return result;
}

ServiceResult BankService::loginDepositor(const QString &accountNumber,
                                          const QString &password)
{
    const ServiceResult employeeRequirement = requireEmployeeSession();
    if (!employeeRequirement.success) {
        return employeeRequirement;
    }

    static const QRegularExpression accountPattern(QStringLiteral("^[0-9]+$"));
    const QString normalizedAccount = accountNumber.trimmed();
    if (!accountPattern.match(normalizedAccount).hasMatch() || password.isEmpty()) {
        return failed(ServiceError::InvalidInput, QStringLiteral("请输入有效账号和密码"));
    }
    const QDateTime now = currentDateTime();
    if (!now.isValid()) {
        return failed(ServiceError::InternalStateError, QStringLiteral("系统时间无效"));
    }

    auto attempt = loginAttempts_.find(normalizedAccount);
    if (attempt != loginAttempts_.end() && attempt->lockedUntil.isValid()) {
        if (now < attempt->lockedUntil) {
            return failed(ServiceError::AccountTemporarilyLocked,
                          QStringLiteral("登录失败次数过多，请稍后重试"));
        }
        loginAttempts_.erase(attempt);
    }

    const Depositor *depositor = state_.findDepositor(normalizedAccount);
    if (!depositor || !security::SecurityUtils::verifyPassword(password, *depositor)) {
        const bool locked = registerLoginFailure(normalizedAccount, now);
        return locked
                   ? failed(ServiceError::AccountTemporarilyLocked,
                            QStringLiteral("登录失败次数过多，账户已临时锁定 60 秒"))
                   : failed(ServiceError::AuthenticationFailed,
                            QStringLiteral("账号或密码错误"));
    }

    loginAttempts_.remove(normalizedAccount);
    currentDepositorAccount_ = normalizedAccount;
    return succeeded(depositor->isLost()
                         ? QStringLiteral("登录成功，当前账户处于挂失状态")
                         : QStringLiteral("登录成功"));
}

DepositResult BankService::addFixedDeposit(qint64 principalCents, DepositTerm term)
{
    DepositResult result;
    result.status = requireDepositorSession(false);
    if (!result.status.success) {
        return result;
    }
    if (principalCents <= 0 || termYears(term) <= 0
        || annualRateBasisPoints(term) <= 0) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("存款本金或储种无效"));
        return result;
    }
    const QDateTime now = currentDateTime();
    if (!now.isValid()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("系统时间无效"));
        return result;
    }

    BankState candidate = state_;
    Depositor *depositor = candidate.findDepositor(currentDepositorAccount_);
    if (!depositor) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("当前储户状态不存在"));
        return result;
    }
    const QString depositId = candidate.issueDepositId();
    const QString transactionId = candidate.issueTransactionId();
    if (depositId.isEmpty() || transactionId.isEmpty()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("业务编号序列已经耗尽"));
        return result;
    }

    const QDate startDate = now.date();
    const FixedDeposit deposit(depositId,
                               principalCents,
                               principalCents,
                               startDate,
                               term,
                               annualRateBasisPoints(term),
                               FixedDeposit::calculateMaturityDate(startDate, term),
                               currentEmployeeId_,
                               now);
    const Transaction transaction(transactionId,
                                  currentDepositorAccount_,
                                  depositId,
                                  now,
                                  TransactionType::Deposit,
                                  WithdrawalKind::None,
                                  principalCents,
                                  0,
                                  currentEmployeeId_);
    QString modelError;
    if (!depositor->addDeposit(deposit, &modelError)
        || !depositor->addTransaction(transaction, &modelError)) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("建立存款记录失败：%1").arg(modelError));
        return result;
    }

    result.status = commitCandidate(std::move(candidate), QStringLiteral("存款成功"));
    if (result.status.success) {
        result.depositId = depositId;
        result.transactionId = transactionId;
    }
    return result;
}

WithdrawalPreviewResult BankService::previewWithdrawal(const QString &depositId,
                                                        qint64 principalCents) const
{
    WithdrawalPreviewResult result;
    result.status = requireDepositorSession(false);
    if (!result.status.success) {
        return result;
    }
    const Depositor *depositor = currentDepositor();
    const FixedDeposit *deposit = depositor ? depositor->findDeposit(depositId) : nullptr;
    if (!deposit) {
        result.status = failed(ServiceError::DepositNotFound,
                               QStringLiteral("未找到指定存款"));
        return result;
    }
    if (principalCents <= 0) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("支取本金必须大于零"));
        return result;
    }
    if (principalCents > deposit->remainingPrincipalCents()) {
        result.status = failed(ServiceError::InsufficientPrincipal,
                               QStringLiteral("支取本金超过该笔存款的剩余本金"));
        return result;
    }

    QString calculationError;
    const QDateTime now = currentDateTime();
    if (!now.isValid()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("系统时间无效"));
        return result;
    }
    const auto calculation = InterestCalculator::withdrawal(
        *deposit, principalCents, now.date(), &calculationError);
    if (!calculation) {
        result.status = failed(ServiceError::InvalidInput, calculationError);
        return result;
    }
    result.status = succeeded(QStringLiteral("支取金额计算完成"));
    result.depositId = depositId;
    result.remainingPrincipalCents = deposit->remainingPrincipalCents();
    result.calculation = *calculation;
    return result;
}

WithdrawalResult BankService::withdraw(const QString &depositId, qint64 principalCents)
{
    WithdrawalResult result;
    result.status = requireDepositorSession(false);
    if (!result.status.success) {
        return result;
    }
    const QDateTime now = currentDateTime();
    if (!now.isValid()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("系统时间无效"));
        return result;
    }

    BankState candidate = state_;
    Depositor *depositor = candidate.findDepositor(currentDepositorAccount_);
    FixedDeposit *deposit = depositor ? depositor->findDeposit(depositId) : nullptr;
    if (!deposit) {
        result.status = failed(ServiceError::DepositNotFound,
                               QStringLiteral("未找到指定存款"));
        return result;
    }
    if (principalCents <= 0) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("支取本金必须大于零"));
        return result;
    }
    if (principalCents > deposit->remainingPrincipalCents()) {
        result.status = failed(ServiceError::InsufficientPrincipal,
                               QStringLiteral("支取本金超过该笔存款的剩余本金"));
        return result;
    }

    QString calculationError;
    const auto calculation = InterestCalculator::withdrawal(
        *deposit, principalCents, now.date(), &calculationError);
    if (!calculation) {
        result.status = failed(ServiceError::InvalidInput, calculationError);
        return result;
    }
    const QString transactionId = candidate.issueTransactionId();
    if (transactionId.isEmpty()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("交易编号序列已经耗尽"));
        return result;
    }

    // 只修改候选存款；保存失败时当前状态中的本金和编号序列都不会改变。
    const qint64 remainingPrincipal = deposit->remainingPrincipalCents() - principalCents;
    if (!deposit->setRemainingPrincipalCents(remainingPrincipal)) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("更新剩余本金失败"));
        return result;
    }
    const Transaction transaction(transactionId,
                                  currentDepositorAccount_,
                                  depositId,
                                  now,
                                  TransactionType::Withdrawal,
                                  calculation->kind,
                                  principalCents,
                                  calculation->interestCents,
                                  currentEmployeeId_);
    QString modelError;
    if (!depositor->addTransaction(transaction, &modelError)) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("建立支取记录失败：%1").arg(modelError));
        return result;
    }

    result.status = commitCandidate(std::move(candidate), QStringLiteral("支取成功"));
    if (result.status.success) {
        result.transactionId = transactionId;
        result.remainingPrincipalCents = remainingPrincipal;
        result.calculation = *calculation;
    }
    return result;
}

ServiceResult BankService::updateProfile(const QString &name, const QString &address)
{
    const ServiceResult requirement = requireDepositorSession(false);
    if (!requirement.success) {
        return requirement;
    }

    BankState candidate = state_;
    Depositor *depositor = candidate.findDepositor(currentDepositorAccount_);
    QString modelError;
    if (!depositor || !depositor->updateProfile(name, address, &modelError)) {
        return failed(depositor ? ServiceError::InvalidInput : ServiceError::InternalStateError,
                      depositor ? modelError : QStringLiteral("当前储户状态不存在"));
    }
    return commitCandidate(std::move(candidate), QStringLiteral("个人资料修改成功"));
}

ServiceResult BankService::changePassword(const QString &currentPassword,
                                          const QString &newPassword,
                                          const QString &newPasswordConfirmation)
{
    const ServiceResult requirement = requireDepositorSession(false);
    if (!requirement.success) {
        return requirement;
    }
    if (newPassword != newPasswordConfirmation) {
        return failed(ServiceError::InvalidInput, QStringLiteral("两次输入的新密码不一致"));
    }
    const Depositor *existing = currentDepositor();
    if (!existing || !security::SecurityUtils::verifyPassword(currentPassword, *existing)) {
        return failed(ServiceError::AuthenticationFailed, QStringLiteral("当前密码错误"));
    }

    QString credentialError;
    const auto credentials = security::SecurityUtils::createPasswordCredentials(
        newPassword, &credentialError);
    if (!credentials) {
        return failed(ServiceError::InvalidInput, credentialError);
    }
    BankState candidate = state_;
    Depositor *depositor = candidate.findDepositor(currentDepositorAccount_);
    QString modelError;
    if (!depositor
        || !depositor->replacePasswordCredentials(credentials->salt,
                                                   credentials->hash,
                                                   credentials->iterations,
                                                   credentials->algorithm,
                                                   &modelError)) {
        return failed(ServiceError::InternalStateError,
                      depositor ? modelError : QStringLiteral("当前储户状态不存在"));
    }
    return commitCandidate(std::move(candidate), QStringLiteral("密码修改成功"));
}

ServiceResult BankService::reportLoss()
{
    const ServiceResult requirement = requireDepositorSession(true);
    if (!requirement.success) {
        return requirement;
    }
    const Depositor *existing = currentDepositor();
    if (!existing) {
        return failed(ServiceError::InternalStateError, QStringLiteral("当前储户状态不存在"));
    }
    if (existing->isLost()) {
        return failed(ServiceError::AccountLost, QStringLiteral("账户已经处于挂失状态"));
    }
    const QDateTime now = currentDateTime();
    if (!now.isValid()) {
        return failed(ServiceError::InternalStateError, QStringLiteral("系统时间无效"));
    }

    BankState candidate = state_;
    Depositor *depositor = candidate.findDepositor(currentDepositorAccount_);
    QString modelError;
    if (!depositor || !depositor->reportLoss(now.date(), &modelError)) {
        return failed(ServiceError::InternalStateError,
                      depositor ? modelError : QStringLiteral("当前储户状态不存在"));
    }
    return commitCandidate(std::move(candidate), QStringLiteral("账户挂失成功"));
}

ServiceResult BankService::unfreezeAccount(const QString &currentPassword)
{
    const ServiceResult requirement = requireDepositorSession(true);
    if (!requirement.success) {
        return requirement;
    }
    const Depositor *existing = currentDepositor();
    if (!existing) {
        return failed(ServiceError::InternalStateError, QStringLiteral("当前储户状态不存在"));
    }
    if (!existing->isLost()) {
        return failed(ServiceError::AccountNotLost, QStringLiteral("账户当前未挂失"));
    }
    if (!security::SecurityUtils::verifyPassword(currentPassword, *existing)) {
        return failed(ServiceError::AuthenticationFailed, QStringLiteral("当前密码错误，解除挂失失败"));
    }

    BankState candidate = state_;
    Depositor *depositor = candidate.findDepositor(currentDepositorAccount_);
    if (!depositor) {
        return failed(ServiceError::InternalStateError, QStringLiteral("当前储户状态不存在"));
    }
    depositor->clearLoss();
    return commitCandidate(std::move(candidate), QStringLiteral("账户已解除挂失"));
}

ServiceResult BankService::succeeded(const QString &message)
{
    return {true, ServiceError::None, message};
}

ServiceResult BankService::failed(ServiceError error, const QString &message)
{
    return {false, error, message};
}

ServiceResult BankService::requireEmployeeSession() const
{
    if (!initialized_) {
        return failed(ServiceError::NotInitialized, QStringLiteral("系统尚未完成初始化"));
    }
    if (currentEmployeeId_.isEmpty()) {
        return failed(ServiceError::EmployeeSessionRequired,
                      QStringLiteral("请先由营业员进入系统"));
    }
    return succeeded({});
}

ServiceResult BankService::requireDepositorSession(bool allowLost) const
{
    const ServiceResult employeeRequirement = requireEmployeeSession();
    if (!employeeRequirement.success) {
        return employeeRequirement;
    }
    if (currentDepositorAccount_.isEmpty()) {
        return failed(ServiceError::DepositorSessionRequired,
                      QStringLiteral("请先登录储户账户"));
    }
    const Depositor *depositor = currentDepositor();
    if (!depositor) {
        return failed(ServiceError::InternalStateError,
                      QStringLiteral("当前储户状态不存在"));
    }
    if (!allowLost && depositor->isLost()) {
        return failed(ServiceError::AccountLost,
                      QStringLiteral("挂失账户不能办理此业务"));
    }
    return succeeded({});
}

ServiceResult BankService::commitCandidate(BankState candidate,
                                           const QString &successMessage)
{
    const persistence::FileSaveResult saveResult = fileManager_->save(candidate);
    if (!saveResult.success) {
        return failed(ServiceError::PersistenceFailure,
                      QStringLiteral("保存失败，操作未生效：%1").arg(saveResult.errorMessage));
    }
    state_ = std::move(candidate);
    return succeeded(successMessage);
}

QDateTime BankService::currentDateTime() const
{
    return clock_ ? clock_() : QDateTime::currentDateTime();
}

bool BankService::registerLoginFailure(const QString &accountNumber, const QDateTime &now)
{
    LoginAttemptState &attempt = loginAttempts_[accountNumber];
    if (attempt.lockedUntil.isValid() && now >= attempt.lockedUntil) {
        attempt = {};
    }
    ++attempt.consecutiveFailures;
    if (attempt.consecutiveFailures >= 5) {
        attempt.consecutiveFailures = 5;
        attempt.lockedUntil = now.addSecs(60);
        return true;
    }
    return false;
}

} // namespace bank

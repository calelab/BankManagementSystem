// 核心业务服务实现：在领域校验通过后以候选状态原子提交每次业务变更。
#include "services/bankservice.h"

#include "persistence/employeefilemanager.h"
#include "security/securityutils.h"

#include <QDebug>
#include <QRegularExpression>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace bank {
namespace {

bool checkedAdd(qint64 left, qint64 right, qint64 *sum)
{
    // 所有统计金额都在相加前检查上界，避免溢出后得到看似合理的负数。
    if (!sum || left < 0 || right < 0
        || left > std::numeric_limits<qint64>::max() - right) {
        return false;
    }
    *sum = left + right;
    return true;
}

} // namespace

BankService::BankService(std::shared_ptr<persistence::FileManager> fileManager, Clock clock)
    : fileManager_(std::move(fileManager))
    , clock_(std::move(clock))
{
}

ServiceResult BankService::initialize()
{
    // 先清空会话和内存状态，只有核心数据与营业员清单都成功加载才对外可用。
    initialized_ = false;
    auditLogger_.reset();
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
    auditLogger_ = std::make_unique<audit::AuditLogger>(fileManager_->dataDirectory(),
                                                        fileManager_->codec());
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

QDate BankService::businessDate() const
{
    return currentDateTime().date();
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

    // 所有写业务先修改候选副本；磁盘保存失败时原内存状态仍保持不变。
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

    result.status = commitCandidate(std::move(candidate),
                                    QStringLiteral("开户成功"),
                                    accountNumber);
    if (result.status.success) {
        result.accountNumber = accountNumber;
        appendAudit(&result.status,
                    accountNumber,
                    QStringLiteral("OPEN_ACCOUNT"),
                    0,
                    0,
                    AuditResult::Success,
                    QStringLiteral("NONE"));
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
        ServiceResult result = failed(ServiceError::InvalidInput,
                                      QStringLiteral("请输入有效账号和密码"));
        appendAudit(&result,
                    accountPattern.match(normalizedAccount).hasMatch()
                        ? normalizedAccount
                        : QString(),
                    QStringLiteral("LOGIN"),
                    0,
                    0,
                    AuditResult::Failure,
                    QStringLiteral("INVALID_INPUT"));
        return result;
    }
    const QDateTime now = currentDateTime();
    if (!now.isValid()) {
        return failed(ServiceError::InternalStateError, QStringLiteral("系统时间无效"));
    }

    auto attempt = loginAttempts_.find(normalizedAccount);
    if (attempt != loginAttempts_.end() && attempt->lockedUntil.isValid()) {
        if (now < attempt->lockedUntil) {
            ServiceResult result = failed(ServiceError::AccountTemporarilyLocked,
                                          QStringLiteral("登录失败次数过多，请稍后重试"));
            appendAudit(&result,
                        normalizedAccount,
                        QStringLiteral("LOGIN"),
                        0,
                        0,
                        AuditResult::Failure,
                        QStringLiteral("ACCOUNT_LOCKED"));
            return result;
        }
        loginAttempts_.erase(attempt);
    }

    // 不存在账号与错误密码使用相同提示，避免登录界面泄露账号是否存在。
    const Depositor *depositor = state_.findDepositor(normalizedAccount);
    if (!depositor || !security::SecurityUtils::verifyPassword(password, *depositor)) {
        const bool locked = registerLoginFailure(normalizedAccount, now);
        ServiceResult result = locked
                                   ? failed(ServiceError::AccountTemporarilyLocked,
                                            QStringLiteral("登录失败次数过多，账户已临时锁定 60 秒"))
                                   : failed(ServiceError::AuthenticationFailed,
                                            QStringLiteral("账号或密码错误"));
        appendAudit(&result,
                    normalizedAccount,
                    QStringLiteral("LOGIN"),
                    0,
                    0,
                    AuditResult::Failure,
                    locked ? QStringLiteral("ACCOUNT_LOCKED")
                           : QStringLiteral("AUTH_FAILED"));
        return result;
    }

    // 成功登录立即清除失败计数；锁定本身仅属于本次运行期防护。
    loginAttempts_.remove(normalizedAccount);
    currentDepositorAccount_ = normalizedAccount;
    ServiceResult result = succeeded(depositor->isLost()
                                         ? QStringLiteral("登录成功，当前账户处于挂失状态")
                                         : QStringLiteral("登录成功"));
    appendAudit(&result,
                normalizedAccount,
                QStringLiteral("LOGIN"),
                0,
                0,
                AuditResult::Success,
                QStringLiteral("NONE"));
    return result;
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

    // 每次存款创建新的 FixedDeposit 和对应流水，不向已有存款追加本金。
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

    result.status = commitCandidate(std::move(candidate),
                                    QStringLiteral("存款成功"),
                                    currentDepositorAccount_);
    if (result.status.success) {
        result.depositId = depositId;
        result.transactionId = transactionId;
        appendAudit(&result.status,
                    currentDepositorAccount_,
                    QStringLiteral("DEPOSIT"),
                    principalCents,
                    0,
                    AuditResult::Success,
                    QStringLiteral("NONE"));
    }
    return result;
}

WithdrawalPreviewResult BankService::previewWithdrawal(const QString &depositId,
                                                        qint64 principalCents) const
{
    // 预览沿用正式计息入口但不创建候选状态，因此用户取消不会产生任何变更。
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

    // 支取只作用于用户选定的一笔存款，不跨多笔存款自动凑款。
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

    result.status = commitCandidate(std::move(candidate),
                                    QStringLiteral("支取成功"),
                                    currentDepositorAccount_);
    if (result.status.success) {
        result.transactionId = transactionId;
        result.remainingPrincipalCents = remainingPrincipal;
        result.calculation = *calculation;
        appendAudit(&result.status,
                    currentDepositorAccount_,
                    calculation->kind == WithdrawalKind::Early
                        ? QStringLiteral("EARLY_WITHDRAW")
                        : QStringLiteral("MATURED_WITHDRAW"),
                    principalCents,
                    calculation->interestCents,
                    AuditResult::Success,
                    QStringLiteral("NONE"));
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
    ServiceResult result = commitCandidate(std::move(candidate),
                                           QStringLiteral("个人资料修改成功"),
                                           currentDepositorAccount_);
    if (result.success) {
        appendAudit(&result,
                    currentDepositorAccount_,
                    QStringLiteral("UPDATE_PROFILE"),
                    0,
                    0,
                    AuditResult::Success,
                    QStringLiteral("NONE"));
    }
    return result;
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
    // 修改密码重新生成随机 Salt，不能沿用旧凭据中的 Salt。
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
    ServiceResult result = commitCandidate(std::move(candidate),
                                           QStringLiteral("密码修改成功"),
                                           currentDepositorAccount_);
    if (result.success) {
        appendAudit(&result,
                    currentDepositorAccount_,
                    QStringLiteral("CHANGE_PASSWORD"),
                    0,
                    0,
                    AuditResult::Success,
                    QStringLiteral("NONE"));
    }
    return result;
}

ServiceResult BankService::reportLoss()
{
    // 挂失后仍允许账户登录和查看，但 requireDepositorSession(false) 会禁止交易与修改。
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
    ServiceResult result = commitCandidate(std::move(candidate),
                                           QStringLiteral("账户挂失成功"),
                                           currentDepositorAccount_);
    if (result.success) {
        appendAudit(&result,
                    currentDepositorAccount_,
                    QStringLiteral("REPORT_LOSS"),
                    0,
                    0,
                    AuditResult::Success,
                    QStringLiteral("NONE"));
    }
    return result;
}

ServiceResult BankService::unfreezeAccount(const QString &currentPassword)
{
    // 解除挂失是敏感操作，必须再次验证当前密码并记录成功或失败审计。
    const ServiceResult requirement = requireDepositorSession(true);
    if (!requirement.success) {
        return requirement;
    }
    const Depositor *existing = currentDepositor();
    if (!existing) {
        return failed(ServiceError::InternalStateError, QStringLiteral("当前储户状态不存在"));
    }
    if (!existing->isLost()) {
        ServiceResult result = failed(ServiceError::AccountNotLost,
                                      QStringLiteral("账户当前未挂失"));
        appendAudit(&result,
                    currentDepositorAccount_,
                    QStringLiteral("UNFREEZE_ACCOUNT"),
                    0,
                    0,
                    AuditResult::Failure,
                    QStringLiteral("ACCOUNT_NOT_LOST"));
        return result;
    }
    if (!security::SecurityUtils::verifyPassword(currentPassword, *existing)) {
        ServiceResult result = failed(ServiceError::AuthenticationFailed,
                                      QStringLiteral("当前密码错误，解除挂失失败"));
        appendAudit(&result,
                    currentDepositorAccount_,
                    QStringLiteral("UNFREEZE_ACCOUNT"),
                    0,
                    0,
                    AuditResult::Failure,
                    QStringLiteral("AUTH_FAILED"));
        return result;
    }

    BankState candidate = state_;
    Depositor *depositor = candidate.findDepositor(currentDepositorAccount_);
    if (!depositor) {
        return failed(ServiceError::InternalStateError, QStringLiteral("当前储户状态不存在"));
    }
    depositor->clearLoss();
    ServiceResult result = commitCandidate(std::move(candidate),
                                           QStringLiteral("账户已解除挂失"),
                                           currentDepositorAccount_);
    appendAudit(&result,
                currentDepositorAccount_,
                QStringLiteral("UNFREEZE_ACCOUNT"),
                0,
                0,
                result.success ? AuditResult::Success : AuditResult::Failure,
                result.success ? QStringLiteral("NONE")
                               : QStringLiteral("PERSISTENCE_ERROR"));
    return result;
}

DepositorQueryResult BankService::queryDepositors(
    const QString &exactAccountNumber,
    const QString &nameContains,
    DepositorStatusFilter statusFilter) const
{
    DepositorQueryResult result;
    result.status = requireEmployeeSession();
    if (!result.status.success) {
        return result;
    }

    static const QRegularExpression accountPattern(QStringLiteral("^[0-9]+$"));
    const QString normalizedAccount = exactAccountNumber.trimmed();
    const QString normalizedName = nameContains.trimmed();
    if (!normalizedAccount.isEmpty()
        && !accountPattern.match(normalizedAccount).hasMatch()) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("查询账号格式无效"));
        return result;
    }
    if (statusFilter != DepositorStatusFilter::All
        && statusFilter != DepositorStatusFilter::Normal
        && statusFilter != DepositorStatusFilter::Lost) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("账户状态筛选条件无效"));
        return result;
    }

    // 查询只组装脱敏摘要，密码派生信息与完整交易不会离开服务层。
    for (const Depositor &depositor : state_.depositors()) {
        if (!normalizedAccount.isEmpty()
            && depositor.accountNumber() != normalizedAccount) {
            continue;
        }
        if (!normalizedName.isEmpty()
            && !depositor.name().contains(normalizedName, Qt::CaseInsensitive)) {
            continue;
        }
        if ((statusFilter == DepositorStatusFilter::Normal && depositor.isLost())
            || (statusFilter == DepositorStatusFilter::Lost && !depositor.isLost())) {
            continue;
        }

        qint64 remainingPrincipal = 0;
        for (const FixedDeposit &deposit : depositor.deposits()) {
            if (!checkedAdd(remainingPrincipal,
                            deposit.remainingPrincipalCents(),
                            &remainingPrincipal)) {
                result.depositors.clear();
                result.status = failed(ServiceError::InternalStateError,
                                       QStringLiteral("储户剩余本金合计超出可表示范围"));
                return result;
            }
        }

        if (depositor.deposits().size() > std::numeric_limits<int>::max()) {
            result.depositors.clear();
            result.status = failed(ServiceError::InternalStateError,
                                   QStringLiteral("储户存款笔数超出可表示范围"));
            return result;
        }

        DepositorSummary summary;
        summary.accountNumber = depositor.accountNumber();
        summary.name = depositor.name();
        summary.address = depositor.address();
        summary.lost = depositor.isLost();
        summary.lostDate = depositor.lostDate();
        summary.openingEmployeeId = depositor.openingEmployeeId();
        summary.depositCount = static_cast<int>(depositor.deposits().size());
        summary.remainingPrincipalCents = remainingPrincipal;
        result.depositors.append(std::move(summary));
    }

    std::sort(result.depositors.begin(),
              result.depositors.end(),
              [](const DepositorSummary &left, const DepositorSummary &right) {
                  return left.accountNumber < right.accountNumber;
              });
    result.status = succeeded(QStringLiteral("储户查询完成"));
    return result;
}

AccountDetailsResult BankService::currentAccountDetails() const
{
    AccountDetailsResult result;
    result.status = requireDepositorSession(true);
    if (!result.status.success) {
        return result;
    }
    const Depositor *depositor = currentDepositor();
    if (!depositor) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("当前储户状态不存在"));
        return result;
    }

    AccountDetails details;
    details.accountNumber = depositor->accountNumber();
    details.name = depositor->name();
    details.address = depositor->address();
    details.lost = depositor->isLost();
    details.lostDate = depositor->lostDate();
    details.openingEmployeeId = depositor->openingEmployeeId();
    details.createdAt = depositor->createdAt();
    details.deposits = depositor->deposits();
    details.transactions = depositor->transactions();
    result.details = std::move(details);
    result.status = succeeded(QStringLiteral("账户明细查询完成"));
    return result;
}

ReserveForecastResult BankService::reserveForecast(const QDate &baseDate) const
{
    ReserveForecastResult result;
    result.status = requireEmployeeSession();
    if (!result.status.success) {
        return result;
    }

    const QDate effectiveBaseDate = baseDate.isValid()
                                        ? baseDate
                                        : currentDateTime().date();
    if (!effectiveBaseDate.isValid()) {
        result.status = failed(ServiceError::InternalStateError,
                               QStringLiteral("三日备款基准日期无效"));
        return result;
    }
    // 今天不计入；即使某天没有到期存款，也预先保留明天起连续三行零值结果。
    for (int offset = 1; offset <= 3; ++offset) {
        DailyReserveForecast day;
        day.date = effectiveBaseDate.addDays(offset);
        if (!day.date.isValid()) {
            result.days.clear();
            result.status = failed(ServiceError::InternalStateError,
                                   QStringLiteral("三日备款日期超出可表示范围"));
            return result;
        }
        result.days.append(day);
    }

    for (const Depositor &depositor : state_.depositors()) {
        for (const FixedDeposit &deposit : depositor.deposits()) {
            if (deposit.remainingPrincipalCents() <= 0) {
                continue;
            }
            const qint64 dayIndex = effectiveBaseDate.daysTo(deposit.maturityDate()) - 1;
            if (dayIndex < 0 || dayIndex >= result.days.size()) {
                continue;
            }

            QString calculationError;
            // 以剩余本金复用统一到期计息；每笔先舍入到分，再用 qint64 汇总。
            const auto interest = InterestCalculator::maturedInterest(
                deposit.remainingPrincipalCents(),
                deposit.annualRateBasisPoints(),
                termYears(deposit.term()),
                &calculationError);
            if (!interest) {
                result.days.clear();
                result.status = failed(
                    ServiceError::InternalStateError,
                    QStringLiteral("三日备款利息计算失败：%1").arg(calculationError));
                return result;
            }

            DailyReserveForecast &day = result.days[static_cast<qsizetype>(dayIndex)];
            qint64 principalSum = 0;
            qint64 interestSum = 0;
            qint64 reserveSum = 0;
            if (day.depositCount == std::numeric_limits<int>::max()
                || !checkedAdd(day.principalCents,
                               deposit.remainingPrincipalCents(),
                               &principalSum)
                || !checkedAdd(day.interestCents, *interest, &interestSum)
                || !checkedAdd(principalSum, interestSum, &reserveSum)) {
                result.days.clear();
                result.status = failed(ServiceError::InternalStateError,
                                       QStringLiteral("三日备款金额合计超出可表示范围"));
                return result;
            }
            ++day.depositCount;
            day.principalCents = principalSum;
            day.interestCents = interestSum;
            day.reserveCents = reserveSum;
        }
    }

    for (const DailyReserveForecast &day : result.days) {
        if (!checkedAdd(result.totalReserveCents,
                        day.reserveCents,
                        &result.totalReserveCents)) {
            result.days.clear();
            result.totalReserveCents = 0;
            result.status = failed(ServiceError::InternalStateError,
                                   QStringLiteral("三日备款总计超出可表示范围"));
            return result;
        }
    }
    result.status = succeeded(QStringLiteral("三日备款计算完成"));
    return result;
}

AuditQueryResult BankService::currentEmployeeAudit(const QString &actionFilter) const
{
    AuditQueryResult result;
    result.status = requireEmployeeSession();
    if (!result.status.success) {
        return result;
    }
    if (!auditLogger_) {
        result.status = failed(ServiceError::AuditLoadFailure,
                               QStringLiteral("审计日志服务不可用"));
        return result;
    }

    static const QRegularExpression actionPattern(QStringLiteral("^[A-Z][A-Z0-9_]*$"));
    const QString normalizedAction = actionFilter.trimmed();
    if (!normalizedAction.isEmpty()
        && !actionPattern.match(normalizedAction).hasMatch()) {
        result.status = failed(ServiceError::InvalidInput,
                               QStringLiteral("审计动作筛选码无效"));
        return result;
    }

    const audit::AuditLoadResult loadResult = auditLogger_->loadForEmployee(currentEmployeeId_);
    if (!loadResult.success) {
        qWarning().noquote() << QStringLiteral("审计日志读取失败：%1")
                                    .arg(loadResult.errorMessage);
        result.status = failed(ServiceError::AuditLoadFailure,
                               QStringLiteral("加载审计日志失败：%1")
                                   .arg(loadResult.errorMessage));
        return result;
    }
    for (const AuditRecord &record : loadResult.records) {
        if (normalizedAction.isEmpty() || record.action() == normalizedAction) {
            result.records.append(record);
        }
    }
    result.status = succeeded(QStringLiteral("审计日志查询完成"));
    return result;
}

ServiceResult BankService::succeeded(const QString &message)
{
    return {true, ServiceError::None, message, {}};
}

ServiceResult BankService::failed(ServiceError error, const QString &message)
{
    return {false, error, message, {}};
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
                                           const QString &successMessage,
                                           const QString &auditAccountNumber)
{
    // QSaveFile 完成原子替换后才发布候选状态，保证内存与磁盘不会各成功一半。
    const persistence::FileSaveResult saveResult = fileManager_->save(candidate);
    if (!saveResult.success) {
        ServiceResult result = failed(
            ServiceError::PersistenceFailure,
            QStringLiteral("保存失败，操作未生效：%1").arg(saveResult.errorMessage));
        appendAudit(&result,
                    auditAccountNumber,
                    QStringLiteral("CORE_DATA_SAVE"),
                    0,
                    0,
                    AuditResult::Warning,
                    QStringLiteral("PERSISTENCE_ERROR"));
        return result;
    }
    state_ = std::move(candidate);
    return succeeded(successMessage);
}

void BankService::appendAudit(ServiceResult *status,
                              const QString &accountNumber,
                              const QString &action,
                              qint64 principalAmountCents,
                              qint64 interestAmountCents,
                              AuditResult result,
                              const QString &reasonCode) const
{
    // 审计是附属记录：失败会成为显式警告，但不能回滚已经成功落盘的核心业务。
    if (!status) {
        return;
    }

    QString errorMessage;
    const QDateTime now = currentDateTime();
    if (!auditLogger_) {
        errorMessage = QStringLiteral("审计日志服务不可用");
    } else if (currentEmployeeId_.isEmpty()) {
        errorMessage = QStringLiteral("当前营业员会话不存在");
    } else if (!now.isValid()) {
        errorMessage = QStringLiteral("审计时间无效");
    } else {
        const AuditRecord record(now,
                                 currentEmployeeId_,
                                 accountNumber,
                                 action,
                                 principalAmountCents,
                                 interestAmountCents,
                                 result,
                                 reasonCode);
        const audit::AuditAppendResult appendResult = auditLogger_->append(record);
        if (appendResult.success) {
            return;
        }
        errorMessage = appendResult.errorMessage;
    }

    status->warningMessage = status->success
                                 ? QStringLiteral("业务已完成，审计日志保存失败：%1")
                                       .arg(errorMessage)
                                 : QStringLiteral("审计日志保存失败：%1").arg(errorMessage);
    // 运行日志只记录动作和非敏感错误，不输出密码、哈希、Salt 或主密钥。
    qWarning().noquote() << QStringLiteral("审计日志写入失败 [%1]：%2")
                                .arg(action, errorMessage);
}

QDateTime BankService::currentDateTime() const
{
    return clock_ ? clock_() : QDateTime::currentDateTime();
}

bool BankService::registerLoginFailure(const QString &accountNumber, const QDateTime &now)
{
    // 五次连续失败锁定 60 秒；该表不持久化，避免把安全运行状态混入业务文件。
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

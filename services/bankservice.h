#ifndef BANKSERVICE_H
#define BANKSERVICE_H

#include "models/bankstate.h"
#include "persistence/filemanager.h"
#include "services/interestcalculator.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>

namespace bank {

enum class ServiceError {
    None,
    NotInitialized,
    DataLoadFailure,
    EmployeeDataFailure,
    EmployeeSessionRequired,
    InvalidEmployeeId,
    DepositorSessionRequired,
    InvalidInput,
    AuthenticationFailed,
    AccountTemporarilyLocked,
    AccountLost,
    AccountNotLost,
    DepositNotFound,
    InsufficientPrincipal,
    PersistenceFailure,
    InternalStateError
};

struct ServiceResult {
    bool success = false;
    ServiceError error = ServiceError::None;
    QString message;
};

struct OpenAccountResult {
    ServiceResult status;
    QString accountNumber;
};

struct DepositResult {
    ServiceResult status;
    QString depositId;
    QString transactionId;
};

struct WithdrawalPreviewResult {
    ServiceResult status;
    QString depositId;
    qint64 remainingPrincipalCents = 0;
    WithdrawalCalculation calculation;
};

struct WithdrawalResult {
    ServiceResult status;
    QString transactionId;
    qint64 remainingPrincipalCents = 0;
    WithdrawalCalculation calculation;
};

// BankService 是 UI 的唯一核心业务入口，负责会话、校验、候选状态和即时保存。
class BankService
{
public:
    using Clock = std::function<QDateTime()>;

    explicit BankService(std::shared_ptr<persistence::FileManager> fileManager,
                         Clock clock = {});

    // 加载核心数据及 employees.dat；任一文件错误都会阻止建立工作会话。
    ServiceResult initialize();

    bool isInitialized() const;
    const QSet<QString> &validEmployeeIds() const;
    const QString &currentEmployeeId() const;
    const QString &currentDepositorAccount() const;
    const BankState &state() const;
    const Depositor *currentDepositor() const;

    ServiceResult enterEmployeeSession(const QString &employeeId);
    ServiceResult switchEmployee();
    ServiceResult logoutDepositor();

    // 开户不自动登录储户；成功结果返回系统生成且已经持久化的账号。
    OpenAccountResult openAccount(const QString &name,
                                  const QString &address,
                                  const QString &password,
                                  const QString &passwordConfirmation);

    // 同一账号连续失败五次后按运行期时钟锁定六十秒，成功登录会清零记录。
    ServiceResult loginDepositor(const QString &accountNumber, const QString &password);

    // 每次存款创建独立 FixedDeposit 及对应交易，不向旧存款追加本金。
    DepositResult addFixedDeposit(qint64 principalCents, DepositTerm term);

    // 预览只计算不修改；正式支取会重新校验并在保存成功后提交候选状态。
    WithdrawalPreviewResult previewWithdrawal(const QString &depositId,
                                               qint64 principalCents) const;
    WithdrawalResult withdraw(const QString &depositId, qint64 principalCents);

    ServiceResult updateProfile(const QString &name, const QString &address);
    ServiceResult changePassword(const QString &currentPassword,
                                 const QString &newPassword,
                                 const QString &newPasswordConfirmation);
    ServiceResult reportLoss();
    ServiceResult unfreezeAccount(const QString &currentPassword);

private:
    struct LoginAttemptState {
        int consecutiveFailures = 0;
        QDateTime lockedUntil;
    };

    static ServiceResult succeeded(const QString &message);
    static ServiceResult failed(ServiceError error, const QString &message);
    ServiceResult requireEmployeeSession() const;
    ServiceResult requireDepositorSession(bool allowLost) const;
    ServiceResult commitCandidate(BankState candidate, const QString &successMessage);
    QDateTime currentDateTime() const;
    bool registerLoginFailure(const QString &accountNumber, const QDateTime &now);

    std::shared_ptr<persistence::FileManager> fileManager_;
    Clock clock_;
    BankState state_;
    QSet<QString> validEmployeeIds_;
    QString currentEmployeeId_;
    QString currentDepositorAccount_;
    QHash<QString, LoginAttemptState> loginAttempts_;
    bool initialized_ = false;
};

} // namespace bank

#endif // BANKSERVICE_H

// 核心业务服务声明：统一承接会话、账户业务、查询、持久化和审计协调。
#ifndef BANKSERVICE_H
#define BANKSERVICE_H

#include "audit/auditlogger.h"
#include "models/bankstate.h"
#include "persistence/filemanager.h"
#include "services/interestcalculator.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

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
    AuditLoadFailure,
    InternalStateError
};

struct ServiceResult {
    bool success = false;
    ServiceError error = ServiceError::None;
    QString message;
    // 核心业务已成功但审计写入失败时单独提示，不把已提交业务误报为失败。
    QString warningMessage;
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

enum class DepositorStatusFilter {
    All,
    Normal,
    Lost
};

// 营业员查询只返回业务展示字段，不向 UI 暴露密码 Salt、哈希或派生参数。
struct DepositorSummary {
    QString accountNumber;
    QString name;
    QString address;
    bool lost = false;
    std::optional<QDate> lostDate;
    QString openingEmployeeId;
    int depositCount = 0;
    qint64 remainingPrincipalCents = 0;
};

struct DepositorQueryResult {
    ServiceResult status;
    QVector<DepositorSummary> depositors;
};

// 完整存款和交易明细仅由当前已经通过密码登录的储户会话取得。
struct AccountDetails {
    QString accountNumber;
    QString name;
    QString address;
    bool lost = false;
    std::optional<QDate> lostDate;
    QString openingEmployeeId;
    QDateTime createdAt;
    QVector<FixedDeposit> deposits;
    QVector<Transaction> transactions;
};

struct AccountDetailsResult {
    ServiceResult status;
    std::optional<AccountDetails> details;
};

struct DailyReserveForecast {
    QDate date;
    int depositCount = 0;
    // 每笔金额先按统一规则舍入到分，再使用 qint64 汇总当天结果。
    qint64 principalCents = 0;
    qint64 interestCents = 0;
    qint64 reserveCents = 0;
};

struct ReserveForecastResult {
    ServiceResult status;
    QVector<DailyReserveForecast> days;
    qint64 totalReserveCents = 0;
};

struct AuditQueryResult {
    ServiceResult status;
    QVector<AuditRecord> records;
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

    // 返回服务注入时钟对应的业务日期，供界面显示存入日等非决策信息。
    QDate businessDate() const;

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

    // 三个查询条件可组合；空账号/姓名表示不启用相应条件。
    DepositorQueryResult queryDepositors(
        const QString &exactAccountNumber = {},
        const QString &nameContains = {},
        DepositorStatusFilter statusFilter = DepositorStatusFilter::All) const;

    AccountDetailsResult currentAccountDetails() const;

    // 空基准日期使用注入时钟的当前日期，结果始终包含明天起连续三天。
    ReserveForecastResult reserveForecast(const QDate &baseDate = {}) const;

    // 默认读取当前营业员的全部记录，动作筛选采用稳定动作码精确匹配。
    AuditQueryResult currentEmployeeAudit(const QString &actionFilter = {}) const;

private:
    struct LoginAttemptState {
        // 登录限制只保存在本次程序运行期，成功登录或锁定期届满后清除。
        int consecutiveFailures = 0;
        QDateTime lockedUntil;
    };

    static ServiceResult succeeded(const QString &message);
    static ServiceResult failed(ServiceError error, const QString &message);
    ServiceResult requireEmployeeSession() const;
    ServiceResult requireDepositorSession(bool allowLost) const;
    ServiceResult commitCandidate(BankState candidate,
                                  const QString &successMessage,
                                  const QString &auditAccountNumber = {});
    void appendAudit(ServiceResult *status,
                     const QString &accountNumber,
                     const QString &action,
                     qint64 principalAmountCents,
                     qint64 interestAmountCents,
                     AuditResult result,
                     const QString &reasonCode) const;
    QDateTime currentDateTime() const;
    bool registerLoginFailure(const QString &accountNumber, const QDateTime &now);

    // 文件管理器和审计器负责磁盘边界；服务层只协调业务提交顺序。
    std::shared_ptr<persistence::FileManager> fileManager_;
    std::unique_ptr<audit::AuditLogger> auditLogger_;
    // 可注入时钟使到期、锁定和三日统计测试不依赖真实当天。
    Clock clock_;
    BankState state_;
    QSet<QString> validEmployeeIds_;
    // 营业员与储户是两层会话，完整账户明细必须经过储户密码登录。
    QString currentEmployeeId_;
    QString currentDepositorAccount_;
    QHash<QString, LoginAttemptState> loginAttempts_;
    bool initialized_ = false;
};

} // namespace bank

#endif // BANKSERVICE_H

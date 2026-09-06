// 主窗口声明：负责页面导航、展示模型以及界面与业务服务的连接。
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "services/bankservice.h"

#include <QMainWindow>

#include <memory>

QT_BEGIN_NAMESPACE
class QEvent;
class QLabel;
class QStandardItemModel;
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// MainWindow 只组织页面、信号槽和展示模型，所有业务判断统一委托给 BankService。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    explicit MainWindow(std::unique_ptr<bank::BankService> service,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 信号槽只负责收集输入和刷新展示，所有业务判断由 BankService 完成。
    void setupConnections();
    void setupTableModels();
    void initializeService();
    void updateSessionDisplay();
    void showWorkspace();
    void showServiceResult(const bank::ServiceResult &result, QLabel *targetLabel);

    void enterEmployeeSession();
    void switchEmployee();
    void openAccountPage();
    void createAccount();
    void depositorLoginPage();
    void loginDepositor();
    void logoutDepositor();

    void refreshAccountCenter();
    void clearAccountPresentation();
    void updateAccountActionState();
    QString selectedDepositId() const;
    void createFixedDeposit();
    void withdrawSelectedDeposit();
    void editProfile();
    void changePassword();
    void reportLoss();
    void unfreezeAccount();

    void showAllDepositors();
    void refreshAllDepositors();
    void resetDepositorFilter();
    void showReserveForecast();
    void refreshReserveForecast();
    void showAuditLog();
    void refreshAuditLog();

    // ui 来自 Designer 生成类；这些标准模型只承载当前页面的只读展示数据。
    Ui::MainWindow *ui;
    std::unique_ptr<bank::BankService> bankService_;
    // 依次对应定期存款、交易、全部储户、三日备款和审计日志五张表。
    QStandardItemModel *depositsModel_ = nullptr;
    QStandardItemModel *transactionsModel_ = nullptr;
    QStandardItemModel *depositorsModel_ = nullptr;
    QStandardItemModel *reserveModel_ = nullptr;
    QStandardItemModel *auditModel_ = nullptr;
};

#endif // MAINWINDOW_H

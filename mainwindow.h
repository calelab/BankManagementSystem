#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "services/bankservice.h"

#include <QMainWindow>

#include <memory>

QT_BEGIN_NAMESPACE
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

private:
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

    Ui::MainWindow *ui;
    std::unique_ptr<bank::BankService> bankService_;
    QStandardItemModel *depositsModel_ = nullptr;
    QStandardItemModel *transactionsModel_ = nullptr;
    QStandardItemModel *depositorsModel_ = nullptr;
    QStandardItemModel *reserveModel_ = nullptr;
    QStandardItemModel *auditModel_ = nullptr;
};

#endif // MAINWINDOW_H

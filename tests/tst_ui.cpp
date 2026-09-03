#include "mainwindow.h"

#include "persistence/filemanager.h"
#include "ui/depositdialog.h"
#include "ui/passworddialog.h"
#include "ui/profiledialog.h"
#include "ui/unfreezedialog.h"
#include "ui/withdrawdialog.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

using namespace bank;
using namespace bank::persistence;

namespace {

template<typename T>
T *requiredChild(QObject *parent, const char *name)
{
    T *child = parent ? parent->findChild<T *>(QString::fromLatin1(name)) : nullptr;
    if (!child) {
        qFatal("缺少界面控件：%s", name);
    }
    return child;
}

std::unique_ptr<BankService> makeService(const QString &dataDirectory,
                                         QDateTime *now)
{
    auto fileManager = std::make_shared<FileManager>(dataDirectory);
    return std::make_unique<BankService>(fileManager, [now] { return *now; });
}

void showAndProcess(MainWindow *window)
{
    window->show();
    QApplication::processEvents();
}

} // namespace

// UI 测试在临时数据目录中通过真实按钮和对话框执行业务，验证页面状态而不绕过 BankService。
class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void definesRequiredDesignerControlsAndDefaults();
    void completesMainWorkflowThroughUiConnections();
    void filtersClosedDepositsAndDisablesWithdrawal();
};

void MainWindowTest::definesRequiredDesignerControlsAndDefaults()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDateTime now(QDate(2026, 1, 1), QTime(9, 0));
    MainWindow window(makeService(temporaryDirectory.path(), &now));

    auto *stack = requiredChild<QStackedWidget>(&window, "mainStackedWidget");
    QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("employeeLoginPage"));
    const QStringList requiredMainObjects{
        QStringLiteral("employeeIdEdit"),
        QStringLiteral("employeeEnterButton"),
        QStringLiteral("currentEmployeeLabel"),
        QStringLiteral("currentDepositorLabel"),
        QStringLiteral("openAccountButton"),
        QStringLiteral("depositorLoginButton"),
        QStringLiteral("allDepositorsButton"),
        QStringLiteral("reserveForecastButton"),
        QStringLiteral("auditLogButton"),
        QStringLiteral("switchEmployeeButton"),
        QStringLiteral("openAccountNameEdit"),
        QStringLiteral("openAccountAddressEdit"),
        QStringLiteral("openAccountPasswordEdit"),
        QStringLiteral("openAccountConfirmPasswordEdit"),
        QStringLiteral("accountNumberEdit"),
        QStringLiteral("accountPasswordEdit"),
        QStringLiteral("depositsTableView"),
        QStringLiteral("transactionsTableView"),
        QStringLiteral("showClosedDepositsCheckBox"),
        QStringLiteral("allDepositorsTableView"),
        QStringLiteral("forecastBaseDateEdit"),
        QStringLiteral("reserveForecastTableView"),
        QStringLiteral("auditLogTableView"),
        QStringLiteral("auditActionComboBox")};
    for (const QString &name : requiredMainObjects) {
        QVERIFY2(window.findChild<QObject *>(name), qPrintable(name));
    }

    QCOMPARE(requiredChild<QLineEdit>(&window, "openAccountPasswordEdit")->echoMode(),
             QLineEdit::Password);
    QCOMPARE(requiredChild<QLineEdit>(&window, "openAccountConfirmPasswordEdit")->echoMode(),
             QLineEdit::Password);
    QCOMPARE(requiredChild<QLineEdit>(&window, "accountPasswordEdit")->echoMode(),
             QLineEdit::Password);
    QVERIFY(!requiredChild<QCheckBox>(&window, "showClosedDepositsCheckBox")->isChecked());
    QVERIFY(!requiredChild<QAction>(&window, "actionLogoutDepositor")->isEnabled());

    DepositDialog depositDialog(now.date());
    auto *termGroup = requiredChild<QButtonGroup>(&depositDialog,
                                                   "depositTermButtonGroup");
    QVERIFY(termGroup->exclusive());
    QVERIFY(requiredChild<QRadioButton>(&depositDialog,
                                        "oneYearTermRadioButton")
                ->isChecked());
    QVERIFY(requiredChild<QObject>(&depositDialog, "depositAmountEdit"));
    QVERIFY(requiredChild<QObject>(&depositDialog, "depositRateLabel"));
    QVERIFY(requiredChild<QObject>(&depositDialog, "depositStartDateLabel"));
    QVERIFY(requiredChild<QObject>(&depositDialog, "depositMaturityDateLabel"));

    PasswordDialog passwordDialog;
    QCOMPARE(requiredChild<QLineEdit>(&passwordDialog, "currentPasswordEdit")->echoMode(),
             QLineEdit::Password);
    QCOMPARE(requiredChild<QLineEdit>(&passwordDialog, "newPasswordEdit")->echoMode(),
             QLineEdit::Password);
    QCOMPARE(requiredChild<QLineEdit>(&passwordDialog, "confirmNewPasswordEdit")->echoMode(),
             QLineEdit::Password);

    UnfreezeDialog unfreezeDialog;
    QCOMPARE(requiredChild<QLineEdit>(&unfreezeDialog, "unfreezePasswordEdit")->echoMode(),
             QLineEdit::Password);
    ProfileDialog profileDialog(QStringLiteral("张三"), QStringLiteral("上海市"));
    QVERIFY(requiredChild<QObject>(&profileDialog, "profileNameEdit"));
    WithdrawDialog withdrawDialog(nullptr, QStringLiteral("FD000001"));
    QVERIFY(requiredChild<QObject>(&withdrawDialog, "withdrawPayoutLabel"));
}

void MainWindowTest::completesMainWorkflowThroughUiConnections()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDateTime now(QDate(2026, 1, 1), QTime(9, 0));
    const QString oldPassword = QStringLiteral("SafePass123");
    const QString newPassword = QStringLiteral("NewSafePass456");

    {
        MainWindow window(makeService(temporaryDirectory.path(), &now));
        showAndProcess(&window);
        auto *stack = requiredChild<QStackedWidget>(&window, "mainStackedWidget");

        requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E03"));
        requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("workspacePage"));
        QVERIFY(requiredChild<QLabel>(&window, "currentEmployeeLabel")
                    ->text()
                    .contains(QStringLiteral("E03")));

        requiredChild<QPushButton>(&window, "openAccountButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("openAccountPage"));
        requiredChild<QLineEdit>(&window, "openAccountNameEdit")
            ->setText(QStringLiteral("张三"));
        requiredChild<QLineEdit>(&window, "openAccountAddressEdit")
            ->setText(QStringLiteral("上海市"));
        requiredChild<QLineEdit>(&window, "openAccountPasswordEdit")->setText(oldPassword);
        requiredChild<QLineEdit>(&window, "openAccountConfirmPasswordEdit")
            ->setText(oldPassword);
        requiredChild<QPushButton>(&window, "createAccountButton")->click();
        QVERIFY(requiredChild<QLabel>(&window, "openAccountMessageLabel")
                    ->text()
                    .contains(QStringLiteral("100001")));

        requiredChild<QPushButton>(&window, "openAccountCancelButton")->click();
        requiredChild<QPushButton>(&window, "depositorLoginButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("depositorLoginPage"));
        QCOMPARE(requiredChild<QLineEdit>(&window, "accountNumberEdit")->text(),
                 QStringLiteral("100001"));
        requiredChild<QLineEdit>(&window, "accountPasswordEdit")->setText(oldPassword);
        requiredChild<QPushButton>(&window, "accountLoginButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("accountCenterPage"));
        QVERIFY(requiredChild<QPushButton>(&window, "newDepositButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "unfreezeAccountButton")->isEnabled());

        bool depositDialogHandled = false;
        QTimer::singleShot(0, &window, [&depositDialogHandled] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "depositAmountEdit")
                ->setText(QStringLiteral("1000.00"));
            requiredChild<QPushButton>(dialog, "confirmDepositButton")->click();
            depositDialogHandled = true;
        });
        requiredChild<QPushButton>(&window, "newDepositButton")->click();
        QVERIFY(depositDialogHandled);

        bool secondDepositDialogHandled = false;
        QTimer::singleShot(0, &window, [&secondDepositDialogHandled] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "depositAmountEdit")
                ->setText(QStringLiteral("2000.00"));
            requiredChild<QRadioButton>(dialog, "threeYearTermRadioButton")
                ->setChecked(true);
            requiredChild<QPushButton>(dialog, "confirmDepositButton")->click();
            secondDepositDialogHandled = true;
        });
        requiredChild<QPushButton>(&window, "newDepositButton")->click();
        QVERIFY(secondDepositDialogHandled);

        auto *deposits = requiredChild<QTableView>(&window, "depositsTableView");
        auto *transactions = requiredChild<QTableView>(&window, "transactionsTableView");
        QCOMPARE(deposits->model()->rowCount(), 2);
        QCOMPARE(transactions->model()->rowCount(), 2);
        QCOMPARE(deposits->model()->index(0, 2).data().toString(),
                 QStringLiteral("¥1,000.00"));
        QCOMPARE(deposits->model()->index(1, 3).data().toString(),
                 QStringLiteral("三年期定期"));
        deposits->setCurrentIndex(deposits->model()->index(0, 0));
        QApplication::processEvents();
        QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());

        bool withdrawDialogHandled = false;
        QTimer::singleShot(0, &window, [&withdrawDialogHandled] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "withdrawAmountEdit")
                ->setText(QStringLiteral("400.00"));
            auto *confirm = requiredChild<QPushButton>(dialog, "confirmWithdrawButton");
            if (confirm->isEnabled()) {
                confirm->click();
                withdrawDialogHandled = true;
            }
        });
        requiredChild<QPushButton>(&window, "withdrawSelectedButton")->click();
        QVERIFY(withdrawDialogHandled);
        QCOMPARE(deposits->model()->index(0, 2).data().toString(),
                 QStringLiteral("¥600.00"));
        QCOMPARE(transactions->model()->rowCount(), 3);

        bool profileDialogHandled = false;
        QTimer::singleShot(0, &window, [&profileDialogHandled] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "profileNameEdit")
                ->setText(QStringLiteral("张三丰"));
            requiredChild<QLineEdit>(dialog, "profileAddressEdit")
                ->setText(QStringLiteral("上海市浦东新区"));
            requiredChild<QPushButton>(dialog, "confirmProfileButton")->click();
            profileDialogHandled = true;
        });
        requiredChild<QPushButton>(&window, "editProfileButton")->click();
        QVERIFY(profileDialogHandled);
        QVERIFY(requiredChild<QLabel>(&window, "depositorNameLabel")
                    ->text()
                    .contains(QStringLiteral("张三丰")));

        bool passwordDialogHandled = false;
        QTimer::singleShot(0, &window, [&] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "currentPasswordEdit")->setText(oldPassword);
            requiredChild<QLineEdit>(dialog, "newPasswordEdit")->setText(newPassword);
            requiredChild<QLineEdit>(dialog, "confirmNewPasswordEdit")->setText(newPassword);
            requiredChild<QPushButton>(dialog, "confirmPasswordButton")->click();
            passwordDialogHandled = true;
        });
        requiredChild<QPushButton>(&window, "changePasswordButton")->click();
        QVERIFY(passwordDialogHandled);
        QVERIFY(requiredChild<QLabel>(&window, "accountMessageLabel")
                    ->text()
                    .contains(QStringLiteral("密码修改成功")));

        bool lossConfirmed = false;
        QTimer::singleShot(0, &window, [&lossConfirmed] {
            auto *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!messageBox) {
                return;
            }
            QAbstractButton *yesButton = messageBox->button(QMessageBox::Yes);
            if (yesButton) {
                yesButton->click();
                lossConfirmed = true;
            }
        });
        requiredChild<QPushButton>(&window, "reportLossButton")->click();
        QVERIFY(lossConfirmed);
        QVERIFY(requiredChild<QLabel>(&window, "accountStatusLabel")
                    ->text()
                    .contains(QStringLiteral("已挂失")));
        QVERIFY(!requiredChild<QPushButton>(&window, "newDepositButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "editProfileButton")->isEnabled());
        QVERIFY(requiredChild<QPushButton>(&window, "unfreezeAccountButton")->isEnabled());

        bool unfreezeDialogHandled = false;
        QTimer::singleShot(0, &window, [&] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "unfreezePasswordEdit")->setText(newPassword);
            requiredChild<QPushButton>(dialog, "confirmUnfreezeButton")->click();
            unfreezeDialogHandled = true;
        });
        requiredChild<QPushButton>(&window, "unfreezeAccountButton")->click();
        QVERIFY(unfreezeDialogHandled);
        QVERIFY(requiredChild<QLabel>(&window, "accountStatusLabel")
                    ->text()
                    .contains(QStringLiteral("正常")));

        requiredChild<QPushButton>(&window, "logoutDepositorButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("workspacePage"));
        QCOMPARE(deposits->model()->rowCount(), 0);
        QCOMPARE(transactions->model()->rowCount(), 0);

        requiredChild<QPushButton>(&window, "allDepositorsButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("allDepositorsPage"));
        auto *depositors = requiredChild<QTableView>(&window, "allDepositorsTableView");
        QCOMPARE(depositors->model()->rowCount(), 1);
        QCOMPARE(depositors->model()->columnCount(), 8);
        QCOMPARE(depositors->model()->index(0, 1).data().toString(),
                 QStringLiteral("张三丰"));
        for (int column = 0; column < depositors->model()->columnCount(); ++column) {
            QVERIFY(!depositors->model()
                         ->headerData(column, Qt::Horizontal)
                         .toString()
                         .contains(QStringLiteral("密码")));
        }
        requiredChild<QLineEdit>(&window, "depositorSearchEdit")
            ->setText(QStringLiteral("不存在"));
        requiredChild<QPushButton>(&window, "searchDepositorsButton")->click();
        QCOMPARE(depositors->model()->rowCount(), 0);
        requiredChild<QLineEdit>(&window, "depositorSearchEdit")->clear();
        requiredChild<QComboBox>(&window, "depositorStatusComboBox")->setCurrentIndex(2);
        requiredChild<QPushButton>(&window, "searchDepositorsButton")->click();
        QCOMPARE(depositors->model()->rowCount(), 0);
        requiredChild<QPushButton>(&window, "resetDepositorFilterButton")->click();
        QCOMPARE(depositors->model()->rowCount(), 1);

        requiredChild<QPushButton>(&window, "allDepositorsBackButton")->click();
        requiredChild<QPushButton>(&window, "reserveForecastButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("reserveForecastPage"));
        auto *forecastTable = requiredChild<QTableView>(&window, "reserveForecastTableView");
        QCOMPARE(forecastTable->model()->rowCount(), 3);
        requiredChild<QDateEdit>(&window, "forecastBaseDateEdit")
            ->setDate(QDate(2026, 12, 31));
        requiredChild<QPushButton>(&window, "refreshForecastButton")->click();
        QCOMPARE(forecastTable->model()->index(0, 2).data().toString(),
                 QStringLiteral("¥600.00"));
        requiredChild<QAction>(&window, "actionReturnWorkspace")->trigger();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("workspacePage"));

        requiredChild<QPushButton>(&window, "auditLogButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("auditLogPage"));
        auto *auditTable = requiredChild<QTableView>(&window, "auditLogTableView");
        QVERIFY(auditTable->model()->rowCount() >= 9);
        auto *auditFilter = requiredChild<QComboBox>(&window, "auditActionComboBox");
        const int depositAction = auditFilter->findData(QStringLiteral("DEPOSIT"));
        QVERIFY(depositAction >= 0);
        auditFilter->setCurrentIndex(depositAction);
        requiredChild<QPushButton>(&window, "refreshAuditButton")->click();
        QCOMPARE(auditTable->model()->rowCount(), 2);
        QCOMPARE(auditTable->model()->index(0, 3).data().toString(),
                 QStringLiteral("DEPOSIT"));

        requiredChild<QPushButton>(&window, "auditLogBackButton")->click();
        requiredChild<QPushButton>(&window, "switchEmployeeButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("employeeLoginPage"));
        QVERIFY(!requiredChild<QAction>(&window, "actionSwitchEmployee")->isEnabled());
    }

    // 重启窗口后再次通过密码登录，验证 UI 操作已由服务层即时保存。
    MainWindow restarted(makeService(temporaryDirectory.path(), &now));
    showAndProcess(&restarted);
    auto *stack = requiredChild<QStackedWidget>(&restarted, "mainStackedWidget");
    requiredChild<QLineEdit>(&restarted, "employeeIdEdit")->setText(QStringLiteral("E04"));
    requiredChild<QPushButton>(&restarted, "employeeEnterButton")->click();
    requiredChild<QPushButton>(&restarted, "depositorLoginButton")->click();
    requiredChild<QLineEdit>(&restarted, "accountNumberEdit")->setText(QStringLiteral("100001"));
    requiredChild<QLineEdit>(&restarted, "accountPasswordEdit")->setText(newPassword);
    requiredChild<QPushButton>(&restarted, "accountLoginButton")->click();
    QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("accountCenterPage"));
    QVERIFY(requiredChild<QLabel>(&restarted, "depositorNameLabel")
                ->text()
                .contains(QStringLiteral("张三丰")));
    QCOMPARE(requiredChild<QTableView>(&restarted, "depositsTableView")
                 ->model()
                 ->rowCount(),
             2);
    QCOMPARE(requiredChild<QTableView>(&restarted, "transactionsTableView")
                 ->model()
                 ->rowCount(),
             3);
}

void MainWindowTest::filtersClosedDepositsAndDisablesWithdrawal()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDateTime now(QDate(2026, 1, 1), QTime(9, 0));
    const QString password = QStringLiteral("SafePass123");

    const auto fileManager = std::make_shared<FileManager>(temporaryDirectory.path());
    BankService setupService(fileManager, [&now] { return now; });
    QVERIFY(setupService.initialize().success);
    QVERIFY(setupService.enterEmployeeSession(QStringLiteral("E03")).success);
    const OpenAccountResult opened = setupService.openAccount(
        QStringLiteral("李四"), QStringLiteral("北京市"), password, password);
    QVERIFY(opened.status.success);
    QVERIFY(setupService.loginDepositor(opened.accountNumber, password).success);
    const DepositResult deposited = setupService.addFixedDeposit(50000,
                                                                 DepositTerm::OneYear);
    QVERIFY(deposited.status.success);
    QVERIFY(setupService.withdraw(deposited.depositId, 50000).status.success);

    MainWindow window(makeService(temporaryDirectory.path(), &now));
    showAndProcess(&window);
    requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E04"));
    requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
    requiredChild<QPushButton>(&window, "depositorLoginButton")->click();
    requiredChild<QLineEdit>(&window, "accountNumberEdit")->setText(opened.accountNumber);
    requiredChild<QLineEdit>(&window, "accountPasswordEdit")->setText(password);
    requiredChild<QPushButton>(&window, "accountLoginButton")->click();

    auto *deposits = requiredChild<QTableView>(&window, "depositsTableView");
    auto *showClosed = requiredChild<QCheckBox>(&window, "showClosedDepositsCheckBox");
    QCOMPARE(deposits->model()->rowCount(), 0);
    QVERIFY(!showClosed->isChecked());
    showClosed->setChecked(true);
    QCOMPARE(deposits->model()->rowCount(), 1);
    QCOMPARE(deposits->model()->index(0, 2).data().toString(), QStringLiteral("¥0.00"));
    deposits->setCurrentIndex(deposits->model()->index(0, 0));
    QApplication::processEvents();
    QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
}

QTEST_MAIN(MainWindowTest)

#include "tst_ui.moc"

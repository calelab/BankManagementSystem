// 界面测试：通过真实控件和对话框驱动主要流程，验证页面、按钮及表格刷新。
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
#include <QDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <array>
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

bool hasReadableDepositColumns(QTableView *tableView)
{
    constexpr std::array<int, 8> minimumWidths{120, 115, 115, 100, 90, 115, 115, 85};
    if (!tableView
        || tableView->model()->columnCount() != static_cast<int>(minimumWidths.size())) {
        return false;
    }
    for (int column = 0; column < static_cast<int>(minimumWidths.size()); ++column) {
        if (tableView->columnWidth(column) < minimumWidths.at(column)) {
            return false;
        }
    }
    return true;
}

} // namespace

// UI 测试在临时数据目录中通过真实按钮和对话框执行业务，验证页面状态而不绕过 BankService。
class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void definesRequiredDesignerControlsAndDefaults();
    void validatesDialogInputsBeforeAccepting();
    void completesMainWorkflowThroughUiConnections();
    void filtersClosedDepositsAndDisablesWithdrawal();
};

void MainWindowTest::definesRequiredDesignerControlsAndDefaults()
{
    // 验证 Designer 控件、密码遮蔽、初始按钮状态及各表格的可读布局策略。
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

    auto *depositsTable = requiredChild<QTableView>(&window, "depositsTableView");
    auto *depositsHeader = depositsTable->horizontalHeader();
    QVERIFY(!depositsHeader->stretchLastSection());
    QCOMPARE(depositsHeader->sectionResizeMode(0), QHeaderView::Interactive);
    QCOMPARE(depositsHeader->sectionResizeMode(7), QHeaderView::Interactive);
    QCOMPARE(depositsTable->horizontalScrollMode(), QAbstractItemView::ScrollPerPixel);
    QVERIFY(!depositsTable->wordWrap());
    QVERIFY(hasReadableDepositColumns(depositsTable));

    auto *transactionsTable = requiredChild<QTableView>(&window, "transactionsTableView");
    QCOMPARE(transactionsTable->model()->headerData(3, Qt::Horizontal).toString(),
             QStringLiteral("业务类型"));
    QVERIFY(transactionsTable->columnWidth(0) >= 150);
    QVERIFY(transactionsTable->columnWidth(1) >= 180);
    QVERIFY(transactionsTable->columnWidth(2) >= 150);
    QVERIFY(transactionsTable->columnWidth(3) >= 110);
    QVERIFY(transactionsTable->columnWidth(4) >= 115);
    QVERIFY(transactionsTable->columnWidth(5) >= 115);
    QVERIFY(transactionsTable->columnWidth(6) >= 115);
    QCOMPARE(transactionsTable->horizontalScrollMode(),
             QAbstractItemView::ScrollPerPixel);
    QVERIFY(!transactionsTable->wordWrap());

    auto *depositorsTable = requiredChild<QTableView>(&window, "allDepositorsTableView");
    auto *depositorsHeader = depositorsTable->horizontalHeader();
    QVERIFY(!depositorsHeader->stretchLastSection());
    QCOMPARE(depositorsHeader->sectionResizeMode(0), QHeaderView::Interactive);
    QCOMPARE(depositorsHeader->sectionResizeMode(2), QHeaderView::Stretch);
    QCOMPARE(depositorsHeader->sectionResizeMode(7), QHeaderView::Interactive);
    QVERIFY(depositorsTable->columnWidth(0) >= 115);
    QVERIFY(depositorsTable->columnWidth(1) >= 120);
    QVERIFY(depositorsTable->columnWidth(3) >= 85);
    QVERIFY(depositorsTable->columnWidth(4) >= 120);
    QVERIFY(depositorsTable->columnWidth(5) >= 105);
    QVERIFY(depositorsTable->columnWidth(6) >= 100);
    QVERIFY(depositorsTable->columnWidth(7) >= 135);

    auto *auditTable = requiredChild<QTableView>(&window, "auditLogTableView");
    QCOMPARE(auditTable->model()->headerData(3, Qt::Horizontal).toString(),
             QStringLiteral("操作类型"));
    QCOMPARE(auditTable->model()->headerData(7, Qt::Horizontal).toString(),
             QStringLiteral("失败原因"));
    QCOMPARE(auditTable->horizontalScrollMode(), QAbstractItemView::ScrollPerPixel);
    QVERIFY(!auditTable->wordWrap());
    QVERIFY(auditTable->columnWidth(0) >= 170);
    QVERIFY(auditTable->columnWidth(7) >= 300);
    QVERIFY(auditTable->columnWidth(7) <= 320);
    QCOMPARE(requiredChild<QLabel>(&window, "auditActionLabel")->text(),
             QStringLiteral("操作类型"));
    auto *auditFilter = requiredChild<QComboBox>(&window, "auditActionComboBox");
    QCOMPARE(auditFilter->itemText(0), QStringLiteral("全部操作"));
    const int depositFilterIndex = auditFilter->findData(QStringLiteral("DEPOSIT"));
    QVERIFY(depositFilterIndex >= 0);
    QCOMPARE(auditFilter->itemText(depositFilterIndex), QStringLiteral("新增存款"));

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

void MainWindowTest::validatesDialogInputsBeforeAccepting()
{
    // 对话框只接受完整、可解析输入，取消或校验失败不会触发服务层操作。
    DepositDialog depositDialog(QDate(2026, 1, 1));
    QSignalSpy depositAccepted(&depositDialog, &QDialog::accepted);
    requiredChild<QLineEdit>(&depositDialog, "depositAmountEdit")->setText(QStringLiteral("0"));
    requiredChild<QPushButton>(&depositDialog, "confirmDepositButton")->click();
    QCOMPARE(depositAccepted.count(), 0);
    QVERIFY(!requiredChild<QLabel>(&depositDialog, "depositMessageLabel")->text().isEmpty());
    requiredChild<QLineEdit>(&depositDialog, "depositAmountEdit")
        ->setText(QStringLiteral("100.00"));
    requiredChild<QPushButton>(&depositDialog, "confirmDepositButton")->click();
    QCOMPARE(depositAccepted.count(), 1);

    ProfileDialog profileDialog{QString(), QString()};
    QSignalSpy profileAccepted(&profileDialog, &QDialog::accepted);
    requiredChild<QPushButton>(&profileDialog, "confirmProfileButton")->click();
    QCOMPARE(profileAccepted.count(), 0);
    QVERIFY(requiredChild<QLabel>(&profileDialog, "profileMessageLabel")
                ->text()
                .contains(QStringLiteral("不能为空")));
    requiredChild<QLineEdit>(&profileDialog, "profileNameEdit")->setText(QStringLiteral("张三"));
    requiredChild<QLineEdit>(&profileDialog, "profileAddressEdit")
        ->setText(QStringLiteral("上海市"));
    requiredChild<QPushButton>(&profileDialog, "confirmProfileButton")->click();
    QCOMPARE(profileAccepted.count(), 1);

    PasswordDialog passwordDialog;
    QSignalSpy passwordAccepted(&passwordDialog, &QDialog::accepted);
    requiredChild<QLineEdit>(&passwordDialog, "currentPasswordEdit")
        ->setText(QStringLiteral("OldPassword123"));
    requiredChild<QLineEdit>(&passwordDialog, "newPasswordEdit")
        ->setText(QStringLiteral("NewPassword123"));
    requiredChild<QLineEdit>(&passwordDialog, "confirmNewPasswordEdit")
        ->setText(QStringLiteral("DifferentPassword123"));
    requiredChild<QPushButton>(&passwordDialog, "confirmPasswordButton")->click();
    QCOMPARE(passwordAccepted.count(), 0);
    QVERIFY(requiredChild<QLabel>(&passwordDialog, "passwordMessageLabel")
                ->text()
                .contains(QStringLiteral("不一致")));
    requiredChild<QLineEdit>(&passwordDialog, "confirmNewPasswordEdit")
        ->setText(QStringLiteral("NewPassword123"));
    requiredChild<QPushButton>(&passwordDialog, "confirmPasswordButton")->click();
    QCOMPARE(passwordAccepted.count(), 1);

    UnfreezeDialog unfreezeDialog;
    QSignalSpy unfreezeAccepted(&unfreezeDialog, &QDialog::accepted);
    requiredChild<QPushButton>(&unfreezeDialog, "confirmUnfreezeButton")->click();
    QCOMPARE(unfreezeAccepted.count(), 0);
    QVERIFY(requiredChild<QLabel>(&unfreezeDialog, "unfreezeMessageLabel")
                ->text()
                .contains(QStringLiteral("请输入")));
    requiredChild<QLineEdit>(&unfreezeDialog, "unfreezePasswordEdit")
        ->setText(QStringLiteral("Password123"));
    requiredChild<QPushButton>(&unfreezeDialog, "confirmUnfreezeButton")->click();
    QCOMPARE(unfreezeAccepted.count(), 1);

    WithdrawDialog withdrawDialog(nullptr, QStringLiteral("FD000001"));
    requiredChild<QLineEdit>(&withdrawDialog, "withdrawAmountEdit")
        ->setText(QStringLiteral("100.00"));
    QVERIFY(!requiredChild<QPushButton>(&withdrawDialog, "confirmWithdrawButton")->isEnabled());
    QCOMPARE(requiredChild<QLabel>(&withdrawDialog, "withdrawMessageLabel")->text(),
             QStringLiteral("业务服务不可用"));
}

void MainWindowTest::completesMainWorkflowThroughUiConnections()
{
    // 使用真实点击贯通开户、登录、两笔存款、支取、资料、挂失、查询和重启恢复。
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDateTime now(QDate(2026, 1, 1), QTime(9, 0));
    const QString oldPassword = QStringLiteral("SafePass123");
    const QString newPassword = QStringLiteral("NewSafePass456");

    {
        MainWindow window(makeService(temporaryDirectory.path(), &now));
        showAndProcess(&window);
        auto *stack = requiredChild<QStackedWidget>(&window, "mainStackedWidget");

        requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E99"));
        requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("employeeLoginPage"));
        QVERIFY(!requiredChild<QLabel>(&window, "employeeLoginMessageLabel")->text().isEmpty());

        requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E03"));
        requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("workspacePage"));
        QVERIFY(requiredChild<QLabel>(&window, "currentEmployeeLabel")
                    ->text()
                    .contains(QStringLiteral("E03")));
        QVERIFY(!requiredChild<QPushButton>(&window, "newDepositButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "editProfileButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "reportLossButton")->isEnabled());

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
        requiredChild<QLineEdit>(&window, "accountPasswordEdit")
            ->setText(QStringLiteral("WrongPassword123"));
        requiredChild<QPushButton>(&window, "accountLoginButton")->click();
        QVERIFY(requiredChild<QLabel>(&window, "accountLoginMessageLabel")
                    ->text()
                    .contains(QStringLiteral("账号或密码错误")));
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

        auto *deposits = requiredChild<QTableView>(&window, "depositsTableView");
        QCOMPARE(deposits->model()->rowCount(), 1);
        QVERIFY(hasReadableDepositColumns(deposits));
        QVERIFY(!deposits->horizontalHeader()->stretchLastSection());

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

        auto *transactions = requiredChild<QTableView>(&window, "transactionsTableView");
        QCOMPARE(deposits->model()->rowCount(), 2);
        QVERIFY(hasReadableDepositColumns(deposits));
        QCOMPARE(transactions->model()->rowCount(), 2);
        QCOMPARE(deposits->model()->index(0, 2).data().toString(),
                 QStringLiteral("¥1,000.00"));
        QCOMPARE(deposits->model()->index(1, 3).data().toString(),
                 QStringLiteral("三年期定期"));
        QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
        deposits->setCurrentIndex(deposits->model()->index(0, 0));
        QApplication::processEvents();
        QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());

        bool cancelledWithdrawal = false;
        QTimer::singleShot(0, &window, [&cancelledWithdrawal] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "withdrawAmountEdit")
                ->setText(QStringLiteral("400.00"));
            requiredChild<QPushButton>(dialog, "cancelWithdrawButton")->click();
            cancelledWithdrawal = true;
        });
        requiredChild<QPushButton>(&window, "withdrawSelectedButton")->click();
        QVERIFY(cancelledWithdrawal);
        QCOMPARE(deposits->model()->index(0, 2).data().toString(),
                 QStringLiteral("¥1,000.00"));
        QCOMPARE(transactions->model()->rowCount(), 2);

        now = QDateTime(QDate(2026, 7, 2), QTime(10, 30));

        bool withdrawDialogHandled = false;
        bool withdrawPreviewVerified = false;
        QTimer::singleShot(0, &window, [&withdrawDialogHandled, &withdrawPreviewVerified] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "withdrawAmountEdit")
                ->setText(QStringLiteral("400.00"));
            auto *confirm = requiredChild<QPushButton>(dialog, "confirmWithdrawButton");
            if (confirm->isEnabled()) {
                withdrawPreviewVerified =
                    requiredChild<QLabel>(dialog, "withdrawKindLabel")
                        ->text()
                        .contains(QStringLiteral("提前支取"))
                    && requiredChild<QLabel>(dialog, "withdrawInterestLabel")->text()
                           != QStringLiteral("¥0.00")
                    && requiredChild<QLabel>(dialog, "withdrawPayoutLabel")->text()
                           != QStringLiteral("¥400.00");
                confirm->click();
                withdrawDialogHandled = true;
            }
        });
        requiredChild<QPushButton>(&window, "withdrawSelectedButton")->click();
        QVERIFY(withdrawDialogHandled);
        QVERIFY(withdrawPreviewVerified);
        QCOMPARE(deposits->model()->index(0, 2).data().toString(),
                 QStringLiteral("¥600.00"));
        QCOMPARE(transactions->model()->rowCount(), 3);
        QVERIFY(!deposits->currentIndex().isValid());
        QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());

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
        bool lossButtonLabelsVerified = false;
        QTimer::singleShot(0, &window, [&lossConfirmed, &lossButtonLabelsVerified] {
            auto *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!messageBox) {
                return;
            }
            QAbstractButton *yesButton = messageBox->button(QMessageBox::Yes);
            QAbstractButton *noButton = messageBox->button(QMessageBox::No);
            if (yesButton && noButton) {
                lossButtonLabelsVerified = yesButton->text() == QStringLiteral("确认")
                                           && noButton->text() == QStringLiteral("取消");
                yesButton->click();
                lossConfirmed = true;
            }
        });
        requiredChild<QPushButton>(&window, "reportLossButton")->click();
        QVERIFY(lossConfirmed);
        QVERIFY(lossButtonLabelsVerified);
        QVERIFY(requiredChild<QLabel>(&window, "accountStatusLabel")
                    ->text()
                    .contains(QStringLiteral("已挂失")));
        QVERIFY(!requiredChild<QPushButton>(&window, "newDepositButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "editProfileButton")->isEnabled());
        QVERIFY(requiredChild<QPushButton>(&window, "unfreezeAccountButton")->isEnabled());

        bool failedUnfreezeDialogHandled = false;
        QTimer::singleShot(0, &window, [&failedUnfreezeDialogHandled] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) {
                return;
            }
            requiredChild<QLineEdit>(dialog, "unfreezePasswordEdit")
                ->setText(QStringLiteral("WrongPassword123"));
            requiredChild<QPushButton>(dialog, "confirmUnfreezeButton")->click();
            failedUnfreezeDialogHandled = true;
        });
        requiredChild<QPushButton>(&window, "unfreezeAccountButton")->click();
        QVERIFY(failedUnfreezeDialogHandled);
        QVERIFY(requiredChild<QLabel>(&window, "accountStatusLabel")
                    ->text()
                    .contains(QStringLiteral("已挂失")));
        QVERIFY(requiredChild<QLabel>(&window, "accountMessageLabel")
                    ->text()
                    .contains(QStringLiteral("密码错误")));
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
        QVERIFY(!requiredChild<QPushButton>(&window, "newDepositButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "editProfileButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "changePasswordButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "reportLossButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "unfreezeAccountButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "logoutDepositorButton")->isEnabled());

        requiredChild<QPushButton>(&window, "allDepositorsButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("allDepositorsPage"));
        auto *depositors = requiredChild<QTableView>(&window, "allDepositorsTableView");
        QCOMPARE(depositors->model()->rowCount(), 1);
        QCOMPARE(depositors->model()->columnCount(), 8);
        QCOMPARE(depositors->model()->index(0, 1).data().toString(),
                 QStringLiteral("张三丰"));
        QCOMPARE(depositors->model()->index(0, 2).data().toString(),
                 QStringLiteral("上海市浦东新区"));
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
        QVERIFY(auditTable->model()->rowCount() >= 11);
        QVERIFY(auditTable->columnWidth(0) >= 170);
        QVERIFY(auditTable->columnWidth(1) >= 70);
        QVERIFY(auditTable->columnWidth(2) >= 100);
        QVERIFY(auditTable->columnWidth(3) >= 110);
        QVERIFY(auditTable->columnWidth(4) >= 95);
        QVERIFY(auditTable->columnWidth(5) >= 95);
        QVERIFY(auditTable->columnWidth(6) >= 65);
        QVERIFY(auditTable->columnWidth(7) >= 300);
        QVERIFY(auditTable->columnWidth(7) <= 320);
        auto *auditFilter = requiredChild<QComboBox>(&window, "auditActionComboBox");
        const int depositAction = auditFilter->findData(QStringLiteral("DEPOSIT"));
        QVERIFY(depositAction >= 0);
        QCOMPARE(auditFilter->itemText(depositAction), QStringLiteral("新增存款"));
        auditFilter->setCurrentIndex(depositAction);
        requiredChild<QPushButton>(&window, "refreshAuditButton")->click();
        QCOMPARE(auditTable->model()->rowCount(), 2);
        QCOMPARE(auditTable->model()->index(0, 3).data().toString(),
                 QStringLiteral("新增存款"));
        QCOMPARE(auditTable->model()->index(0, 7).data().toString(),
                 QStringLiteral("—"));

        const int loginAction = auditFilter->findData(QStringLiteral("LOGIN"));
        QVERIFY(loginAction >= 0);
        QCOMPARE(auditFilter->itemText(loginAction), QStringLiteral("储户登录"));
        auditFilter->setCurrentIndex(loginAction);
        requiredChild<QPushButton>(&window, "refreshAuditButton")->click();
        QCOMPARE(auditTable->model()->rowCount(), 2);
        bool sawSuccessfulLogin = false;
        bool sawFailedLogin = false;
        for (int row = 0; row < auditTable->model()->rowCount(); ++row) {
            QCOMPARE(auditTable->model()->index(row, 3).data().toString(),
                     QStringLiteral("储户登录"));
            const QString auditResult = auditTable->model()->index(row, 6).data().toString();
            const QString reason = auditTable->model()->index(row, 7).data().toString();
            sawSuccessfulLogin = sawSuccessfulLogin
                                 || (auditResult == QStringLiteral("成功")
                                     && reason == QStringLiteral("—"));
            sawFailedLogin = sawFailedLogin
                             || (auditResult == QStringLiteral("失败")
                                 && reason == QStringLiteral("密码错误"));
        }
        QVERIFY(sawSuccessfulLogin);
        QVERIFY(sawFailedLogin);

        requiredChild<QPushButton>(&window, "auditLogBackButton")->click();
        requiredChild<QPushButton>(&window, "switchEmployeeButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("employeeLoginPage"));
        QVERIFY(!requiredChild<QAction>(&window, "actionSwitchEmployee")->isEnabled());
        QCOMPARE(auditTable->model()->rowCount(), 0);
        QCOMPARE(depositors->model()->rowCount(), 0);
        QCOMPARE(forecastTable->model()->rowCount(), 0);
        QCOMPARE(requiredChild<QLabel>(&window, "accountNumberLabel")->text(),
                 QStringLiteral("账号：—"));
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
    // 结清存款默认隐藏；用户选择显示后仍不能再次点击支取。
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

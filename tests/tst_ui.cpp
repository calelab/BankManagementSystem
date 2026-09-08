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
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QHash>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <array>
#include <cstdlib>
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

bool hasSelectedDeposit(QTableView *table, const QString &depositId)
{
    const QModelIndex current = table->currentIndex();
    return current.isValid()
           && table->model()->index(current.row(), 0).data(Qt::UserRole).toString() == depositId
           && table->selectionModel()->selectedRows().size() == 1
           && table->selectionModel()->isRowSelected(current.row(), QModelIndex());
}

// 只使核心数据保存失败，审计仍正常，验证失败刷新不会丢掉操作对象。
class FailingCoreCodec final : public DataCodec
{
public:
    bool rejectCoreSave = false;

    QString fileName() const override { return codec_.fileName(); }
    QString auditFileSuffix() const override { return codec_.auditFileSuffix(); }

    bool encode(const QByteArray &plainJson, QByteArray *encodedData,
                QString *errorMessage) const override
    {
        if (rejectCoreSave
            && QJsonDocument::fromJson(plainJson).object().contains(QStringLiteral("depositors"))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("测试核心数据保存失败");
            }
            return false;
        }
        return codec_.encode(plainJson, encodedData, errorMessage);
    }

    bool decode(const QByteArray &encodedData, QByteArray *plainJson,
                QString *errorMessage) const override
    {
        return codec_.decode(encodedData, plainJson, errorMessage);
    }

private:
    PlainJsonCodec codec_;
};

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

bool hasReadableReserveColumns(QTableView *tableView)
{
    constexpr std::array<int, 5> minimumWidths{115, 90, 120, 120, 120};
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

int totalColumnWidth(QTableView *tableView)
{
    int width = 0;
    for (int column = 0; column < tableView->model()->columnCount(); ++column) {
        width += tableView->columnWidth(column);
    }
    return width;
}

bool reserveColumnsFillViewport(QTableView *tableView)
{
    return tableView
           && std::abs(totalColumnWidth(tableView) - tableView->viewport()->width()) <= 1;
}

bool hasBalancedReserveColumns(QTableView *tableView)
{
    if (!tableView || tableView->model()->columnCount() != 5) {
        return false;
    }
    const int totalWidth = totalColumnWidth(tableView);
    return tableView->columnWidth(1) < tableView->columnWidth(0)
           && tableView->columnWidth(1) < tableView->columnWidth(2)
           && tableView->columnWidth(4) * 3 < totalWidth;
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
    void preservesSelectionForPartialWithdrawal_data();
    void preservesSelectionForPartialWithdrawal();
    void preservesSelectionAfterServiceFailure_data();
    void preservesSelectionAfterServiceFailure();
    void clearsSelectionAcrossSessions();
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

    auto *reserveTable = requiredChild<QTableView>(&window, "reserveForecastTableView");
    auto *reserveHeader = reserveTable->horizontalHeader();
    QVERIFY(!reserveHeader->stretchLastSection());
    QCOMPARE(reserveHeader->sectionResizeMode(0), QHeaderView::Interactive);
    QCOMPARE(reserveHeader->sectionResizeMode(4), QHeaderView::Interactive);
    QCOMPARE(reserveTable->horizontalScrollMode(), QAbstractItemView::ScrollPerPixel);
    QVERIFY(!reserveTable->wordWrap());
    QVERIFY(hasReadableReserveColumns(reserveTable));

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
    const QStringList expectedActions{
        QString(), QStringLiteral("LOGIN"), QStringLiteral("OPEN_ACCOUNT"),
        QStringLiteral("DEPOSIT"), QStringLiteral("EARLY_WITHDRAW"),
        QStringLiteral("MATURED_WITHDRAW"), QStringLiteral("UPDATE_PROFILE"),
        QStringLiteral("CHANGE_PASSWORD"), QStringLiteral("REPORT_LOSS"),
        QStringLiteral("UNFREEZE_ACCOUNT"), QStringLiteral("CORE_DATA_SAVE")};
    const QStringList expectedTexts{
        QStringLiteral("全部操作"), QStringLiteral("储户登录"), QStringLiteral("开户"),
        QStringLiteral("新增存款"), QStringLiteral("提前支取"), QStringLiteral("到期支取"),
        QStringLiteral("修改资料"), QStringLiteral("修改密码"), QStringLiteral("账户挂失"),
        QStringLiteral("解除挂失"), QStringLiteral("核心数据保存")};
    QCOMPARE(auditFilter->count(), expectedActions.size());
    QCOMPARE(auditFilter->currentIndex(), 0);
    for (int index = 0; index < auditFilter->count(); ++index) {
        QCOMPARE(auditFilter->itemText(index), expectedTexts.at(index));
        QCOMPARE(auditFilter->itemData(index).toString(), expectedActions.at(index));
    }
    for (QTableView *table : {depositsTable, transactionsTable, depositorsTable,
                             reserveTable, auditTable}) {
        QVERIFY(table->verticalHeader()->isHidden());
        QVERIFY(!table->horizontalHeader()->stretchLastSection());
    }

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
    QCOMPARE(requiredChild<QLabel>(&passwordDialog, "passwordMessageLabel")->text(),
             QStringLiteral("修改成功后，新密码将安全保存。"));
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
        deposits->setCurrentIndex(deposits->model()->index(0, 0));

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
        QVERIFY(hasSelectedDeposit(deposits, QStringLiteral("FD000001")));
        QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());

        // 后续账户级操作应保留第二笔，不能以默认选首行代替按编号恢复。
        deposits->setCurrentIndex(deposits->model()->index(1, 0));

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
        QVERIFY(hasSelectedDeposit(deposits, QStringLiteral("FD000002")));
        QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
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
        QVERIFY(hasSelectedDeposit(deposits, QStringLiteral("FD000002")));
        QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
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
        QVERIFY(!deposits->currentIndex().isValid());
        QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
        QVERIFY(requiredChild<QLabel>(&window, "accountStatusLabel")
                    ->text()
                    .contains(QStringLiteral("已挂失")));
        QVERIFY(!requiredChild<QPushButton>(&window, "newDepositButton")->isEnabled());
        QVERIFY(!requiredChild<QPushButton>(&window, "editProfileButton")->isEnabled());
        QVERIFY(requiredChild<QPushButton>(&window, "unfreezeAccountButton")->isEnabled());

        deposits->setCurrentIndex(deposits->model()->index(1, 0));
        QVERIFY(hasSelectedDeposit(deposits, QStringLiteral("FD000002")));
        QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
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
        QVERIFY(hasSelectedDeposit(deposits, QStringLiteral("FD000002")));
        QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
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
        QVERIFY(hasSelectedDeposit(deposits, QStringLiteral("FD000002")));
        QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
        QVERIFY(requiredChild<QLabel>(&window, "accountStatusLabel")
                    ->text()
                    .contains(QStringLiteral("正常")));

        requiredChild<QPushButton>(&window, "logoutDepositorButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("workspacePage"));
        QCOMPARE(deposits->model()->rowCount(), 0);
        QVERIFY(!deposits->currentIndex().isValid());
        QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
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
        QApplication::processEvents();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("reserveForecastPage"));
        auto *forecastTable = requiredChild<QTableView>(&window, "reserveForecastTableView");
        QCOMPARE(forecastTable->model()->rowCount(), 3);
        QVERIFY(!forecastTable->currentIndex().isValid());
        QVERIFY(hasReadableReserveColumns(forecastTable));
        QVERIFY(reserveColumnsFillViewport(forecastTable));
        QVERIFY(hasBalancedReserveColumns(forecastTable));
        requiredChild<QDateEdit>(&window, "forecastBaseDateEdit")
            ->setDate(QDate(2026, 12, 31));
        requiredChild<QPushButton>(&window, "refreshForecastButton")->click();
        QCOMPARE(forecastTable->model()->index(0, 2).data().toString(),
                 QStringLiteral("¥600.00"));
        QVERIFY(hasReadableReserveColumns(forecastTable));
        QVERIFY(reserveColumnsFillViewport(forecastTable));
        QVERIFY(hasBalancedReserveColumns(forecastTable));
        window.resize(window.width() + 240, window.height());
        QApplication::processEvents();
        QVERIFY(hasReadableReserveColumns(forecastTable));
        QVERIFY(reserveColumnsFillViewport(forecastTable));
        QVERIFY(hasBalancedReserveColumns(forecastTable));
        window.resize(window.minimumSize());
        QApplication::processEvents();
        QVERIFY(hasReadableReserveColumns(forecastTable));
        QVERIFY(reserveColumnsFillViewport(forecastTable));
        QVERIFY(hasBalancedReserveColumns(forecastTable));

        forecastTable->setFixedWidth(500);
        QApplication::processEvents();
        QVERIFY(hasReadableReserveColumns(forecastTable));
        QVERIFY(totalColumnWidth(forecastTable) > forecastTable->viewport()->width());
        QVERIFY(forecastTable->horizontalScrollBar()->maximum() > 0);
        const int manuallyAdjustedWidth = forecastTable->columnWidth(0) + 20;
        forecastTable->setColumnWidth(0, manuallyAdjustedWidth);
        QApplication::processEvents();
        QCOMPARE(forecastTable->columnWidth(0), manuallyAdjustedWidth);
        requiredChild<QAction>(&window, "actionReturnWorkspace")->trigger();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("workspacePage"));

        requiredChild<QPushButton>(&window, "auditLogButton")->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("auditLogPage"));
        auto *auditTable = requiredChild<QTableView>(&window, "auditLogTableView");
        QVERIFY(auditTable->model()->rowCount() >= 11);
        const int totalAuditRows = auditTable->model()->rowCount();
        QHash<QString, int> auditActionCounts;
        for (int row = 0; row < totalAuditRows; ++row) {
            ++auditActionCounts[auditTable->model()->index(row, 3).data().toString()];
        }
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

        // 逐项检查 Designer 显示文字关联的操作码仍筛出相同业务记录。
        for (int index = 0; index < auditFilter->count(); ++index) {
            auditFilter->setCurrentIndex(index);
            requiredChild<QPushButton>(&window, "refreshAuditButton")->click();
            const QString actionText = auditFilter->itemText(index);
            QCOMPARE(auditTable->model()->rowCount(),
                     index == 0 ? totalAuditRows : auditActionCounts.value(actionText));
            if (index != 0) {
                for (int row = 0; row < auditTable->model()->rowCount(); ++row) {
                    QCOMPARE(auditTable->model()->index(row, 3).data().toString(), actionText);
                }
            }
        }

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

    // 窗口关闭后直接解析正式 JSON 文件，验证密码仅以派生凭据保存。
    QFile bankFile(QDir(temporaryDirectory.path()).filePath(QStringLiteral("bank_data.json")));
    QVERIFY(bankFile.open(QIODevice::ReadOnly));
    const QByteArray bankJson = bankFile.readAll();
    bankFile.close();
    QJsonParseError parseError;
    const QJsonDocument bankDocument = QJsonDocument::fromJson(bankJson, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(bankDocument.isObject());
    const QJsonArray savedDepositors = bankDocument.object()
                                          .value(QStringLiteral("depositors")).toArray();
    QCOMPARE(savedDepositors.size(), 1);
    const QJsonObject savedDepositor = savedDepositors.first().toObject();
    QCOMPARE(savedDepositor.value(QStringLiteral("accountNumber")).toString(),
             QStringLiteral("100001"));
    QVERIFY(!savedDepositor.contains(QStringLiteral("password")));
    const QByteArray savedSalt = QByteArray::fromBase64(
        savedDepositor.value(QStringLiteral("passwordSalt")).toString().toLatin1());
    const QByteArray savedHash = QByteArray::fromBase64(
        savedDepositor.value(QStringLiteral("passwordHash")).toString().toLatin1());
    QCOMPARE(savedSalt.size(), 16);
    QCOMPARE(savedHash.size(), 32);
    QCOMPARE(savedDepositor.value(QStringLiteral("passwordKdfIterations")).toInt(), 210000);
    QCOMPARE(savedDepositor.value(QStringLiteral("passwordKdfAlgorithm")).toString(),
             Depositor::supportedPasswordKdfAlgorithm());
    QVERIFY(!bankJson.contains(oldPassword.toUtf8()));
    QVERIFY(!bankJson.contains(newPassword.toUtf8()));

    QFile auditFile(QDir(temporaryDirectory.path()).filePath(QStringLiteral("audit/E03.audit.json")));
    QVERIFY(auditFile.open(QIODevice::ReadOnly));
    const QByteArray auditJson = auditFile.readAll();
    auditFile.close();
    const QJsonDocument auditDocument = QJsonDocument::fromJson(auditJson, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(auditDocument.isObject());
    const QJsonArray savedRecords = auditDocument.object()
                                       .value(QStringLiteral("records")).toArray();
    QVERIFY(!savedRecords.isEmpty());
    for (const QJsonValue &record : savedRecords) {
        QCOMPARE(record.toObject().value(QStringLiteral("employeeId")).toString(),
                 QStringLiteral("E03"));
    }
    QVERIFY(!auditJson.contains(oldPassword.toUtf8()));
    QVERIFY(!auditJson.contains(newPassword.toUtf8()));
    const QStringList dataEntries = QDir(temporaryDirectory.path())
                                        .entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    QCOMPARE(dataEntries, (QStringList{QStringLiteral("audit"),
                                      QStringLiteral("bank_data.json"),
                                      QStringLiteral("employees.dat")}));

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
    QVERIFY(requiredChild<QTableView>(&restarted, "depositsTableView")
                ->selectionModel()->selectedRows().isEmpty());
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
    const DepositResult activeDeposit = setupService.addFixedDeposit(80000, DepositTerm::ThreeYears);
    QVERIFY(activeDeposit.status.success);

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
    QCOMPARE(deposits->model()->rowCount(), 1);
    QVERIFY(!showClosed->isChecked());
    deposits->selectionModel()->clear();
    QVERIFY(!deposits->currentIndex().isValid());
    showClosed->setChecked(true);
    QCOMPARE(deposits->model()->rowCount(), 2);
    QVERIFY(!deposits->currentIndex().isValid());
    QCOMPARE(deposits->model()->index(0, 2).data().toString(), QStringLiteral("¥0.00"));
    deposits->setCurrentIndex(deposits->model()->index(0, 0));
    QApplication::processEvents();
    QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());

    // 隐藏选中的结清存款时，不得转而选中另一笔仍可见存款。
    showClosed->setChecked(false);
    QCOMPARE(deposits->model()->rowCount(), 1);
    QVERIFY(!deposits->currentIndex().isValid());
    QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());

    // 同一未结清存款的行号随过滤变化，恢复必须以 depositId 为准。
    deposits->setCurrentIndex(deposits->model()->index(0, 0));
    showClosed->setChecked(true);
    QVERIFY(hasSelectedDeposit(deposits, activeDeposit.depositId));
    QCOMPARE(deposits->currentIndex().row(), 1);
    QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
    showClosed->setChecked(false);
    QVERIFY(hasSelectedDeposit(deposits, activeDeposit.depositId));
    QCOMPARE(deposits->currentIndex().row(), 0);
    QVERIFY(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled());
}

void MainWindowTest::preservesSelectionForPartialWithdrawal_data()
{
    QTest::addColumn<bool>("matured");
    QTest::addColumn<bool>("showClosed");
    QTest::newRow("early-hide-closed") << false << false;
    QTest::newRow("early-show-closed") << false << true;
    QTest::newRow("matured-hide-closed") << true << false;
    QTest::newRow("matured-show-closed") << true << true;
}

void MainWindowTest::preservesSelectionForPartialWithdrawal()
{
    QFETCH(bool, matured);
    QFETCH(bool, showClosed);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDateTime now(QDate(2026, 1, 1), QTime(9, 0));
    const QString password = QStringLiteral("SafePass123");
    auto service = makeService(directory.path(), &now);
    QVERIFY(service->initialize().success);
    QVERIFY(service->enterEmployeeSession(QStringLiteral("E03")).success);
    const auto opened = service->openAccount(QStringLiteral("李四"), QStringLiteral("北京"),
                                             password, password);
    QVERIFY(opened.status.success);
    QVERIFY(service->loginDepositor(opened.accountNumber, password).success);
    QVERIFY(service->addFixedDeposit(100000, DepositTerm::OneYear).status.success);
    const auto target = service->addFixedDeposit(100000, DepositTerm::OneYear);
    QVERIFY(target.status.success);
    now = now.addDays(matured ? 365 : 30);

    MainWindow window(std::move(service));
    showAndProcess(&window);
    requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E03"));
    requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
    requiredChild<QPushButton>(&window, "depositorLoginButton")->click();
    requiredChild<QLineEdit>(&window, "accountNumberEdit")->setText(opened.accountNumber);
    requiredChild<QLineEdit>(&window, "accountPasswordEdit")->setText(password);
    requiredChild<QPushButton>(&window, "accountLoginButton")->click();
    auto *deposits = requiredChild<QTableView>(&window, "depositsTableView");
    auto *withdrawButton = requiredChild<QPushButton>(&window, "withdrawSelectedButton");
    QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
    requiredChild<QCheckBox>(&window, "showClosedDepositsCheckBox")->setChecked(showClosed);
    deposits->setCurrentIndex(deposits->model()->index(1, 0));

    const auto withdraw = [&](const QString &amount) {
        bool confirmed = false;
        QTimer::singleShot(0, &window, [&] {
            QWidget *dialog = QApplication::activeModalWidget();
            requiredChild<QLineEdit>(dialog, "withdrawAmountEdit")->setText(amount);
            auto *confirm = requiredChild<QPushButton>(dialog, "confirmWithdrawButton");
            confirmed = confirm->isEnabled();
            confirm->click();
        });
        withdrawButton->click();
        return confirmed;
    };

    QVERIFY(withdraw(QStringLiteral("400.00")));
    QVERIFY(hasSelectedDeposit(deposits, target.depositId));
    QCOMPARE(deposits->currentIndex().data(Qt::UserRole + 1).toLongLong(), 60000);
    QVERIFY(withdrawButton->isEnabled());
    auto *transactions = requiredChild<QTableView>(&window, "transactionsTableView");
    QCOMPARE(transactions->model()->rowCount(), 3);
    QVERIFY(transactions->model()->index(2, 3).data().toString().contains(
        matured ? QStringLiteral("到期") : QStringLiteral("提前")));

    QVERIFY(withdraw(QStringLiteral("600.00")));
    QCOMPARE(transactions->model()->rowCount(), 4);
    QCOMPARE(deposits->model()->rowCount(), showClosed ? 2 : 1);
    QVERIFY(!deposits->currentIndex().isValid());
    QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!withdrawButton->isEnabled());
    QCOMPARE(deposits->model()->index(0, 0).data(Qt::UserRole + 1).toLongLong(), 100000);
    if (showClosed) {
        QCOMPARE(deposits->model()->index(1, 0).data(Qt::UserRole + 1).toLongLong(), 0);
    }
}

void MainWindowTest::preservesSelectionAfterServiceFailure_data()
{
    QTest::addColumn<QString>("operation");
    for (const char *operation : {"deposit", "withdraw", "profile", "password", "loss", "unfreeze"}) {
        QTest::newRow(operation) << QString::fromLatin1(operation);
    }
}

void MainWindowTest::preservesSelectionAfterServiceFailure()
{
    QFETCH(QString, operation);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDateTime now(QDate(2026, 1, 1), QTime(9, 0));
    const QString password = QStringLiteral("SafePass123");
    const auto codec = std::make_shared<FailingCoreCodec>();
    const auto manager = std::make_shared<FileManager>(directory.path(), codec);
    auto service = std::make_unique<BankService>(manager, [&now] { return now; });
    QVERIFY(service->initialize().success);
    QVERIFY(service->enterEmployeeSession(QStringLiteral("E03")).success);
    const auto opened = service->openAccount(QStringLiteral("李四"), QStringLiteral("北京"),
                                             password, password);
    QVERIFY(opened.status.success);
    QVERIFY(service->loginDepositor(opened.accountNumber, password).success);
    QVERIFY(service->addFixedDeposit(100000, DepositTerm::OneYear).status.success);
    const auto target = service->addFixedDeposit(200000, DepositTerm::ThreeYears);
    QVERIFY(target.status.success);
    const bool lost = operation == QStringLiteral("unfreeze");
    if (lost) {
        QVERIFY(service->reportLoss().success);
    }

    MainWindow window(std::move(service));
    showAndProcess(&window);
    requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E03"));
    requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
    requiredChild<QPushButton>(&window, "depositorLoginButton")->click();
    requiredChild<QLineEdit>(&window, "accountNumberEdit")->setText(opened.accountNumber);
    requiredChild<QLineEdit>(&window, "accountPasswordEdit")->setText(password);
    requiredChild<QPushButton>(&window, "accountLoginButton")->click();
    auto *deposits = requiredChild<QTableView>(&window, "depositsTableView");
    deposits->setCurrentIndex(deposits->model()->index(1, 0));
    codec->rejectCoreSave = true;

    const char *buttonName = operation == QStringLiteral("deposit") ? "newDepositButton"
                             : operation == QStringLiteral("withdraw") ? "withdrawSelectedButton"
                             : operation == QStringLiteral("profile") ? "editProfileButton"
                             : operation == QStringLiteral("password") ? "changePasswordButton"
                             : lost ? "unfreezeAccountButton" : "reportLossButton";
    bool confirmed = false;
    QTimer::singleShot(0, &window, [&] {
        QWidget *dialog = QApplication::activeModalWidget();
        if (operation == QStringLiteral("loss")) {
            auto *confirmation = qobject_cast<QMessageBox *>(dialog);
            if (confirmation) {
                confirmation->button(QMessageBox::Yes)->click();
                confirmed = true;
            }
            return;
        }
        const char *confirmName = nullptr;
        if (operation == QStringLiteral("deposit")) {
            requiredChild<QLineEdit>(dialog, "depositAmountEdit")->setText(QStringLiteral("100.00"));
            confirmName = "confirmDepositButton";
        } else if (operation == QStringLiteral("withdraw")) {
            requiredChild<QLineEdit>(dialog, "withdrawAmountEdit")->setText(QStringLiteral("100.00"));
            confirmName = "confirmWithdrawButton";
        } else if (operation == QStringLiteral("profile")) {
            requiredChild<QLineEdit>(dialog, "profileNameEdit")->setText(QStringLiteral("新姓名"));
            confirmName = "confirmProfileButton";
        } else if (operation == QStringLiteral("password")) {
            requiredChild<QLineEdit>(dialog, "currentPasswordEdit")->setText(password);
            requiredChild<QLineEdit>(dialog, "newPasswordEdit")->setText(QStringLiteral("NewPass456"));
            requiredChild<QLineEdit>(dialog, "confirmNewPasswordEdit")->setText(QStringLiteral("NewPass456"));
            confirmName = "confirmPasswordButton";
        } else {
            requiredChild<QLineEdit>(dialog, "unfreezePasswordEdit")->setText(password);
            confirmName = "confirmUnfreezeButton";
        }
        auto *confirm = requiredChild<QPushButton>(dialog, confirmName);
        confirmed = confirm->isEnabled();
        confirm->click();
    });
    requiredChild<QPushButton>(&window, buttonName)->click();
    QVERIFY(confirmed);
    QVERIFY(requiredChild<QLabel>(&window, "accountMessageLabel")->text().contains(QStringLiteral("保存失败")));
    QCOMPARE(deposits->model()->rowCount(), 2);
    QVERIFY(hasSelectedDeposit(deposits, target.depositId));
    QCOMPARE(deposits->currentIndex().data(Qt::UserRole + 1).toLongLong(), 200000);
    QCOMPARE(requiredChild<QPushButton>(&window, "withdrawSelectedButton")->isEnabled(), !lost);
}

void MainWindowTest::clearsSelectionAcrossSessions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDateTime now(QDate(2026, 1, 1), QTime(9, 0));
    const QString password = QStringLiteral("SafePass123");
    auto service = makeService(directory.path(), &now);
    QVERIFY(service->initialize().success);
    QVERIFY(service->enterEmployeeSession(QStringLiteral("E03")).success);
    QStringList accounts;
    for (int index = 0; index < 2; ++index) {
        const auto opened = service->openAccount(QStringLiteral("储户%1").arg(index),
                                                 QStringLiteral("北京"), password, password);
        QVERIFY(opened.status.success);
        accounts.append(opened.accountNumber);
        QVERIFY(service->loginDepositor(opened.accountNumber, password).success);
        QVERIFY(service->addFixedDeposit(100000, DepositTerm::OneYear).status.success);
    }
    MainWindow window(std::move(service));
    showAndProcess(&window);
    requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E03"));
    requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
    auto *deposits = requiredChild<QTableView>(&window, "depositsTableView");
    auto *withdrawButton = requiredChild<QPushButton>(&window, "withdrawSelectedButton");
    const auto login = [&](const QString &account) {
        requiredChild<QPushButton>(&window, "depositorLoginButton")->click();
        requiredChild<QLineEdit>(&window, "accountNumberEdit")->setText(account);
        requiredChild<QLineEdit>(&window, "accountPasswordEdit")->setText(password);
        requiredChild<QPushButton>(&window, "accountLoginButton")->click();
    };
    login(accounts.first());
    // Qt 聚焦表格时可能建立键盘当前行；整行选择不得继承上一会话。
    QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
    deposits->setCurrentIndex(deposits->model()->index(0, 0));
    QVERIFY(withdrawButton->isEnabled());
    requiredChild<QAction>(&window, "actionReturnWorkspace")->trigger();
    login(accounts.last());
    QCOMPARE(deposits->model()->index(0, 0).data(Qt::UserRole).toString(), QStringLiteral("FD000002"));
    QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!hasSelectedDeposit(deposits, QStringLiteral("FD000001")));

    deposits->setCurrentIndex(deposits->model()->index(0, 0));
    requiredChild<QPushButton>(&window, "logoutDepositorButton")->click();
    QCOMPARE(deposits->model()->rowCount(), 0);
    QVERIFY(!deposits->currentIndex().isValid());
    QVERIFY(!withdrawButton->isEnabled());
    login(accounts.last());
    QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
    deposits->setCurrentIndex(deposits->model()->index(0, 0));
    requiredChild<QAction>(&window, "actionSwitchEmployee")->trigger();
    QCOMPARE(deposits->model()->rowCount(), 0);
    QVERIFY(!deposits->currentIndex().isValid());
    QVERIFY(!withdrawButton->isEnabled());
    requiredChild<QLineEdit>(&window, "employeeIdEdit")->setText(QStringLiteral("E04"));
    requiredChild<QPushButton>(&window, "employeeEnterButton")->click();
    login(accounts.first());
    QVERIFY(deposits->selectionModel()->selectedRows().isEmpty());
    QVERIFY(!hasSelectedDeposit(deposits, QStringLiteral("FD000002")));
}

QTEST_MAIN(MainWindowTest)

#include "tst_ui.moc"

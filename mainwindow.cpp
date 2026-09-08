// 主窗口实现：把 Designer 页面连接到 BankService，并维护各查询表格的展示状态。
#include "mainwindow.h"

#include "persistence/filemanager.h"
#include "ui/depositdialog.h"
#include "ui/passworddialog.h"
#include "ui/profiledialog.h"
#include "ui/unfreezedialog.h"
#include "ui/withdrawdialog.h"
#include "ui_mainwindow.h"
#include "utils/moneyutils.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace {

QStandardItem *textItem(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

QString dateText(const QDate &date)
{
    return date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd"))
                          : QStringLiteral("—");
}

QString dateTimeText(const QDateTime &dateTime)
{
    return dateTime.isValid()
               ? dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
               : QStringLiteral("—");
}

QString transactionTypeText(const bank::Transaction &transaction)
{
    if (transaction.type() == bank::TransactionType::Deposit) {
        return QStringLiteral("存款");
    }
    return transaction.withdrawalKind() == bank::WithdrawalKind::Early
               ? QStringLiteral("提前支取")
               : QStringLiteral("到期支取");
}

QString auditResultText(bank::AuditResult result)
{
    switch (result) {
    case bank::AuditResult::Success:
        return QStringLiteral("成功");
    case bank::AuditResult::Failure:
        return QStringLiteral("失败");
    case bank::AuditResult::Warning:
        return QStringLiteral("警告");
    }
    return QStringLiteral("未知");
}

using AuditActionPresentation = std::pair<QString, QString>;

const std::array<AuditActionPresentation, 10> &auditActionPresentations()
{
    // 文件中保留稳定英文动作码，界面和筛选框只负责映射为中文。
    static const std::array<AuditActionPresentation, 10> presentations{
        AuditActionPresentation{QStringLiteral("LOGIN"), QStringLiteral("储户登录")},
        AuditActionPresentation{QStringLiteral("OPEN_ACCOUNT"), QStringLiteral("开户")},
        AuditActionPresentation{QStringLiteral("DEPOSIT"), QStringLiteral("新增存款")},
        AuditActionPresentation{QStringLiteral("EARLY_WITHDRAW"), QStringLiteral("提前支取")},
        AuditActionPresentation{QStringLiteral("MATURED_WITHDRAW"), QStringLiteral("到期支取")},
        AuditActionPresentation{QStringLiteral("UPDATE_PROFILE"), QStringLiteral("修改资料")},
        AuditActionPresentation{QStringLiteral("CHANGE_PASSWORD"), QStringLiteral("修改密码")},
        AuditActionPresentation{QStringLiteral("REPORT_LOSS"), QStringLiteral("账户挂失")},
        AuditActionPresentation{QStringLiteral("UNFREEZE_ACCOUNT"), QStringLiteral("解除挂失")},
        AuditActionPresentation{QStringLiteral("CORE_DATA_SAVE"), QStringLiteral("核心数据保存")}};
    return presentations;
}

QString auditActionText(const QString &actionCode)
{
    for (const auto &[code, text] : auditActionPresentations()) {
        if (code == actionCode) {
            return text;
        }
    }
    return QStringLiteral("未知操作");
}

QString auditFailureReasonText(const QString &reasonCode)
{
    if (reasonCode == QStringLiteral("NONE")) {
        return QStringLiteral("—");
    }
    if (reasonCode == QStringLiteral("AUTH_FAILED")
        || reasonCode == QStringLiteral("PASSWORD_ERROR")) {
        return QStringLiteral("密码错误");
    }
    if (reasonCode == QStringLiteral("ACCOUNT_NOT_FOUND")) {
        return QStringLiteral("账户不存在");
    }
    if (reasonCode == QStringLiteral("ACCOUNT_LOCKED")) {
        return QStringLiteral("账户已锁定");
    }
    if (reasonCode == QStringLiteral("ACCOUNT_LOST")) {
        return QStringLiteral("账户已挂失");
    }
    if (reasonCode == QStringLiteral("ACCOUNT_NOT_LOST")) {
        return QStringLiteral("账户未挂失");
    }
    if (reasonCode == QStringLiteral("INVALID_AMOUNT")) {
        return QStringLiteral("金额无效");
    }
    if (reasonCode == QStringLiteral("INSUFFICIENT_BALANCE")
        || reasonCode == QStringLiteral("INSUFFICIENT_PRINCIPAL")) {
        return QStringLiteral("余额不足");
    }
    if (reasonCode == QStringLiteral("DEPOSIT_CLOSED")) {
        return QStringLiteral("存款已结清");
    }
    if (reasonCode == QStringLiteral("INVALID_INPUT")) {
        return QStringLiteral("输入无效");
    }
    if (reasonCode == QStringLiteral("PERSISTENCE_ERROR")) {
        return QStringLiteral("保存失败");
    }
    return QStringLiteral("操作失败");
}

struct ColumnWidthRange {
    int minimum;
    int maximum;
};

template<std::size_t ColumnCount>
void resizeTableColumnsWithinRanges(
    QTableView *tableView,
    const std::array<ColumnWidthRange, ColumnCount> &widthRanges,
    const std::array<int, ColumnCount> &remainingWidthWeights = {})
{
    // maximum 只限制内容测量得到的基础宽度；可选权重负责把 viewport 余量均衡分配。
    tableView->resizeColumnsToContents();
    int currentWidth = 0;
    int totalWeight = 0;
    int lastWeightedColumn = -1;
    for (int column = 0; column < static_cast<int>(widthRanges.size()); ++column) {
        const ColumnWidthRange range = widthRanges.at(column);
        const int width = std::clamp(tableView->columnWidth(column),
                                     range.minimum,
                                     range.maximum);
        tableView->setColumnWidth(column, width);
        currentWidth += tableView->columnWidth(column);
        if (remainingWidthWeights.at(column) > 0) {
            totalWeight += remainingWidthWeights.at(column);
            lastWeightedColumn = column;
        }
    }

    const int remainingWidth = tableView->viewport()->width() - currentWidth;
    if (remainingWidth <= 0 || totalWeight <= 0) {
        return;
    }

    int distributedWidth = 0;
    for (int column = 0; column < static_cast<int>(widthRanges.size()); ++column) {
        const int weight = remainingWidthWeights.at(column);
        if (weight <= 0) {
            continue;
        }
        const int extraWidth = remainingWidth * weight / totalWeight;
        tableView->setColumnWidth(column, tableView->columnWidth(column) + extraWidth);
        distributedWidth += extraWidth;
    }
    if (lastWeightedColumn >= 0) {
        tableView->setColumnWidth(
            lastWeightedColumn,
            tableView->columnWidth(lastWeightedColumn)
                + remainingWidth - distributedWidth);
    }
}

void resizeDepositTableColumns(QTableView *tableView)
{
    // 数据从空表变为有行时重新测量，并夹在可读范围内，避免等到点击表头才布局。
    constexpr std::array<ColumnWidthRange, 8> widthRanges{
        ColumnWidthRange{120, 150},
        ColumnWidthRange{115, 145},
        ColumnWidthRange{115, 145},
        ColumnWidthRange{100, 140},
        ColumnWidthRange{90, 110},
        ColumnWidthRange{115, 130},
        ColumnWidthRange{115, 130},
        ColumnWidthRange{85, 110}};

    resizeTableColumnsWithinRanges(tableView, widthRanges);
}

void prepareDepositTable(QTableView *tableView)
{
    QHeaderView *header = tableView->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    resizeDepositTableColumns(tableView);
}

void prepareTransactionTable(QTableView *tableView)
{
    QHeaderView *header = tableView->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);

    // 交易字段较固定，保留可拖动宽度并让营业员列吸收窗口剩余空间。
    constexpr std::array<int, 8> columnWidths{150, 180, 150, 110, 115, 115, 115, 90};
    for (int column = 0; column < static_cast<int>(columnWidths.size()); ++column) {
        tableView->setColumnWidth(column, columnWidths.at(column));
    }
    header->setSectionResizeMode(7, QHeaderView::Stretch);
}

void prepareDepositorTable(QTableView *tableView)
{
    QHeaderView *header = tableView->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);

    // 地址最适合随窗口伸缩，其他业务列保持最低可读宽度。
    constexpr std::array<int, 8> columnWidths{115, 120, 260, 85, 120, 105, 100, 135};
    for (int column = 0; column < static_cast<int>(columnWidths.size()); ++column) {
        tableView->setColumnWidth(column, columnWidths.at(column));
    }
    header->setSectionResizeMode(2, QHeaderView::Stretch);
}

void resizeReserveTableColumns(QTableView *tableView)
{
    // 日期与金额列适度扩展，笔数列较窄；不足时保留最小宽度并由滚动条承载。
    constexpr std::array<ColumnWidthRange, 5> widthRanges{
        ColumnWidthRange{115, 135},
        ColumnWidthRange{90, 110},
        ColumnWidthRange{120, 150},
        ColumnWidthRange{120, 150},
        ColumnWidthRange{120, 160}};
    constexpr std::array<int, 5> remainingWidthWeights{2, 1, 2, 2, 2};

    resizeTableColumnsWithinRanges(tableView, widthRanges, remainingWidthWeights);
}

void prepareReserveTable(QTableView *tableView)
{
    QHeaderView *header = tableView->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    resizeReserveTableColumns(tableView);
}

void resizeAuditTableColumns(QTableView *tableView)
{
    // 审计内容长度差异大，动态测量后限幅，防止失败原因独占整个表格。
    constexpr std::array<ColumnWidthRange, 8> widthRanges{
        ColumnWidthRange{170, 190},
        ColumnWidthRange{70, 85},
        ColumnWidthRange{100, 120},
        ColumnWidthRange{110, 140},
        ColumnWidthRange{95, 115},
        ColumnWidthRange{95, 115},
        ColumnWidthRange{65, 80},
        ColumnWidthRange{300, 320}};

    resizeTableColumnsWithinRanges(tableView, widthRanges);
}

void prepareAuditTable(QTableView *tableView)
{
    QHeaderView *header = tableView->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    resizeAuditTableColumns(tableView);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(std::make_unique<bank::BankService>(
                     std::make_shared<bank::persistence::FileManager>()),
                 parent)
{
}

MainWindow::MainWindow(std::unique_ptr<bank::BankService> service, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , bankService_(std::move(service))
{
    ui->setupUi(this);
    setupTableModels();
    setupConnections();

    ui->forecastBaseDateEdit->setDate(QDate::currentDate());
    // 固定显示项由 Designer 定义，这里只关联日志筛选使用的英文动作码。
    ui->auditActionComboBox->setItemData(0, QString());
    for (const auto &[code, text] : auditActionPresentations()) {
        const int index = ui->auditActionComboBox->findText(text);
        ui->auditActionComboBox->setItemData(index, code);
    }

    initializeService();
}

MainWindow::~MainWindow()
{
    ui->reserveForecastTableView->viewport()->removeEventFilter(this);
    delete ui;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize
        && watched == ui->reserveForecastTableView->viewport()) {
        const auto *resizeEvent = static_cast<QResizeEvent *>(event);
        if (resizeEvent->size().width() != resizeEvent->oldSize().width()) {
            resizeReserveTableColumns(ui->reserveForecastTableView);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupConnections()
{
    // 这里仅连接交互入口；槽函数再统一调用服务层并刷新展示。
    connect(ui->employeeEnterButton,
            &QPushButton::clicked,
            this,
            &MainWindow::enterEmployeeSession);
    connect(ui->employeeIdEdit,
            &QLineEdit::returnPressed,
            this,
            &MainWindow::enterEmployeeSession);
    connect(ui->switchEmployeeButton,
            &QPushButton::clicked,
            this,
            &MainWindow::switchEmployee);
    connect(ui->openAccountButton,
            &QPushButton::clicked,
            this,
            &MainWindow::openAccountPage);
    connect(ui->openAccountCancelButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showWorkspace);
    connect(ui->createAccountButton,
            &QPushButton::clicked,
            this,
            &MainWindow::createAccount);
    connect(ui->depositorLoginButton,
            &QPushButton::clicked,
            this,
            &MainWindow::depositorLoginPage);
    connect(ui->accountLoginCancelButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showWorkspace);
    connect(ui->accountLoginButton,
            &QPushButton::clicked,
            this,
            &MainWindow::loginDepositor);
    connect(ui->accountPasswordEdit,
            &QLineEdit::returnPressed,
            this,
            &MainWindow::loginDepositor);
    connect(ui->logoutDepositorButton,
            &QPushButton::clicked,
            this,
            &MainWindow::logoutDepositor);

    connect(ui->showClosedDepositsCheckBox,
            &QCheckBox::toggled,
            this,
            [this] {
                const QString depositId = selectedDepositId();
                refreshAccountCenter();
                restoreDepositSelection(depositId);
            });
    connect(ui->depositsTableView,
            &QTableView::clicked,
            this,
            [this] { updateAccountActionState(); });
    connect(ui->newDepositButton,
            &QPushButton::clicked,
            this,
            &MainWindow::createFixedDeposit);
    connect(ui->withdrawSelectedButton,
            &QPushButton::clicked,
            this,
            &MainWindow::withdrawSelectedDeposit);
    connect(ui->editProfileButton,
            &QPushButton::clicked,
            this,
            &MainWindow::editProfile);
    connect(ui->changePasswordButton,
            &QPushButton::clicked,
            this,
            &MainWindow::changePassword);
    connect(ui->reportLossButton,
            &QPushButton::clicked,
            this,
            &MainWindow::reportLoss);
    connect(ui->unfreezeAccountButton,
            &QPushButton::clicked,
            this,
            &MainWindow::unfreezeAccount);

    connect(ui->allDepositorsButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showAllDepositors);
    connect(ui->searchDepositorsButton,
            &QPushButton::clicked,
            this,
            &MainWindow::refreshAllDepositors);
    connect(ui->depositorSearchEdit,
            &QLineEdit::returnPressed,
            this,
            &MainWindow::refreshAllDepositors);
    connect(ui->resetDepositorFilterButton,
            &QPushButton::clicked,
            this,
            &MainWindow::resetDepositorFilter);
    connect(ui->allDepositorsBackButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showWorkspace);

    connect(ui->reserveForecastButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showReserveForecast);
    connect(ui->refreshForecastButton,
            &QPushButton::clicked,
            this,
            &MainWindow::refreshReserveForecast);
    connect(ui->reserveForecastBackButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showWorkspace);
    connect(ui->auditLogButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showAuditLog);
    connect(ui->refreshAuditButton,
            &QPushButton::clicked,
            this,
            &MainWindow::refreshAuditLog);
    connect(ui->auditLogBackButton,
            &QPushButton::clicked,
            this,
            &MainWindow::showWorkspace);

    connect(ui->actionReturnWorkspace, &QAction::triggered, this, &MainWindow::showWorkspace);
    connect(ui->actionLogoutDepositor,
            &QAction::triggered,
            this,
            &MainWindow::logoutDepositor);
    connect(ui->actionSwitchEmployee,
            &QAction::triggered,
            this,
            &MainWindow::switchEmployee);
    connect(ui->actionAllDepositors,
            &QAction::triggered,
            this,
            &MainWindow::showAllDepositors);
    connect(ui->actionReserveForecast,
            &QAction::triggered,
            this,
            &MainWindow::showReserveForecast);
    connect(ui->actionAuditLog, &QAction::triggered, this, &MainWindow::showAuditLog);
    connect(ui->actionExit, &QAction::triggered, qApp, &QApplication::quit);
}

void MainWindow::setupTableModels()
{
    // 模型以主窗口为 QObject 父对象，随窗口释放，不需要手工 delete。
    depositsModel_ = new QStandardItemModel(this);
    transactionsModel_ = new QStandardItemModel(this);
    depositorsModel_ = new QStandardItemModel(this);
    reserveModel_ = new QStandardItemModel(this);
    auditModel_ = new QStandardItemModel(this);

    depositsModel_->setHorizontalHeaderLabels(
        {QStringLiteral("存款编号"),
         QStringLiteral("原始本金"),
         QStringLiteral("剩余本金"),
         QStringLiteral("储种"),
         QStringLiteral("年利率"),
         QStringLiteral("存入日"),
         QStringLiteral("到期日"),
         QStringLiteral("状态")});
    transactionsModel_->setHorizontalHeaderLabels(
        {QStringLiteral("交易号"),
         QStringLiteral("日期时间"),
         QStringLiteral("存款编号"),
         QStringLiteral("业务类型"),
         QStringLiteral("本金"),
         QStringLiteral("利息"),
         QStringLiteral("实际金额"),
         QStringLiteral("营业员")});
    depositorsModel_->setHorizontalHeaderLabels(
        {QStringLiteral("账号"),
         QStringLiteral("姓名"),
         QStringLiteral("地址"),
         QStringLiteral("状态"),
         QStringLiteral("挂失日期"),
         QStringLiteral("开户营业员"),
         QStringLiteral("存款笔数"),
         QStringLiteral("剩余本金")});
    reserveModel_->setHorizontalHeaderLabels(
        {QStringLiteral("到期日期"),
         QStringLiteral("存款笔数"),
         QStringLiteral("本金"),
         QStringLiteral("到期利息"),
         QStringLiteral("预计备款")});
    auditModel_->setHorizontalHeaderLabels(
        {QStringLiteral("日期时间"),
         QStringLiteral("营业员"),
         QStringLiteral("账号"),
         QStringLiteral("操作类型"),
         QStringLiteral("本金"),
         QStringLiteral("利息"),
         QStringLiteral("结果"),
         QStringLiteral("失败原因")});

    // setModel 后才存在对应 selectionModel，随后连接键盘和程序化选行变化。
    ui->depositsTableView->setModel(depositsModel_);
    ui->transactionsTableView->setModel(transactionsModel_);
    ui->allDepositorsTableView->setModel(depositorsModel_);
    ui->reserveForecastTableView->setModel(reserveModel_);
    ui->auditLogTableView->setModel(auditModel_);
    connect(ui->depositsTableView->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            [this] { updateAccountActionState(); });
    prepareDepositTable(ui->depositsTableView);
    prepareTransactionTable(ui->transactionsTableView);
    prepareDepositorTable(ui->allDepositorsTableView);
    prepareReserveTable(ui->reserveForecastTableView);
    ui->reserveForecastTableView->viewport()->installEventFilter(this);
    prepareAuditTable(ui->auditLogTableView);
}

void MainWindow::initializeService()
{
    ui->mainStackedWidget->setCurrentWidget(ui->employeeLoginPage);
    if (!bankService_) {
        ui->employeeLoginMessageLabel->setText(QStringLiteral("业务服务不可用。"));
        ui->employeeEnterButton->setEnabled(false);
        updateSessionDisplay();
        return;
    }

    const bank::ServiceResult result = bankService_->initialize();
    showServiceResult(result, ui->employeeLoginMessageLabel);
    ui->employeeEnterButton->setEnabled(result.success);
    updateSessionDisplay();
}

void MainWindow::updateSessionDisplay()
{
    const bool employeeActive = bankService_ && !bankService_->currentEmployeeId().isEmpty();
    const bool depositorActive = bankService_
                                 && !bankService_->currentDepositorAccount().isEmpty();
    ui->currentEmployeeLabel->setText(
        employeeActive
            ? QStringLiteral("当前营业员：%1").arg(bankService_->currentEmployeeId())
            : QStringLiteral("当前营业员：未登录"));
    ui->currentDepositorLabel->setText(
        depositorActive
            ? QStringLiteral("当前储户：%1").arg(bankService_->currentDepositorAccount())
            : QStringLiteral("当前储户：未登录"));

    ui->actionReturnWorkspace->setEnabled(employeeActive);
    ui->actionLogoutDepositor->setEnabled(depositorActive);
    ui->actionSwitchEmployee->setEnabled(employeeActive);
    ui->queryMenu->setEnabled(employeeActive);
    updateAccountActionState();
}

void MainWindow::showWorkspace()
{
    updateSessionDisplay();
    if (!bankService_ || bankService_->currentEmployeeId().isEmpty()) {
        ui->mainStackedWidget->setCurrentWidget(ui->employeeLoginPage);
        return;
    }
    ui->mainStackedWidget->setCurrentWidget(ui->workspacePage);
}

void MainWindow::showServiceResult(const bank::ServiceResult &result, QLabel *targetLabel)
{
    QString message = result.message;
    if (!result.warningMessage.isEmpty()) {
        message += QStringLiteral("\n%1").arg(result.warningMessage);
    }
    if (targetLabel) {
        targetLabel->setText(message);
        targetLabel->setStyleSheet(result.success
                                       ? (result.warningMessage.isEmpty()
                                              ? QStringLiteral("color: #176b2c;")
                                              : QStringLiteral("color: #9a6700;"))
                                       : QStringLiteral("color: #b42318;"));
    }
    ui->statusBar->showMessage(message, 8000);
}

void MainWindow::enterEmployeeSession()
{
    if (!bankService_) {
        ui->employeeLoginMessageLabel->setText(QStringLiteral("业务服务不可用。"));
        return;
    }
    const bank::ServiceResult result = bankService_->enterEmployeeSession(
        ui->employeeIdEdit->text());
    showServiceResult(result, ui->employeeLoginMessageLabel);
    if (result.success) {
        ui->employeeIdEdit->clear();
        showWorkspace();
    }
}

void MainWindow::switchEmployee()
{
    if (!bankService_) {
        return;
    }
    const bank::ServiceResult result = bankService_->switchEmployee();
    // 切换营业员时清除账户和查询展示，避免前一会话的信息残留在新会话中。
    clearAccountPresentation();
    depositorsModel_->removeRows(0, depositorsModel_->rowCount());
    reserveModel_->removeRows(0, reserveModel_->rowCount());
    auditModel_->removeRows(0, auditModel_->rowCount());
    ui->accountNumberEdit->clear();
    ui->accountPasswordEdit->clear();
    ui->accountLoginMessageLabel->clear();
    ui->depositorSearchEdit->clear();
    ui->depositorStatusComboBox->setCurrentIndex(0);
    ui->auditActionComboBox->setCurrentIndex(0);
    updateSessionDisplay();
    showServiceResult(result, ui->employeeLoginMessageLabel);
    ui->mainStackedWidget->setCurrentWidget(ui->employeeLoginPage);
    ui->employeeIdEdit->setFocus();
}

void MainWindow::openAccountPage()
{
    ui->openAccountNameEdit->clear();
    ui->openAccountAddressEdit->clear();
    ui->openAccountPasswordEdit->clear();
    ui->openAccountConfirmPasswordEdit->clear();
    ui->openAccountMessageLabel->clear();
    ui->mainStackedWidget->setCurrentWidget(ui->openAccountPage);
    ui->openAccountNameEdit->setFocus();
}

void MainWindow::createAccount()
{
    const bank::OpenAccountResult result = bankService_->openAccount(
        ui->openAccountNameEdit->text(),
        ui->openAccountAddressEdit->text(),
        ui->openAccountPasswordEdit->text(),
        ui->openAccountConfirmPasswordEdit->text());
    showServiceResult(result.status, ui->openAccountMessageLabel);
    if (!result.status.success) {
        return;
    }

    ui->openAccountMessageLabel->setText(
        QStringLiteral("开户成功，新账号：%1。请返回工作台后登录。").arg(result.accountNumber)
        + (result.status.warningMessage.isEmpty()
               ? QString()
               : QStringLiteral("\n%1").arg(result.status.warningMessage)));
    ui->accountNumberEdit->setText(result.accountNumber);
    ui->openAccountPasswordEdit->clear();
    ui->openAccountConfirmPasswordEdit->clear();
}

void MainWindow::depositorLoginPage()
{
    ui->accountPasswordEdit->clear();
    ui->accountLoginMessageLabel->clear();
    ui->mainStackedWidget->setCurrentWidget(ui->depositorLoginPage);
    if (ui->accountNumberEdit->text().isEmpty()) {
        ui->accountNumberEdit->setFocus();
    } else {
        ui->accountPasswordEdit->setFocus();
    }
}

void MainWindow::loginDepositor()
{
    const bank::ServiceResult result = bankService_->loginDepositor(
        ui->accountNumberEdit->text(), ui->accountPasswordEdit->text());
    showServiceResult(result, ui->accountLoginMessageLabel);
    ui->accountPasswordEdit->clear();
    if (result.success) {
        ui->accountMessageLabel->clear();
        refreshAccountCenter();
        ui->mainStackedWidget->setCurrentWidget(ui->accountCenterPage);
    }
}

void MainWindow::logoutDepositor()
{
    if (!bankService_ || bankService_->currentDepositorAccount().isEmpty()) {
        showWorkspace();
        return;
    }
    const bank::ServiceResult result = bankService_->logoutDepositor();
    clearAccountPresentation();
    showServiceResult(result, ui->workspaceMessageLabel);
    showWorkspace();
}

void MainWindow::refreshAccountCenter()
{
    // 每次用服务层快照全量重建，避免旧行和旧 QModelIndex 指向已经变化的数据。
    depositsModel_->removeRows(0, depositsModel_->rowCount());
    transactionsModel_->removeRows(0, transactionsModel_->rowCount());
    const bank::AccountDetailsResult result = bankService_->currentAccountDetails();
    if (!result.status.success || !result.details) {
        showServiceResult(result.status, ui->accountMessageLabel);
        updateAccountActionState();
        return;
    }
    const bank::AccountDetails &details = *result.details;
    ui->accountNumberLabel->setText(
        QStringLiteral("账号：%1").arg(details.accountNumber));
    ui->depositorNameLabel->setText(QStringLiteral("姓名：%1").arg(details.name));
    ui->accountStatusLabel->setText(
        details.lost ? QStringLiteral("状态：已挂失") : QStringLiteral("状态：正常"));
    ui->lostDateLabel->setText(
        QStringLiteral("挂失日期：%1")
            .arg(details.lostDate ? dateText(*details.lostDate) : QStringLiteral("—")));

    qint64 totalPrincipal = 0;
    const bank::DepositorQueryResult summary = bankService_->queryDepositors(
        details.accountNumber);
    if (summary.status.success && !summary.depositors.isEmpty()) {
        totalPrincipal = summary.depositors.first().remainingPrincipalCents;
    }
    ui->totalPrincipalLabel->setText(
        QStringLiteral("剩余本金：%1").arg(bank::MoneyUtils::formatCents(totalPrincipal)));

    const QDate referenceDate = bankService_->businessDate();
    for (const bank::FixedDeposit &deposit : details.deposits) {
        if (!ui->showClosedDepositsCheckBox->isChecked()
            && deposit.remainingPrincipalCents() == 0) {
            continue;
        }
        QList<QStandardItem *> row{
            textItem(deposit.depositId()),
            textItem(bank::MoneyUtils::formatCents(deposit.originalPrincipalCents())),
            textItem(bank::MoneyUtils::formatCents(deposit.remainingPrincipalCents())),
            textItem(bank::depositTermDisplayName(deposit.term())),
            textItem(bank::MoneyUtils::formatRateBasisPoints(
                deposit.annualRateBasisPoints())),
            textItem(dateText(deposit.startDate())),
            textItem(dateText(deposit.maturityDate())),
            textItem(bank::depositStatusDisplayName(deposit.statusOn(referenceDate)))};
        // UserRole 保存真实编号和整数分，操作按钮无需反解析格式化后的显示文本。
        row.first()->setData(deposit.depositId(), Qt::UserRole);
        row.first()->setData(deposit.remainingPrincipalCents(), Qt::UserRole + 1);
        depositsModel_->appendRow(row);
    }
    resizeDepositTableColumns(ui->depositsTableView);

    for (const bank::Transaction &transaction : details.transactions) {
        transactionsModel_->appendRow(
            {textItem(transaction.transactionId()),
             textItem(dateTimeText(transaction.dateTime())),
             textItem(transaction.depositId()),
             textItem(transactionTypeText(transaction)),
             textItem(bank::MoneyUtils::formatCents(transaction.principalAmountCents())),
             textItem(bank::MoneyUtils::formatCents(transaction.interestAmountCents())),
             textItem(bank::MoneyUtils::formatCents(transaction.actualPayoutCents())),
             textItem(transaction.employeeId())});
    }
    // 刷新后清除选择，防止按钮继续操作刷新前选中的存款。
    ui->depositsTableView->selectionModel()->clear();
    updateSessionDisplay();
    updateAccountActionState();
}

void MainWindow::clearAccountPresentation()
{
    depositsModel_->removeRows(0, depositsModel_->rowCount());
    transactionsModel_->removeRows(0, transactionsModel_->rowCount());
    ui->depositsTableView->selectionModel()->clear();
    ui->accountNumberLabel->setText(QStringLiteral("账号：—"));
    ui->depositorNameLabel->setText(QStringLiteral("姓名：—"));
    ui->accountStatusLabel->setText(QStringLiteral("状态：—"));
    ui->lostDateLabel->setText(QStringLiteral("挂失日期：—"));
    ui->totalPrincipalLabel->setText(QStringLiteral("剩余本金：¥0.00"));
    ui->accountMessageLabel->clear();
    updateAccountActionState();
}

void MainWindow::updateAccountActionState()
{
    const bank::Depositor *depositor = bankService_ ? bankService_->currentDepositor() : nullptr;
    const bool loggedIn = depositor != nullptr;
    const bool normal = loggedIn && !depositor->isLost();
    const QModelIndex selected = ui->depositsTableView->currentIndex();
    const QModelIndex idIndex = selected.isValid()
                                    ? depositsModel_->index(selected.row(), 0)
                                    : QModelIndex();
    const bool withdrawable = idIndex.isValid()
                              && idIndex.data(Qt::UserRole + 1).toLongLong() > 0;

    // 挂失账户只能查看和解除挂失；结清存款即使显示也不能再次支取。
    ui->newDepositButton->setEnabled(normal);
    ui->withdrawSelectedButton->setEnabled(normal && withdrawable);
    ui->editProfileButton->setEnabled(normal);
    ui->changePasswordButton->setEnabled(normal);
    ui->reportLossButton->setEnabled(normal);
    ui->unfreezeAccountButton->setEnabled(loggedIn && !normal);
    ui->logoutDepositorButton->setEnabled(loggedIn);
}

QString MainWindow::selectedDepositId() const
{
    const QModelIndex current = ui->depositsTableView->currentIndex();
    if (!current.isValid()) {
        return {};
    }
    return depositsModel_->index(current.row(), 0).data(Qt::UserRole).toString();
}

void MainWindow::restoreDepositSelection(const QString &depositId)
{
    // 在新模型中按稳定编号重新定位，不复用刷新前的 QModelIndex。
    for (int row = 0; row < depositsModel_->rowCount(); ++row) {
        const QModelIndex index = depositsModel_->index(row, 0);
        if (index.data(Qt::UserRole).toString() == depositId) {
            ui->depositsTableView->selectionModel()->setCurrentIndex(
                index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            break;
        }
    }
    updateAccountActionState();
}

void MainWindow::createFixedDeposit()
{
    // 使用服务层业务日期，使界面摘要和可注入时钟下的实际存款日期一致。
    QDate startDate = bankService_->businessDate();
    if (!startDate.isValid()) {
        startDate = QDate::currentDate();
    }
    DepositDialog dialog(startDate, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString depositId = selectedDepositId();
    const bank::DepositResult result = bankService_->addFixedDeposit(
        dialog.principalCents(), dialog.selectedTerm());
    refreshAccountCenter();
    if (!result.status.success) {
        restoreDepositSelection(depositId);
    }
    showServiceResult(result.status, ui->accountMessageLabel);
}

void MainWindow::withdrawSelectedDeposit()
{
    const QString depositId = selectedDepositId();
    if (depositId.isEmpty()) {
        ui->accountMessageLabel->setText(QStringLiteral("请先选中一笔可支取的存款。"));
        return;
    }
    // 对话框只做无副作用预览；用户接受后才在主窗口提交真实支取。
    WithdrawDialog dialog(bankService_.get(), depositId, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bank::WithdrawalResult result = bankService_->withdraw(
        depositId, dialog.principalCents());
    refreshAccountCenter();
    if (!result.status.success || result.remainingPrincipalCents > 0) {
        restoreDepositSelection(depositId);
    }
    showServiceResult(result.status, ui->accountMessageLabel);
}

void MainWindow::editProfile()
{
    const bank::AccountDetailsResult details = bankService_->currentAccountDetails();
    if (!details.status.success || !details.details) {
        showServiceResult(details.status, ui->accountMessageLabel);
        return;
    }
    ProfileDialog dialog(details.details->name, details.details->address, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString depositId = selectedDepositId();
    const bank::ServiceResult result = bankService_->updateProfile(dialog.name(),
                                                                   dialog.address());
    refreshAccountCenter();
    restoreDepositSelection(depositId);
    showServiceResult(result, ui->accountMessageLabel);
}

void MainWindow::changePassword()
{
    PasswordDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString depositId = selectedDepositId();
    const bank::ServiceResult result = bankService_->changePassword(
        dialog.currentPassword(), dialog.newPassword(), dialog.passwordConfirmation());
    refreshAccountCenter();
    restoreDepositSelection(depositId);
    showServiceResult(result, ui->accountMessageLabel);
}

void MainWindow::reportLoss()
{
    QMessageBox confirmation(QMessageBox::Question,
                             QStringLiteral("确认挂失"),
                             QStringLiteral("挂失后将禁止存款、支取和资料修改，确定继续吗？"),
                             QMessageBox::Yes | QMessageBox::No,
                             this);
    confirmation.setDefaultButton(QMessageBox::No);
    QAbstractButton *confirmButton = confirmation.button(QMessageBox::Yes);
    QAbstractButton *cancelButton = confirmation.button(QMessageBox::No);
    confirmButton->setText(QStringLiteral("确认"));
    cancelButton->setText(QStringLiteral("取消"));
    confirmation.setEscapeButton(cancelButton);
    const QMessageBox::StandardButton answer =
        static_cast<QMessageBox::StandardButton>(confirmation.exec());
    if (answer != QMessageBox::Yes) {
        return;
    }
    const QString depositId = selectedDepositId();
    const bank::ServiceResult result = bankService_->reportLoss();
    refreshAccountCenter();
    if (!result.success) {
        restoreDepositSelection(depositId);
    }
    showServiceResult(result, ui->accountMessageLabel);
}

void MainWindow::unfreezeAccount()
{
    UnfreezeDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString depositId = selectedDepositId();
    const bank::ServiceResult result = bankService_->unfreezeAccount(dialog.password());
    refreshAccountCenter();
    restoreDepositSelection(depositId);
    showServiceResult(result, ui->accountMessageLabel);
}

void MainWindow::showAllDepositors()
{
    if (!bankService_ || bankService_->currentEmployeeId().isEmpty()) {
        return;
    }
    ui->mainStackedWidget->setCurrentWidget(ui->allDepositorsPage);
    refreshAllDepositors();
}

void MainWindow::refreshAllDepositors()
{
    // 纯数字按账号精确查询，其余文本按姓名模糊查询，保持一个简洁搜索框。
    const QString searchText = ui->depositorSearchEdit->text().trimmed();
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    const QString account = digits.match(searchText).hasMatch() ? searchText : QString();
    const QString name = !searchText.isEmpty() && account.isEmpty() ? searchText : QString();
    bank::DepositorStatusFilter filter = bank::DepositorStatusFilter::All;
    if (ui->depositorStatusComboBox->currentIndex() == 1) {
        filter = bank::DepositorStatusFilter::Normal;
    } else if (ui->depositorStatusComboBox->currentIndex() == 2) {
        filter = bank::DepositorStatusFilter::Lost;
    }

    const bank::DepositorQueryResult result = bankService_->queryDepositors(account,
                                                                            name,
                                                                            filter);
    depositorsModel_->removeRows(0, depositorsModel_->rowCount());
    if (!result.status.success) {
        showServiceResult(result.status, ui->allDepositorsMessageLabel);
        return;
    }
    for (const bank::DepositorSummary &depositor : result.depositors) {
        depositorsModel_->appendRow(
            {textItem(depositor.accountNumber),
             textItem(depositor.name),
             textItem(depositor.address),
             textItem(depositor.lost ? QStringLiteral("挂失") : QStringLiteral("正常")),
             textItem(depositor.lostDate ? dateText(*depositor.lostDate)
                                         : QStringLiteral("—")),
             textItem(depositor.openingEmployeeId),
             textItem(QString::number(depositor.depositCount)),
             textItem(bank::MoneyUtils::formatCents(depositor.remainingPrincipalCents))});
    }
    ui->allDepositorsMessageLabel->setText(
        QStringLiteral("共找到 %1 个储户。任何完整明细仍需密码登录后查看。")
            .arg(result.depositors.size()));
}

void MainWindow::resetDepositorFilter()
{
    ui->depositorSearchEdit->clear();
    ui->depositorStatusComboBox->setCurrentIndex(0);
    refreshAllDepositors();
}

void MainWindow::showReserveForecast()
{
    if (!bankService_ || bankService_->currentEmployeeId().isEmpty()) {
        return;
    }
    QDate baseDate = bankService_->businessDate();
    if (!baseDate.isValid()) {
        baseDate = QDate::currentDate();
    }
    ui->forecastBaseDateEdit->setDate(baseDate);
    ui->mainStackedWidget->setCurrentWidget(ui->reserveForecastPage);
    refreshReserveForecast();
}

void MainWindow::refreshReserveForecast()
{
    // 服务结果固定包含基准日后的明天、后天和大后天，空日也显示零值行。
    const bank::ReserveForecastResult result = bankService_->reserveForecast(
        ui->forecastBaseDateEdit->date());
    reserveModel_->removeRows(0, reserveModel_->rowCount());
    if (!result.status.success) {
        showServiceResult(result.status, ui->reserveForecastMessageLabel);
        return;
    }
    for (const bank::DailyReserveForecast &day : result.days) {
        reserveModel_->appendRow(
            {textItem(dateText(day.date)),
             textItem(QString::number(day.depositCount)),
             textItem(bank::MoneyUtils::formatCents(day.principalCents)),
             textItem(bank::MoneyUtils::formatCents(day.interestCents)),
             textItem(bank::MoneyUtils::formatCents(day.reserveCents))});
    }
    resizeReserveTableColumns(ui->reserveForecastTableView);
    ui->threeDayReserveTotalLabel->setText(
        QStringLiteral("三日预计备款总计：%1")
            .arg(bank::MoneyUtils::formatCents(result.totalReserveCents)));
    ui->reserveForecastMessageLabel->setText(
        QStringLiteral("统计范围为基准日期后的明天、后天和大后天。"));
}

void MainWindow::showAuditLog()
{
    if (!bankService_ || bankService_->currentEmployeeId().isEmpty()) {
        return;
    }
    ui->auditEmployeeLabel->setText(
        QStringLiteral("营业员：%1").arg(bankService_->currentEmployeeId()));
    ui->mainStackedWidget->setCurrentWidget(ui->auditLogPage);
    refreshAuditLog();
}

void MainWindow::refreshAuditLog()
{
    // currentData 取得稳定动作码筛选；表格再把动作与失败原因映射为简短中文。
    const QString action = ui->auditActionComboBox->currentData().toString();
    const bank::AuditQueryResult result = bankService_->currentEmployeeAudit(action);
    auditModel_->removeRows(0, auditModel_->rowCount());
    if (!result.status.success) {
        showServiceResult(result.status, ui->auditLogMessageLabel);
        return;
    }
    for (const bank::AuditRecord &record : result.records) {
        auditModel_->appendRow(
            {textItem(dateTimeText(record.dateTime())),
             textItem(record.employeeId()),
             textItem(record.accountNumber().isEmpty() ? QStringLiteral("—")
                                                        : record.accountNumber()),
             textItem(auditActionText(record.action())),
             textItem(bank::MoneyUtils::formatCents(record.principalAmountCents())),
             textItem(bank::MoneyUtils::formatCents(record.interestAmountCents())),
             textItem(auditResultText(record.result())),
             textItem(auditFailureReasonText(record.reasonCode()))});
    }
    resizeAuditTableColumns(ui->auditLogTableView);
    ui->auditLogMessageLabel->setText(
        QStringLiteral("当前营业员共有 %1 条匹配记录。").arg(result.records.size()));
}

#include "mainwindow.h"

#include "persistence/filemanager.h"
#include "ui/depositdialog.h"
#include "ui/passworddialog.h"
#include "ui/profiledialog.h"
#include "ui/unfreezedialog.h"
#include "ui/withdrawdialog.h"
#include "ui_mainwindow.h"
#include "utils/moneyutils.h"

#include <QApplication>
#include <QDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>

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

void prepareTable(QTableView *tableView)
{
    tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tableView->horizontalHeader()->setStretchLastSection(true);
    tableView->verticalHeader()->setVisible(false);
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
    ui->auditActionComboBox->addItem(QStringLiteral("全部动作"), QString());
    ui->auditActionComboBox->addItem(QStringLiteral("储户登录"), QStringLiteral("LOGIN"));
    ui->auditActionComboBox->addItem(QStringLiteral("开户"), QStringLiteral("OPEN_ACCOUNT"));
    ui->auditActionComboBox->addItem(QStringLiteral("新增存款"), QStringLiteral("DEPOSIT"));
    ui->auditActionComboBox->addItem(QStringLiteral("提前支取"),
                                     QStringLiteral("EARLY_WITHDRAW"));
    ui->auditActionComboBox->addItem(QStringLiteral("到期支取"),
                                     QStringLiteral("MATURED_WITHDRAW"));
    ui->auditActionComboBox->addItem(QStringLiteral("修改资料"),
                                     QStringLiteral("UPDATE_PROFILE"));
    ui->auditActionComboBox->addItem(QStringLiteral("修改密码"),
                                     QStringLiteral("CHANGE_PASSWORD"));
    ui->auditActionComboBox->addItem(QStringLiteral("挂失"), QStringLiteral("REPORT_LOSS"));
    ui->auditActionComboBox->addItem(QStringLiteral("解除挂失"),
                                     QStringLiteral("UNFREEZE_ACCOUNT"));
    ui->auditActionComboBox->addItem(QStringLiteral("核心文件警告"),
                                     QStringLiteral("CORE_DATA_SAVE"));

    initializeService();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupConnections()
{
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
            [this] { refreshAccountCenter(); });
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
         QStringLiteral("动作码"),
         QStringLiteral("本金"),
         QStringLiteral("利息"),
         QStringLiteral("结果"),
         QStringLiteral("原因码")});

    ui->depositsTableView->setModel(depositsModel_);
    ui->transactionsTableView->setModel(transactionsModel_);
    ui->allDepositorsTableView->setModel(depositorsModel_);
    ui->reserveForecastTableView->setModel(reserveModel_);
    ui->auditLogTableView->setModel(auditModel_);
    connect(ui->depositsTableView->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            [this] { updateAccountActionState(); });
    prepareTable(ui->depositsTableView);
    prepareTable(ui->transactionsTableView);
    prepareTable(ui->allDepositorsTableView);
    prepareTable(ui->reserveForecastTableView);
    prepareTable(ui->auditLogTableView);
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
    depositsModel_->removeRows(0, depositsModel_->rowCount());
    transactionsModel_->removeRows(0, transactionsModel_->rowCount());
    updateSessionDisplay();
    ui->employeeLoginMessageLabel->setText(result.message);
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
    depositsModel_->removeRows(0, depositsModel_->rowCount());
    transactionsModel_->removeRows(0, transactionsModel_->rowCount());
    showServiceResult(result, ui->workspaceMessageLabel);
    showWorkspace();
}

void MainWindow::refreshAccountCenter()
{
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
        row.first()->setData(deposit.depositId(), Qt::UserRole);
        row.first()->setData(deposit.remainingPrincipalCents(), Qt::UserRole + 1);
        depositsModel_->appendRow(row);
    }

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
    ui->depositsTableView->clearSelection();
    updateSessionDisplay();
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

void MainWindow::createFixedDeposit()
{
    QDate startDate = bankService_->businessDate();
    if (!startDate.isValid()) {
        startDate = QDate::currentDate();
    }
    DepositDialog dialog(startDate, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bank::DepositResult result = bankService_->addFixedDeposit(
        dialog.principalCents(), dialog.selectedTerm());
    refreshAccountCenter();
    showServiceResult(result.status, ui->accountMessageLabel);
}

void MainWindow::withdrawSelectedDeposit()
{
    const QString depositId = selectedDepositId();
    if (depositId.isEmpty()) {
        ui->accountMessageLabel->setText(QStringLiteral("请先选中一笔可支取的存款。"));
        return;
    }
    WithdrawDialog dialog(bankService_.get(), depositId, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bank::WithdrawalResult result = bankService_->withdraw(
        depositId, dialog.principalCents());
    refreshAccountCenter();
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
    const bank::ServiceResult result = bankService_->updateProfile(dialog.name(),
                                                                   dialog.address());
    refreshAccountCenter();
    showServiceResult(result, ui->accountMessageLabel);
}

void MainWindow::changePassword()
{
    PasswordDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bank::ServiceResult result = bankService_->changePassword(
        dialog.currentPassword(), dialog.newPassword(), dialog.passwordConfirmation());
    refreshAccountCenter();
    showServiceResult(result, ui->accountMessageLabel);
}

void MainWindow::reportLoss()
{
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("确认挂失"),
        QStringLiteral("挂失后将禁止存款、支取和资料修改，确定继续吗？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    const bank::ServiceResult result = bankService_->reportLoss();
    refreshAccountCenter();
    showServiceResult(result, ui->accountMessageLabel);
}

void MainWindow::unfreezeAccount()
{
    UnfreezeDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bank::ServiceResult result = bankService_->unfreezeAccount(dialog.password());
    refreshAccountCenter();
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
             textItem(record.action()),
             textItem(bank::MoneyUtils::formatCents(record.principalAmountCents())),
             textItem(bank::MoneyUtils::formatCents(record.interestAmountCents())),
             textItem(auditResultText(record.result())),
             textItem(record.reasonCode())});
    }
    ui->auditLogMessageLabel->setText(
        QStringLiteral("当前营业员共有 %1 条匹配记录。").arg(result.records.size()));
}

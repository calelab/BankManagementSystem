#ifndef DEPOSITDIALOG_H
#define DEPOSITDIALOG_H

#include "models/banktypes.h"

#include <QDate>
#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class DepositDialog;
}
QT_END_NAMESPACE

// DepositDialog 只采集并初步校验存款输入，正式业务仍由 MainWindow 调用 BankService。
class DepositDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DepositDialog(const QDate &startDate, QWidget *parent = nullptr);
    ~DepositDialog() override;

    qint64 principalCents() const;
    bank::DepositTerm selectedTerm() const;

private:
    void updateTermSummary();
    void validateAndAccept();

    Ui::DepositDialog *ui;
    QDate startDate_;
    qint64 principalCents_ = 0;
};

#endif // DEPOSITDIALOG_H

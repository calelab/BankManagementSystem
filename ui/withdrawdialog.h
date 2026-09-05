// 支取对话框声明：基于选定存款预览本金、利息、类型和实际支付额。
#ifndef WITHDRAWDIALOG_H
#define WITHDRAWDIALOG_H

#include <QDialog>
#include <QString>

namespace bank {
class BankService;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class WithdrawDialog;
}
QT_END_NAMESPACE

// WithdrawDialog 通过 BankService 预览支取结果，只在用户确认后把本金交回主窗口提交。
class WithdrawDialog : public QDialog
{
    Q_OBJECT

public:
    WithdrawDialog(bank::BankService *service,
                   QString depositId,
                   QWidget *parent = nullptr);
    ~WithdrawDialog() override;

    qint64 principalCents() const;

private:
    void refreshPreview();
    void confirmPreview();

    Ui::WithdrawDialog *ui;
    bank::BankService *service_ = nullptr;
    QString depositId_;
    qint64 principalCents_ = 0;
    bool previewValid_ = false;
};

#endif // WITHDRAWDIALOG_H

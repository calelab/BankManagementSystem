#ifndef UNFREEZEDIALOG_H
#define UNFREEZEDIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class UnfreezeDialog;
}
QT_END_NAMESPACE

// UnfreezeDialog 仅收集解除挂失所需的当前密码，密码不会写入界面状态或日志。
class UnfreezeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UnfreezeDialog(QWidget *parent = nullptr);
    ~UnfreezeDialog() override;

    QString password() const;

private:
    void validateAndAccept();

    Ui::UnfreezeDialog *ui;
};

#endif // UNFREEZEDIALOG_H

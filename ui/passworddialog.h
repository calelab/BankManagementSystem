#ifndef PASSWORDDIALOG_H
#define PASSWORDDIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class PasswordDialog;
}
QT_END_NAMESPACE

// PasswordDialog 收集当前密码、新密码和确认密码，不读取或展示任何派生凭据。
class PasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PasswordDialog(QWidget *parent = nullptr);
    ~PasswordDialog() override;

    QString currentPassword() const;
    QString newPassword() const;
    QString passwordConfirmation() const;

private:
    void validateAndAccept();

    Ui::PasswordDialog *ui;
};

#endif // PASSWORDDIALOG_H

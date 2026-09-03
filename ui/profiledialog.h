#ifndef PROFILEDIALOG_H
#define PROFILEDIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class ProfileDialog;
}
QT_END_NAMESPACE

// ProfileDialog 只允许编辑业务规定可修改的姓名和地址字段。
class ProfileDialog : public QDialog
{
    Q_OBJECT

public:
    ProfileDialog(const QString &name,
                  const QString &address,
                  QWidget *parent = nullptr);
    ~ProfileDialog() override;

    QString name() const;
    QString address() const;

private:
    void validateAndAccept();

    Ui::ProfileDialog *ui;
};

#endif // PROFILEDIALOG_H

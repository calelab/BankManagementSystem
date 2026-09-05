// 营业员文件管理声明：维护独立工号清单并严格校验文件内容。
#ifndef EMPLOYEEFILEMANAGER_H
#define EMPLOYEEFILEMANAGER_H

#include <QSet>
#include <QString>

namespace bank::persistence {

struct EmployeeLoadResult {
    bool success = false;
    QSet<QString> employeeIds;
    bool createdInitialFile = false;
    QString errorMessage;
};

// EmployeeFileManager 管理独立的 employees.dat，只负责营业员工号文件的创建和校验。
class EmployeeFileManager
{
public:
    explicit EmployeeFileManager(QString dataDirectory);

    QString filePath() const;

    // 首次运行创建 E01 至 E10；已有文件含非法内容时报告错误且不重置。
    EmployeeLoadResult loadOrCreate() const;

    static bool isValidEmployeeId(const QString &employeeId);

private:
    bool ensureDataDirectory(QString *errorMessage) const;
    bool createInitialFile(QString *errorMessage) const;

    QString dataDirectory_;
};

} // namespace bank::persistence

#endif // EMPLOYEEFILEMANAGER_H

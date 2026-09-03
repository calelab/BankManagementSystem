#include "persistence/employeefilemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <utility>

namespace bank::persistence {

EmployeeFileManager::EmployeeFileManager(QString dataDirectory)
    : dataDirectory_(std::move(dataDirectory))
{
}

QString EmployeeFileManager::filePath() const
{
    return QDir(dataDirectory_).filePath(QStringLiteral("employees.dat"));
}

EmployeeLoadResult EmployeeFileManager::loadOrCreate() const
{
    EmployeeLoadResult result;
    if (!ensureDataDirectory(&result.errorMessage)) {
        return result;
    }

    if (!QFileInfo::exists(filePath())) {
        if (!createInitialFile(&result.errorMessage)) {
            return result;
        }
        result.createdInitialFile = true;
    }

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("无法读取营业员文件：%1").arg(file.errorString());
        return result;
    }

    qsizetype lineNumber = 0;
    while (!file.atEnd()) {
        ++lineNumber;
        const QString employeeId = QString::fromUtf8(file.readLine()).trimmed();
        if (employeeId.isEmpty()) {
            continue;
        }
        if (!isValidEmployeeId(employeeId)) {
            result.errorMessage = QStringLiteral("营业员文件第 %1 行工号无效").arg(lineNumber);
            return result;
        }
        result.employeeIds.insert(employeeId);
    }
    if (file.error() != QFileDevice::NoError) {
        result.errorMessage = QStringLiteral("读取营业员文件失败：%1").arg(file.errorString());
        return result;
    }
    if (result.employeeIds.isEmpty()) {
        result.errorMessage = QStringLiteral("营业员文件中没有有效工号");
        return result;
    }

    result.success = true;
    return result;
}

bool EmployeeFileManager::isValidEmployeeId(const QString &employeeId)
{
    static const QRegularExpression pattern(QStringLiteral("^E(?:0[1-9]|[1-9][0-9])$"));
    return pattern.match(employeeId).hasMatch();
}

bool EmployeeFileManager::ensureDataDirectory(QString *errorMessage) const
{
    if (dataDirectory_.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("数据目录不能为空");
        }
        return false;
    }
    const QFileInfo info(dataDirectory_);
    if (info.exists() && !info.isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("数据目录路径不是文件夹");
        }
        return false;
    }
    if (!info.exists() && !QDir().mkpath(dataDirectory_)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建数据目录");
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool EmployeeFileManager::createInitialFile(QString *errorMessage) const
{
    QByteArray contents;
    for (int number = 1; number <= 10; ++number) {
        contents += QStringLiteral("E%1\n").arg(number, 2, 10, QLatin1Char('0')).toUtf8();
    }

    QSaveFile file(filePath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建营业员文件：%1").arg(file.errorString());
        }
        return false;
    }
    if (file.write(contents) != contents.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("写入营业员文件失败：%1").arg(file.errorString());
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("保存营业员文件失败：%1").arg(file.errorString());
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace bank::persistence

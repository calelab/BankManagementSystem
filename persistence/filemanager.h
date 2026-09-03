#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "models/bankstate.h"
#include "persistence/datacodec.h"

#include <QString>

#include <memory>

namespace bank::persistence {

struct FileSaveResult {
    bool success = false;
    QString errorMessage;
};

struct FileLoadResult {
    bool success = false;
    BankState state;
    bool initializedEmptyState = false;
    QString errorMessage;
};

// FileManager 负责数据目录、编码边界和原子文件替换，不包含开户或支取等业务决策。
class FileManager
{
public:
    explicit FileManager(
        QString dataDirectory = defaultDataDirectory(),
        std::shared_ptr<const DataCodec> codec = nullptr);

    static QString defaultDataDirectory();

    const QString &dataDirectory() const;
    QString dataFilePath() const;
    QString storageModeDisplayName() const;

    // 文件不存在时返回新的空 BankState；损坏数据绝不会被静默覆盖。
    FileLoadResult load() const;

    // 校验、序列化和编码均成功后才用 QSaveFile 原子替换目标文件。
    FileSaveResult save(const BankState &state) const;

private:
    bool ensureDataDirectory(QString *errorMessage) const;

    QString dataDirectory_;
    std::shared_ptr<const DataCodec> codec_;
};

} // namespace bank::persistence

#endif // FILEMANAGER_H

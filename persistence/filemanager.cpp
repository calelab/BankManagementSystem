// 核心数据文件管理实现：通过 QSaveFile 保证失败写入不会破坏旧数据。
#include "persistence/filemanager.h"

#include "persistence/bankstatejsonserializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <utility>

namespace bank::persistence {

FileManager::FileManager(QString dataDirectory, std::shared_ptr<const DataCodec> codec)
    : dataDirectory_(std::move(dataDirectory))
    , codec_(std::move(codec))
{
    if (!codec_) {
        codec_ = createDefaultDataCodec(dataDirectory_);
    }
}

QString FileManager::defaultDataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

const QString &FileManager::dataDirectory() const
{
    return dataDirectory_;
}

QString FileManager::dataFilePath() const
{
    return QDir(dataDirectory_).filePath(codec_->fileName());
}

QString FileManager::storageModeDisplayName() const
{
    return codec_->displayName();
}

const std::shared_ptr<const DataCodec> &FileManager::codec() const
{
    return codec_;
}

FileLoadResult FileManager::load() const
{
    FileLoadResult result;
    if (!ensureDataDirectory(&result.errorMessage)) {
        return result;
    }
    if (!codec_->initialize(&result.errorMessage)) {
        return result;
    }

    const QString path = dataFilePath();
    // 首次运行仅返回合法空状态；已有文件的读取或解码错误绝不静默重建。
    if (!QFileInfo::exists(path)) {
        result.success = true;
        result.initializedEmptyState = true;
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("无法读取核心数据文件：%1").arg(file.errorString());
        return result;
    }
    const QByteArray encodedData = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        result.errorMessage = QStringLiteral("读取核心数据文件失败：%1").arg(file.errorString());
        return result;
    }

    QByteArray plainJson;
    if (!codec_->decode(encodedData, &plainJson, &result.errorMessage)) {
        return result;
    }
    if (!BankStateJsonSerializer::deserialize(plainJson, &result.state, &result.errorMessage)) {
        return result;
    }

    result.success = true;
    return result;
}

FileSaveResult FileManager::save(const BankState &state) const
{
    FileSaveResult result;
    if (!ensureDataDirectory(&result.errorMessage)
        || !codec_->initialize(&result.errorMessage)) {
        return result;
    }
    QByteArray plainJson;
    if (!BankStateJsonSerializer::serialize(state, &plainJson, &result.errorMessage)) {
        return result;
    }

    QByteArray encodedData;
    if (!codec_->encode(plainJson, &encodedData, &result.errorMessage)) {
        return result;
    }
    // JSON 先经过当前编码器，再由 QSaveFile 写临时文件并原子替换目标。
    QSaveFile file(dataFilePath());
    // 禁止退化为直接覆盖，临时文件或原子替换失败时必须保留旧文件。
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.errorMessage = QStringLiteral("无法创建核心数据临时文件：%1").arg(file.errorString());
        return result;
    }
    if (file.write(encodedData) != encodedData.size()) {
        result.errorMessage = QStringLiteral("写入核心数据失败：%1").arg(file.errorString());
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) {
        result.errorMessage = QStringLiteral("原子替换核心数据文件失败：%1").arg(file.errorString());
        return result;
    }

    result.success = true;
    return result;
}

bool FileManager::ensureDataDirectory(QString *errorMessage) const
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

} // namespace bank::persistence

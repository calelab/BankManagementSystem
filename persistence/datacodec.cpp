#include "persistence/datacodec.h"

namespace bank::persistence {
namespace {

bool copyBytes(const QByteArray &source, QByteArray *destination, QString *errorMessage)
{
    if (!destination) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("编码输出参数不能为空");
        }
        return false;
    }
    *destination = source;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace

QString PlainJsonCodec::fileName() const
{
    return QStringLiteral("bank_data.json");
}

QString PlainJsonCodec::displayName() const
{
    return QStringLiteral("明文 JSON 兼容模式");
}

bool PlainJsonCodec::encode(const QByteArray &plainJson,
                            QByteArray *encodedData,
                            QString *errorMessage) const
{
    return copyBytes(plainJson, encodedData, errorMessage);
}

bool PlainJsonCodec::decode(const QByteArray &encodedData,
                            QByteArray *plainJson,
                            QString *errorMessage) const
{
    return copyBytes(encodedData, plainJson, errorMessage);
}

} // namespace bank::persistence

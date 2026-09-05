// 磁盘编码实现：封装加密文件格式、主密钥初始化和构建期模式选择。
#include "persistence/datacodec.h"

#include "security/securityutils.h"

#include <QDir>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace bank::persistence {
namespace {

// 20 字节固定头依次保存 Magic、版本、Nonce/Tag 长度、保留位和密文长度；
// 整个固定头作为 GCM AAD 参与认证，随后排列 Nonce、Tag 和密文。
constexpr std::array<char, 8> EncryptedFileMagic{'B', 'M', 'S', 'A', 'E', 'S', 'G', '1'};
constexpr quint8 EncryptedFileVersion = 1;
constexpr qsizetype EncryptedHeaderSize = 20;

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

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

QByteArray makeHeader(quint64 ciphertextSize)
{
    QByteArray header;
    header.reserve(EncryptedHeaderSize);
    header.append(EncryptedFileMagic.data(), EncryptedFileMagic.size());
    header.append(static_cast<char>(EncryptedFileVersion));
    header.append(static_cast<char>(security::SecurityUtils::AesNonceSize));
    header.append(static_cast<char>(security::SecurityUtils::AesTagSize));
    header.append('\0');
    std::array<unsigned char, sizeof(quint64)> lengthBytes{};
    qToBigEndian(ciphertextSize, lengthBytes.data());
    header.append(reinterpret_cast<const char *>(lengthBytes.data()), lengthBytes.size());
    return header;
}

} // namespace

bool DataCodec::initialize(QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QString PlainJsonCodec::fileName() const
{
    return QStringLiteral("bank_data.json");
}

QString PlainJsonCodec::auditFileSuffix() const
{
    return QStringLiteral(".audit.json");
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

AesGcmCodec::AesGcmCodec(QString dataDirectory)
    : dataDirectory_(std::move(dataDirectory))
{
}

bool AesGcmCodec::initialize(QString *errorMessage) const
{
    // 同一编码器惰性缓存一次主密钥，避免每条审计记录重复读取敏感文件。
    if (masterKey_.size() == security::SecurityUtils::AesKeySize) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    return security::SecurityUtils::loadOrCreateMasterKey(
        dataDirectory_, encryptedArtifactsExist(), &masterKey_, errorMessage);
}

QString AesGcmCodec::fileName() const
{
    return QStringLiteral("bank_data.enc");
}

QString AesGcmCodec::auditFileSuffix() const
{
    return QStringLiteral(".audit.enc");
}

QString AesGcmCodec::displayName() const
{
    return QStringLiteral("AES-256-GCM 加密模式");
}

bool AesGcmCodec::encode(const QByteArray &plainJson,
                         QByteArray *encodedData,
                         QString *errorMessage) const
{
    if (!encodedData) {
        return fail(errorMessage, QStringLiteral("加密输出参数不能为空"));
    }
    if (!initialize(errorMessage)) {
        return false;
    }

    // 固定头作为 AAD 认证，使版本、长度以及 Nonce/Tag 参数也无法被静默篡改。
    const QByteArray header = makeHeader(static_cast<quint64>(plainJson.size()));
    security::AesGcmPayload payload;
    if (!security::SecurityUtils::encryptAes256Gcm(
            plainJson, masterKey_, header, &payload, errorMessage)) {
        return false;
    }

    QByteArray result;
    result.reserve(header.size() + payload.nonce.size()
                   + payload.authenticationTag.size() + payload.ciphertext.size());
    result.append(header);
    result.append(payload.nonce);
    result.append(payload.authenticationTag);
    result.append(payload.ciphertext);
    *encodedData = std::move(result);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool AesGcmCodec::decode(const QByteArray &encodedData,
                         QByteArray *plainJson,
                         QString *errorMessage) const
{
    if (!plainJson) {
        return fail(errorMessage, QStringLiteral("解密输出参数不能为空"));
    }
    if (!initialize(errorMessage)) {
        return false;
    }

    // 先验证封装边界再调用 GCM 认证，畸形长度不会进入底层密码接口。
    const qsizetype fixedSize = EncryptedHeaderSize
                                + security::SecurityUtils::AesNonceSize
                                + security::SecurityUtils::AesTagSize;
    if (encodedData.size() < fixedSize
        || !std::equal(EncryptedFileMagic.cbegin(), EncryptedFileMagic.cend(),
                       encodedData.cbegin())) {
        return fail(errorMessage, QStringLiteral("加密数据文件 Magic 无效或文件不完整"));
    }
    const auto byteAt = [&encodedData](qsizetype offset) {
        return static_cast<quint8>(encodedData.at(offset));
    };
    if (byteAt(8) != EncryptedFileVersion) {
        return fail(errorMessage, QStringLiteral("加密数据文件版本不受支持"));
    }
    if (byteAt(9) != security::SecurityUtils::AesNonceSize
        || byteAt(10) != security::SecurityUtils::AesTagSize || byteAt(11) != 0) {
        return fail(errorMessage, QStringLiteral("加密数据文件参数无效"));
    }

    const quint64 ciphertextSize = qFromBigEndian<quint64>(
        reinterpret_cast<const unsigned char *>(encodedData.constData() + 12));
    const quint64 actualCiphertextSize = static_cast<quint64>(encodedData.size() - fixedSize);
    if (ciphertextSize != actualCiphertextSize
        || ciphertextSize > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
        return fail(errorMessage, QStringLiteral("加密数据文件长度无效"));
    }

    const QByteArray header = encodedData.first(EncryptedHeaderSize);
    security::AesGcmPayload payload;
    payload.nonce = encodedData.mid(EncryptedHeaderSize,
                                    security::SecurityUtils::AesNonceSize);
    payload.authenticationTag = encodedData.mid(
        EncryptedHeaderSize + security::SecurityUtils::AesNonceSize,
        security::SecurityUtils::AesTagSize);
    payload.ciphertext = encodedData.sliced(fixedSize,
                                            static_cast<qsizetype>(ciphertextSize));
    return security::SecurityUtils::decryptAes256Gcm(
        payload, masterKey_, header, plainJson, errorMessage);
}

bool AesGcmCodec::encryptedArtifactsExist() const
{
    // 核心密文或任一加密审计存在时都禁止重建主密钥，防止永久丢失旧数据。
    if (QFileInfo::exists(QDir(dataDirectory_).filePath(fileName()))) {
        return true;
    }
    const QDir auditDirectory(QDir(dataDirectory_).filePath(QStringLiteral("audit")));
    return auditDirectory.exists()
           && !auditDirectory.entryList(
                   {QStringLiteral("*.audit.enc")}, QDir::Files | QDir::NoDotAndDotDot)
                   .isEmpty();
}

std::shared_ptr<const DataCodec> createDefaultDataCodec(const QString &dataDirectory)
{
    // 加密能力由构建期决定；两种模式使用不同文件名，绝不互相覆盖。
#ifdef BANK_HAS_OPENSSL
    return std::make_shared<AesGcmCodec>(dataDirectory);
#else
    Q_UNUSED(dataDirectory)
    return std::make_shared<PlainJsonCodec>();
#endif
}

bool defaultEncryptionEnabled()
{
#ifdef BANK_HAS_OPENSSL
    return true;
#else
    return false;
#endif
}

QString defaultStorageModeDisplayName()
{
    return defaultEncryptionEnabled() ? QStringLiteral("AES-256-GCM 加密模式")
                                      : QStringLiteral("明文 JSON 兼容模式");
}

} // namespace bank::persistence

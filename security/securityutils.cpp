#include "security/securityutils.h"

#include "models/depositor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSaveFile>

#include <climits>
#include <memory>

#ifdef BANK_HAS_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace bank::security {
namespace {

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

#ifdef BANK_HAS_OPENSSL
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

bool inputFitsEvp(const QByteArray &bytes)
{
    return bytes.size() <= INT_MAX;
}
#endif

} // namespace

std::optional<PasswordCredentials> SecurityUtils::createPasswordCredentials(
    const QString &password,
    QString *errorMessage)
{
    if (password.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("密码不能为空");
        }
        return std::nullopt;
    }

    PasswordCredentials credentials;
    credentials.salt = randomBytes(PasswordSaltSize);
    credentials.iterations = PasswordKdfIterations;
    credentials.algorithm = Depositor::supportedPasswordKdfAlgorithm();
    credentials.hash = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256,
        password.toUtf8(),
        credentials.salt,
        credentials.iterations,
        PasswordHashSize);
    if (credentials.salt.size() != PasswordSaltSize
        || credentials.hash.size() != PasswordHashSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("密码派生失败");
        }
        return std::nullopt;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return credentials;
}

bool SecurityUtils::verifyPassword(const QString &password, const Depositor &depositor)
{
    if (password.isEmpty()
        || depositor.passwordKdfAlgorithm() != Depositor::supportedPasswordKdfAlgorithm()
        || depositor.passwordSalt().size() != PasswordSaltSize
        || depositor.passwordHash().size() != PasswordHashSize
        || depositor.passwordKdfIterations() != PasswordKdfIterations) {
        return false;
    }
    const QByteArray candidate = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256,
        password.toUtf8(),
        depositor.passwordSalt(),
        depositor.passwordKdfIterations(),
        PasswordHashSize);
    return constantTimeEquals(candidate, depositor.passwordHash());
}

bool SecurityUtils::aes256GcmAvailable()
{
#ifdef BANK_HAS_OPENSSL
    return true;
#else
    return false;
#endif
}

bool SecurityUtils::encryptAes256Gcm(const QByteArray &plainText,
                                     const QByteArray &key,
                                     const QByteArray &additionalData,
                                     AesGcmPayload *payload,
                                     QString *errorMessage)
{
#ifdef BANK_HAS_OPENSSL
    if (!payload || key.size() != AesKeySize || !inputFitsEvp(plainText)
        || !inputFitsEvp(additionalData)) {
        return fail(errorMessage, QStringLiteral("AES-GCM 加密参数无效"));
    }

    AesGcmPayload candidate;
    candidate.nonce = randomBytes(AesNonceSize);
    if (candidate.nonce.size() != AesNonceSize) {
        return fail(errorMessage, QStringLiteral("无法生成 AES-GCM Nonce"));
    }

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context
        || EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                               candidate.nonce.size(), nullptr) != 1
        || EVP_EncryptInit_ex(
               context.get(), nullptr, nullptr,
               reinterpret_cast<const unsigned char *>(key.constData()),
               reinterpret_cast<const unsigned char *>(candidate.nonce.constData())) != 1) {
        return fail(errorMessage, QStringLiteral("AES-256-GCM 加密初始化失败"));
    }

    int written = 0;
    if (!additionalData.isEmpty()
        && EVP_EncryptUpdate(
               context.get(), nullptr, &written,
               reinterpret_cast<const unsigned char *>(additionalData.constData()),
               static_cast<int>(additionalData.size())) != 1) {
        return fail(errorMessage, QStringLiteral("AES-GCM 文件头认证失败"));
    }

    candidate.ciphertext.resize(plainText.size() + EVP_MAX_BLOCK_LENGTH);
    written = 0;
    int ciphertextSize = 0;
    if (!plainText.isEmpty()
        && EVP_EncryptUpdate(
               context.get(),
               reinterpret_cast<unsigned char *>(candidate.ciphertext.data()),
               &written,
               reinterpret_cast<const unsigned char *>(plainText.constData()),
               static_cast<int>(plainText.size())) != 1) {
        return fail(errorMessage, QStringLiteral("AES-256-GCM 加密失败"));
    }
    ciphertextSize += written;
    if (EVP_EncryptFinal_ex(
            context.get(),
            reinterpret_cast<unsigned char *>(candidate.ciphertext.data()) + ciphertextSize,
            &written) != 1) {
        return fail(errorMessage, QStringLiteral("AES-256-GCM 加密结束失败"));
    }
    ciphertextSize += written;
    candidate.ciphertext.resize(ciphertextSize);

    candidate.authenticationTag.resize(AesTagSize);
    if (EVP_CIPHER_CTX_ctrl(
            context.get(), EVP_CTRL_GCM_GET_TAG, AesTagSize,
            candidate.authenticationTag.data()) != 1) {
        return fail(errorMessage, QStringLiteral("无法取得 AES-GCM 认证标签"));
    }

    *payload = std::move(candidate);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
#else
    Q_UNUSED(plainText)
    Q_UNUSED(key)
    Q_UNUSED(additionalData)
    Q_UNUSED(payload)
    return fail(errorMessage, QStringLiteral("当前构建未启用 OpenSSL AES-256-GCM"));
#endif
}

bool SecurityUtils::decryptAes256Gcm(const AesGcmPayload &payload,
                                     const QByteArray &key,
                                     const QByteArray &additionalData,
                                     QByteArray *plainText,
                                     QString *errorMessage)
{
#ifdef BANK_HAS_OPENSSL
    if (!plainText || key.size() != AesKeySize
        || payload.nonce.size() != AesNonceSize
        || payload.authenticationTag.size() != AesTagSize
        || !inputFitsEvp(payload.ciphertext) || !inputFitsEvp(additionalData)) {
        return fail(errorMessage, QStringLiteral("AES-GCM 解密参数无效"));
    }

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context
        || EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                               payload.nonce.size(), nullptr) != 1
        || EVP_DecryptInit_ex(
               context.get(), nullptr, nullptr,
               reinterpret_cast<const unsigned char *>(key.constData()),
               reinterpret_cast<const unsigned char *>(payload.nonce.constData())) != 1) {
        return fail(errorMessage, QStringLiteral("AES-256-GCM 解密初始化失败"));
    }

    int written = 0;
    if (!additionalData.isEmpty()
        && EVP_DecryptUpdate(
               context.get(), nullptr, &written,
               reinterpret_cast<const unsigned char *>(additionalData.constData()),
               static_cast<int>(additionalData.size())) != 1) {
        return fail(errorMessage, QStringLiteral("AES-GCM 文件头认证失败"));
    }

    QByteArray candidate(payload.ciphertext.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    written = 0;
    int plainTextSize = 0;
    if (!payload.ciphertext.isEmpty()
        && EVP_DecryptUpdate(
               context.get(), reinterpret_cast<unsigned char *>(candidate.data()), &written,
               reinterpret_cast<const unsigned char *>(payload.ciphertext.constData()),
               static_cast<int>(payload.ciphertext.size())) != 1) {
        return fail(errorMessage, QStringLiteral("AES-256-GCM 解密失败"));
    }
    plainTextSize += written;
    if (EVP_CIPHER_CTX_ctrl(
            context.get(), EVP_CTRL_GCM_SET_TAG, AesTagSize,
            const_cast<char *>(payload.authenticationTag.constData())) != 1) {
        return fail(errorMessage, QStringLiteral("AES-GCM 认证标签设置失败"));
    }

    const int finalResult = EVP_DecryptFinal_ex(
        context.get(), reinterpret_cast<unsigned char *>(candidate.data()) + plainTextSize,
        &written);
    if (finalResult != 1) {
        return fail(errorMessage,
                    QStringLiteral("AES-GCM 认证失败：数据可能损坏、密钥不匹配或已被篡改"));
    }
    plainTextSize += written;
    candidate.resize(plainTextSize);
    *plainText = std::move(candidate);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
#else
    Q_UNUSED(payload)
    Q_UNUSED(key)
    Q_UNUSED(additionalData)
    Q_UNUSED(plainText)
    return fail(errorMessage, QStringLiteral("当前构建未启用 OpenSSL AES-256-GCM"));
#endif
}

QString SecurityUtils::masterKeyPath(const QString &dataDirectory)
{
    return QDir(dataDirectory).filePath(QStringLiteral("config/master.key"));
}

bool SecurityUtils::loadOrCreateMasterKey(const QString &dataDirectory,
                                          bool encryptedArtifactsExist,
                                          QByteArray *key,
                                          QString *errorMessage)
{
    if (!key || dataDirectory.trimmed().isEmpty()) {
        return fail(errorMessage, QStringLiteral("主密钥参数无效"));
    }
    const QString path = masterKeyPath(dataDirectory);
    const QFileInfo keyInfo(path);
    if (keyInfo.exists()) {
        if (!keyInfo.isFile()) {
            return fail(errorMessage, QStringLiteral("主密钥路径不是普通文件"));
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return fail(errorMessage, QStringLiteral("无法读取主密钥文件"));
        }
        const QByteArray loadedKey = file.readAll();
        if (file.error() != QFileDevice::NoError || loadedKey.size() != AesKeySize) {
            return fail(errorMessage, QStringLiteral("主密钥文件损坏或长度无效"));
        }
        *key = loadedKey;
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }

    if (encryptedArtifactsExist) {
        return fail(errorMessage,
                    QStringLiteral("主密钥缺失，无法解密现有加密数据；不会生成替代密钥"));
    }
    if (!aes256GcmAvailable()) {
        return fail(errorMessage, QStringLiteral("当前构建未启用 OpenSSL，无法创建主密钥"));
    }

    const QString configDirectory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(configDirectory)) {
        return fail(errorMessage, QStringLiteral("无法创建主密钥目录"));
    }
    const QByteArray generatedKey = randomBytes(AesKeySize);
    if (generatedKey.size() != AesKeySize) {
        return fail(errorMessage, QStringLiteral("无法生成 AES 主密钥"));
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(errorMessage, QStringLiteral("无法创建主密钥临时文件"));
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(generatedKey) != generatedKey.size()) {
        file.cancelWriting();
        return fail(errorMessage, QStringLiteral("写入主密钥失败"));
    }
    if (!file.commit()) {
        return fail(errorMessage, QStringLiteral("原子保存主密钥失败"));
    }
#ifdef Q_OS_UNIX
    // 权限设置属于尽力加固；密钥内容和路径不会写入日志。
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    *key = generatedKey;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QByteArray SecurityUtils::randomBytes(int size)
{
    if (size <= 0) {
        return {};
    }

    QByteArray bytes(size, '\0');
#ifdef BANK_HAS_OPENSSL
    if (RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), size) != 1) {
        return {};
    }
#else
    int offset = 0;
    while (offset < size) {
        const quint32 randomValue = QRandomGenerator::system()->generate();
        for (int byteIndex = 0; byteIndex < 4 && offset < size; ++byteIndex, ++offset) {
            bytes[offset] = static_cast<char>((randomValue >> (byteIndex * 8)) & 0xffU);
        }
    }
#endif
    return bytes;
}

bool SecurityUtils::constantTimeEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index) {
        difference |= static_cast<unsigned char>(left.at(index))
                      ^ static_cast<unsigned char>(right.at(index));
    }
    return difference == 0;
}

} // namespace bank::security

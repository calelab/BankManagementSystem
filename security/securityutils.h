#ifndef SECURITYUTILS_H
#define SECURITYUTILS_H

#include <QByteArray>
#include <QString>

#include <optional>

namespace bank {
class Depositor;
}

namespace bank::security {

struct PasswordCredentials {
    QByteArray salt;
    QByteArray hash;
    int iterations = 0;
    QString algorithm;
};

struct AesGcmPayload {
    QByteArray nonce;
    QByteArray authenticationTag;
    QByteArray ciphertext;
};

// SecurityUtils 只封装成熟密码库能力，不保存或输出密码、派生值或 AES 主密钥。
class SecurityUtils
{
public:
    static constexpr int PasswordSaltSize = 16;
    static constexpr int PasswordHashSize = 32;
    static constexpr int PasswordKdfIterations = 210000;
    static constexpr int AesKeySize = 32;
    static constexpr int AesNonceSize = 12;
    static constexpr int AesTagSize = 16;

    // 为新密码生成独立随机 Salt，并派生 PBKDF2-HMAC-SHA256 结果。
    static std::optional<PasswordCredentials> createPasswordCredentials(
        const QString &password,
        QString *errorMessage = nullptr);

    // 使用储户保存的算法、Salt 和迭代次数验证密码，比较过程不提前退出。
    static bool verifyPassword(const QString &password, const Depositor &depositor);

    static bool aes256GcmAvailable();

    // 使用随机 Nonce 加密并返回认证标签；additionalData 会被认证但不写入密文。
    static bool encryptAes256Gcm(const QByteArray &plainText,
                                 const QByteArray &key,
                                 const QByteArray &additionalData,
                                 AesGcmPayload *payload,
                                 QString *errorMessage = nullptr);

    // 认证失败不返回任何部分明文，避免损坏或错误密钥的数据继续进入 JSON 层。
    static bool decryptAes256Gcm(const AesGcmPayload &payload,
                                 const QByteArray &key,
                                 const QByteArray &additionalData,
                                 QByteArray *plainText,
                                 QString *errorMessage = nullptr);

    static QString masterKeyPath(const QString &dataDirectory);

    // 已有密文时绝不生成替代密钥；只有全新数据目录才创建 32 字节主密钥。
    static bool loadOrCreateMasterKey(const QString &dataDirectory,
                                      bool encryptedArtifactsExist,
                                      QByteArray *key,
                                      QString *errorMessage = nullptr);

private:
    static QByteArray randomBytes(int size);
    static bool constantTimeEquals(const QByteArray &left, const QByteArray &right);
};

} // namespace bank::security

#endif // SECURITYUTILS_H

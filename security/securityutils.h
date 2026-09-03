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

// SecurityUtils 只封装可靠随机源和 Qt 的 PBKDF2，实现中不保存或输出明文密码。
class SecurityUtils
{
public:
    static constexpr int PasswordSaltSize = 16;
    static constexpr int PasswordHashSize = 32;
    static constexpr int PasswordKdfIterations = 210000;

    // 为新密码生成独立随机 Salt，并派生 PBKDF2-HMAC-SHA256 结果。
    static std::optional<PasswordCredentials> createPasswordCredentials(
        const QString &password,
        QString *errorMessage = nullptr);

    // 使用储户保存的算法、Salt 和迭代次数验证密码，比较过程不提前退出。
    static bool verifyPassword(const QString &password, const Depositor &depositor);

private:
    static QByteArray randomBytes(int size);
    static bool constantTimeEquals(const QByteArray &left, const QByteArray &right);
};

} // namespace bank::security

#endif // SECURITYUTILS_H

#include "security/securityutils.h"

#include "models/depositor.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>

namespace bank::security {

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

QByteArray SecurityUtils::randomBytes(int size)
{
    if (size <= 0) {
        return {};
    }

    QByteArray bytes(size, '\0');
    int offset = 0;
    while (offset < size) {
        const quint32 randomValue = QRandomGenerator::system()->generate();
        for (int byteIndex = 0; byteIndex < 4 && offset < size; ++byteIndex, ++offset) {
            bytes[offset] = static_cast<char>((randomValue >> (byteIndex * 8)) & 0xffU);
        }
    }
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

#include "utils/moneyutils.h"

#include <QLocale>
#include <QRegularExpression>

#include <limits>

namespace bank::MoneyUtils {
namespace {

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

quint64 magnitudeOf(qint64 value)
{
    // 先偏移再取负可安全处理 qint64 最小值，避免 abs(min) 溢出。
    return value < 0 ? static_cast<quint64>(-(value + 1)) + 1
                     : static_cast<quint64>(value);
}

} // namespace

std::optional<qint64> parseCents(const QString &text, QString *errorMessage)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(0|[1-9][0-9]*)(?:\\.([0-9]{1,2}))?$"));

    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        setError(errorMessage, QStringLiteral("金额必须是正数，且最多保留两位小数"));
        return std::nullopt;
    }

    bool conversionOk = false;
    const quint64 yuan = match.captured(1).toULongLong(&conversionOk);
    if (!conversionOk) {
        setError(errorMessage, QStringLiteral("金额超出可表示范围"));
        return std::nullopt;
    }

    QString fractionText = match.captured(2);
    if (fractionText.size() == 1) {
        fractionText.append(QLatin1Char('0'));
    }
    const quint64 fraction = fractionText.isEmpty() ? 0 : fractionText.toUInt();
    constexpr quint64 maximum = static_cast<quint64>(std::numeric_limits<qint64>::max());
    if (yuan > (maximum - fraction) / 100) {
        setError(errorMessage, QStringLiteral("金额超出可表示范围"));
        return std::nullopt;
    }

    const quint64 cents = yuan * 100 + fraction;
    if (cents == 0) {
        setError(errorMessage, QStringLiteral("金额必须大于零"));
        return std::nullopt;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return static_cast<qint64>(cents);
}

QString formatCents(qint64 cents)
{
    const quint64 magnitude = magnitudeOf(cents);
    const quint64 wholeYuan = magnitude / 100;
    const quint64 fractionalCents = magnitude % 100;
    const QLocale numberLocale(QLocale::English, QLocale::UnitedStates);
    const QString amount = QStringLiteral("%1.%2")
                               .arg(numberLocale.toString(wholeYuan))
                               .arg(fractionalCents, 2, 10, QLatin1Char('0'));
    return cents < 0 ? QStringLiteral("-¥%1").arg(amount)
                     : QStringLiteral("¥%1").arg(amount);
}

QString formatRateBasisPoints(int basisPoints)
{
    const bool negative = basisPoints < 0;
    const qint64 magnitude = magnitudeOf(static_cast<qint64>(basisPoints));
    const QString rate = QStringLiteral("%1.%2%")
                             .arg(magnitude / 100)
                             .arg(magnitude % 100, 2, 10, QLatin1Char('0'));
    return negative ? QStringLiteral("-%1").arg(rate) : rate;
}

} // namespace bank::MoneyUtils

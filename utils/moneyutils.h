// 金额工具声明：在用户输入、整数分和统一人民币展示之间转换。
#ifndef MONEYUTILS_H
#define MONEYUTILS_H

#include <QString>
#include <QtGlobal>

#include <optional>

namespace bank::MoneyUtils {

// 严格把“元”文本转换成“分”；只接受正数和最多两位小数。
std::optional<qint64> parseCents(const QString &text, QString *errorMessage = nullptr);

// 金额统一带人民币符号、千位分隔和两位小数，内部仍保持整数分。
QString formatCents(qint64 cents);

// 将整数基点格式化为百分比，例如 198 bp 显示为 1.98%。
QString formatRateBasisPoints(int basisPoints);

} // namespace bank::MoneyUtils

#endif // MONEYUTILS_H

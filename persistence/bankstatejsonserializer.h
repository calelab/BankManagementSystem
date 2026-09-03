#ifndef BANKSTATEJSONSERIALIZER_H
#define BANKSTATEJSONSERIALIZER_H

#include "models/bankstate.h"

#include <QByteArray>
#include <QString>

namespace bank::persistence {

// BankStateJsonSerializer 只负责领域对象与版本化 JSON 字节之间的严格转换。
class BankStateJsonSerializer
{
public:
    static bool serialize(const BankState &state,
                          QByteArray *jsonData,
                          QString *errorMessage = nullptr);

    // 解析成功前不修改输出对象；缺字段、类型错误或领域约束破坏都会失败。
    static bool deserialize(const QByteArray &jsonData,
                            BankState *state,
                            QString *errorMessage = nullptr);
};

} // namespace bank::persistence

#endif // BANKSTATEJSONSERIALIZER_H

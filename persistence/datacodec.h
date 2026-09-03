#ifndef DATACODEC_H
#define DATACODEC_H

#include <QByteArray>
#include <QString>

namespace bank::persistence {

// DataCodec 只负责 JSON 字节与磁盘字节之间的转换，使后续加密无需改动业务或序列化层。
class DataCodec
{
public:
    virtual ~DataCodec() = default;

    virtual QString fileName() const = 0;
    virtual QString auditFileSuffix() const = 0;
    virtual QString displayName() const = 0;
    virtual bool encode(const QByteArray &plainJson,
                        QByteArray *encodedData,
                        QString *errorMessage) const = 0;
    virtual bool decode(const QByteArray &encodedData,
                        QByteArray *plainJson,
                        QString *errorMessage) const = 0;
};

// 阶段 2 的兼容编码保持 JSON 原文，文件名与未来加密模式明确分离。
class PlainJsonCodec final : public DataCodec
{
public:
    QString fileName() const override;
    QString auditFileSuffix() const override;
    QString displayName() const override;
    bool encode(const QByteArray &plainJson,
                QByteArray *encodedData,
                QString *errorMessage) const override;
    bool decode(const QByteArray &encodedData,
                QByteArray *plainJson,
                QString *errorMessage) const override;
};

} // namespace bank::persistence

#endif // DATACODEC_H

// JSON 文件编码接口：保持序列化字节与磁盘文件之间的清晰边界。
#ifndef DATACODEC_H
#define DATACODEC_H

#include <QByteArray>
#include <QString>

namespace bank::persistence {

// DataCodec 负责 JSON 字节的读写编码，并支持测试注入文件编码故障。
class DataCodec
{
public:
    virtual ~DataCodec() = default;

    virtual QString fileName() const = 0;
    virtual QString auditFileSuffix() const = 0;
    virtual bool encode(const QByteArray &plainJson,
                        QByteArray *encodedData,
                        QString *errorMessage) const = 0;
    virtual bool decode(const QByteArray &encodedData,
                        QByteArray *plainJson,
                        QString *errorMessage) const = 0;
};

// PlainJsonCodec 原样保存 JSON 字节，并定义银行数据与审计日志的文件名。
class PlainJsonCodec final : public DataCodec
{
public:
    QString fileName() const override;
    QString auditFileSuffix() const override;
    bool encode(const QByteArray &plainJson,
                QByteArray *encodedData,
                QString *errorMessage) const override;
    bool decode(const QByteArray &encodedData,
                QByteArray *plainJson,
                QString *errorMessage) const override;
};

} // namespace bank::persistence

#endif // DATACODEC_H

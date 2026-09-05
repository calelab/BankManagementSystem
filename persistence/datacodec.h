// 磁盘编码边界：为明文兼容模式和 AES-256-GCM 模式提供统一接口。
#ifndef DATACODEC_H
#define DATACODEC_H

#include <QByteArray>
#include <QString>

#include <memory>

namespace bank::persistence {

// DataCodec 只负责 JSON 字节与磁盘字节之间的转换，使后续加密无需改动业务或序列化层。
class DataCodec
{
public:
    virtual ~DataCodec() = default;

    // 编码器在首次读写前完成所需资源准备；明文模式无需额外初始化。
    virtual bool initialize(QString *errorMessage) const;
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

// 兼容编码保持 JSON 原文，并使用独立文件名，绝不覆盖 AES 模式数据。
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

// AesGcmCodec 封装加密文件格式；实际密码学运算和主密钥管理由 SecurityUtils 提供。
class AesGcmCodec final : public DataCodec
{
public:
    explicit AesGcmCodec(QString dataDirectory);

    bool initialize(QString *errorMessage) const override;
    QString fileName() const override;
    QString auditFileSuffix() const override;
    QString displayName() const override;
    bool encode(const QByteArray &plainJson,
                QByteArray *encodedData,
                QString *errorMessage) const override;
    bool decode(const QByteArray &encodedData,
                QByteArray *plainJson,
                QString *errorMessage) const override;

private:
    bool encryptedArtifactsExist() const;

    QString dataDirectory_;
    mutable QByteArray masterKey_;
};

// 正式运行按构建能力选择 AES 模式或独立的明文兼容模式。
std::shared_ptr<const DataCodec> createDefaultDataCodec(const QString &dataDirectory);
bool defaultEncryptionEnabled();
QString defaultStorageModeDisplayName();

} // namespace bank::persistence

#endif // DATACODEC_H

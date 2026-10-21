#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

/**
 * Office（docx / xlsx / pptx）解包工具。
 *
 * 三者本质都是 **ZIP + XML**：本文件提供一个**自研最小 ZIP 读取器**，
 * 只支持本次范围需要的 stored(0) 与 deflate(8) 两种压缩方式、不处理 ZIP64；
 * 压缩流走 **Qt 自带的 zlib**（`QtZlib/zlib.h`，符号在 Qt 内部已加 `z_` 前缀并由 `Qt6Core` 导出），
 * 因此**不引入任何新的第三方依赖**。
 *
 * 只读不写：绝不修改用户的原始 Office 文件。
 */
class OfficeZipArchive
{
public:
    /// 打开一个 ZIP 容器（整包读入内存）；失败写 `error` 并返回 false
    bool open(const QString &path, QString *error);

    bool isOpen() const;

    /// 全部条目名（顺序不保证；需要次序时由调用方自行排序）
    QStringList names() const;

    bool contains(const QString &entryName) const;

    /// 解出指定条目的**未压缩内容**；失败写 `error` 并返回 false
    bool read(const QString &entryName, QByteArray *data, QString *error) const;

private:
    struct Entry
    {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };

    QByteArray m_bytes;
    QHash<QString, Entry> m_entries;
};

/// XML 抽取出的文本折叠：连续空白（含换行、制表）归一为单空格并去首尾空白
QString collapseXmlWhitespace(const QString &text);

/// 元素本地名：取 `:` 之后的部分（`w:p` → `p`）。
/// 无论是否开启命名空间处理，都能稳定拿到 OOXML 的本地元素名
inline QString officeElementName(const QXmlStreamReader &reader)
{
    const QString qualified = QString(reader.qualifiedName());
    const int colon = qualified.indexOf(QLatin1Char(':'));
    return colon >= 0 ? qualified.mid(colon + 1) : qualified;
}

/// 按**本地名**取属性值（`r:id` → `id`）；不存在返回空串
inline QString officeAttribute(const QXmlStreamReader &reader, const QString &localName)
{
    const QXmlStreamAttributes attributes = reader.attributes();
    for (const QXmlStreamAttribute &attribute : attributes) {
        const QString qualified = QString(attribute.qualifiedName());
        const int colon = qualified.indexOf(QLatin1Char(':'));
        const QString local = colon >= 0 ? qualified.mid(colon + 1) : qualified;
        if (local == localName) {
            return attribute.value().toString();
        }
    }
    return QString();
}

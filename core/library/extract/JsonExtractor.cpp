#include "core/library/extract/JsonExtractor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QVariant>

namespace {

/// 展平后的语义行：JSON 路径 + 值
struct Leaf
{
    QString path;
    QString value;
};

QString scalarToText(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        return QString::number(value.toDouble(), 'g', 15);
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Null:
        return QStringLiteral("null");
    default:
        break;
    }
    return QString();
}

QString childPath(const QString &parent, const QString &key)
{
    if (parent == QStringLiteral("$")) {
        return QStringLiteral("$.%1").arg(key);
    }
    return QStringLiteral("%1.%2").arg(parent, key);
}

QString itemPath(const QString &parent, int index)
{
    return QStringLiteral("%1[%2]").arg(parent).arg(index);
}

void flatten(const QJsonValue &value, const QString &path, QList<Leaf> *leaves)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            flatten(it.value(), childPath(path, it.key()), leaves);
        }
        return;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            flatten(array.at(i), itemPath(path, i), leaves);
        }
        return;
    }
    leaves->append(Leaf{ path, scalarToText(value) });
}

/// 把一组展平行拼成片段文本；超出上限则截断并明确标注
QString buildText(const QList<Leaf> &leaves)
{
    QStringList lines;
    const int limit = qMin(leaves.size(), JsonExtractor::kMaxLinesPerSegment);
    lines.reserve(limit + 1);
    for (int i = 0; i < limit; ++i) {
        const Leaf &leaf = leaves.at(i);
        const QString value = leaf.value.isEmpty() ? QStringLiteral("(空)") : leaf.value;
        lines.append(QStringLiteral("%1: %2").arg(leaf.path, value));
    }
    if (leaves.size() > limit) {
        lines.append(QStringLiteral("（内容过多，此处已截断，共 %1 项）").arg(leaves.size()));
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

QString JsonExtractor::id() const
{
    return QStringLiteral("json");
}

bool JsonExtractor::supports(LibraryKind kind, const QString &extension) const
{
    return kind == LibraryKind::Text && extension == QStringLiteral("json");
}

bool JsonExtractor::extract(const QString &path, ExtractionResult *result, QString *error) const
{
    if (result) {
        *result = ExtractionResult();
    }

    QString text;
    if (!readLibraryTextFile(path, &text, error)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("JSON 解析失败：%1（偏移 %2）")
                         .arg(parseError.errorString())
                         .arg(parseError.offset);
        }
        return false;
    }

    QList<ExtractedSegment> segments;
    if (document.isArray()) {
        const QJsonArray array = document.array();
        for (int i = 0; i < array.size(); ++i) {
            QList<Leaf> leaves;
            const QString label = itemPath(QStringLiteral("$"), i);
            flatten(array.at(i), label, &leaves);
            if (leaves.isEmpty()) {
                continue;  // 空对象 / 空数组：无内容可检索
            }
            ExtractedSegment segment;
            segment.locKind = LocatorKind::Json;
            segment.locLabel = label;
            segment.text = buildText(leaves);
            segments.append(segment);
        }
    } else if (document.isObject()) {
        const QJsonObject object = document.object();
        for (auto it = object.begin(); it != object.end(); ++it) {
            QList<Leaf> leaves;
            const QString label = childPath(QStringLiteral("$"), it.key());
            flatten(it.value(), label, &leaves);
            if (leaves.isEmpty()) {
                continue;
            }
            ExtractedSegment segment;
            segment.locKind = LocatorKind::Json;
            segment.locLabel = label;
            segment.text = buildText(leaves);
            segments.append(segment);
        }
    } else {
        // 根为标量（如 `123` 或 `"abc"`）：JSON 允许，给出单片段
        QList<Leaf> leaves;
        flatten(QJsonValue::fromVariant(document.toVariant()), QStringLiteral("$"), &leaves);
        if (!leaves.isEmpty()) {
            ExtractedSegment segment;
            segment.locKind = LocatorKind::Json;
            segment.locLabel = QStringLiteral("$");
            segment.text = buildText(leaves);
            segments.append(segment);
        }
    }

    if (result) {
        result->segments = segments;
        result->ok = true;
    }
    if (error) {
        error->clear();
    }
    return true;
}

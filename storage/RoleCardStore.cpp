#include "storage/RoleCardStore.h"

#include "storage/Database.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QVariant>

namespace {

/// 头像副本托管目录（相对 App 根目录）：只清理该目录内的文件，绝不触碰用户原文件
constexpr char kAvatarSubdir[] = "assets/rolecards";
/// 支持的头像后缀（裁剪产物为 PNG；此处兼容常见位图格式）
const QStringList kAvatarSuffixes = { QStringLiteral("png"), QStringLiteral("jpg"),
                                      QStringLiteral("jpeg"), QStringLiteral("webp"),
                                      QStringLiteral("bmp") };
/// 单个头像文件上限：裁剪产物为 512×512 PNG，远小于此值；超限视为异常输入
constexpr qint64 kAvatarSizeLimit = 16 * 1024 * 1024;

qint64 nowMillis()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/// SQLite 的 NOT NULL 列不接受 NULL 绑定：null 字符串统一规范为空串
QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

/// file:// URL 或本地路径 → 本地路径；空 / 非本地返回空串
QString toLocalPath(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty())
        return QString();
    if (trimmed.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
        return QUrl(trimmed).toLocalFile();
    return trimmed;
}

/// 相对路径安全校验：拒绝绝对路径与 `..` 穿越（与 PersonalizationStore 同口径）
bool isSafeRelativePath(const QString &relativePath)
{
    const QString normalized = QDir::cleanPath(relativePath);
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized))
        return false;
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return !parts.contains(QStringLiteral(".."));
}

QString fieldValue(const QJsonObject &object, const char *key)
{
    return nonNullText(object.value(QLatin1String(key)).toString());
}

} // namespace

RoleCardStore::RoleCardStore(Database *database, const QString &rootDir, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_rootDir(rootDir)
{
}

bool RoleCardStore::isAvailable() const
{
    return m_database && m_database->isOpen();
}

QString RoleCardStore::lastError() const
{
    return m_lastError;
}

void RoleCardStore::setLastError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit lastErrorChanged();
}

void RoleCardStore::emitChanged()
{
    emit changed();
}

QList<RoleCardRecord> RoleCardStore::records(QString *error) const
{
    QList<RoleCardRecord> result;
    if (!isAvailable()) {
        if (error)
            *error = QStringLiteral("数据库不可用，无法读取角色卡");
        return result;
    }

    QSqlQuery query(m_database->handle());
    if (!query.exec(QStringLiteral("SELECT id, name, avatar_path, system_prompt, provider_id,"
                                   " model_id, thinking_level, created_at, updated_at"
                                   " FROM role_cards ORDER BY updated_at DESC, id DESC"))) {
        if (error)
            *error = QStringLiteral("读取角色卡失败：%1").arg(query.lastError().text());
        return result;
    }

    while (query.next()) {
        RoleCardRecord record;
        record.id = query.value(0).toLongLong();
        record.name = query.value(1).toString();
        record.avatarPath = query.value(2).toString();
        record.systemPrompt = query.value(3).toString();
        record.providerId = query.value(4).toString();
        record.modelId = query.value(5).toString();
        record.thinkingLevel = query.value(6).toString();
        record.createdAt = query.value(7).toLongLong();
        record.updatedAt = query.value(8).toLongLong();
        result.append(record);
    }
    return result;
}

QVariantMap RoleCardStore::toMap(const RoleCardRecord &record, const QString &avatarUrl)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), record.id);
    map.insert(QStringLiteral("name"), record.name);
    map.insert(QStringLiteral("avatarPath"), record.avatarPath);
    map.insert(QStringLiteral("avatarUrl"), avatarUrl);
    map.insert(QStringLiteral("systemPrompt"), record.systemPrompt);
    map.insert(QStringLiteral("providerId"), record.providerId);
    map.insert(QStringLiteral("modelId"), record.modelId);
    map.insert(QStringLiteral("thinkingLevel"), record.thinkingLevel);
    map.insert(QStringLiteral("createdAt"), record.createdAt);
    map.insert(QStringLiteral("updatedAt"), record.updatedAt);
    return map;
}

QVariantList RoleCardStore::list()
{
    QVariantList result;
    if (!isAvailable()) {
        setLastError(QStringLiteral("数据库不可用，无法读取角色卡"));
        return result;
    }
    QString error;
    const QList<RoleCardRecord> items = records(&error);
    if (!error.isEmpty()) {
        setLastError(error);
        return result;
    }
    for (const RoleCardRecord &record : items)
        result.append(toMap(record, avatarUrl(record.avatarPath)));
    return result;
}

QVariantMap RoleCardStore::get(qint64 id)
{
    QVariantMap result;
    if (!isAvailable()) {
        setLastError(QStringLiteral("数据库不可用，无法读取角色卡"));
        return result;
    }
    RoleCardRecord record;
    QString error;
    if (!fetchOne(id, &record, &error)) {
        setLastError(error);
        return result;
    }
    return toMap(record, avatarUrl(record.avatarPath));
}

bool RoleCardStore::nameExists(const QString &name, qint64 excludeId) const
{
    if (!isAvailable() || name.trimmed().isEmpty())
        return false;
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT 1 FROM role_cards WHERE name = ? AND id != ? LIMIT 1"));
    query.addBindValue(name.trimmed());
    query.addBindValue(excludeId);
    return query.exec() && query.next();
}

bool RoleCardStore::fetchOne(qint64 id, RoleCardRecord *record, QString *error) const
{
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("SELECT id, name, avatar_path, system_prompt, provider_id,"
                                 " model_id, thinking_level, created_at, updated_at"
                                 " FROM role_cards WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        if (error)
            *error = QStringLiteral("读取角色卡失败：%1").arg(query.lastError().text());
        return false;
    }
    if (!query.next()) {
        if (error)
            *error = QStringLiteral("角色卡不存在");
        return false;
    }
    if (record) {
        record->id = query.value(0).toLongLong();
        record->name = query.value(1).toString();
        record->avatarPath = query.value(2).toString();
        record->systemPrompt = query.value(3).toString();
        record->providerId = query.value(4).toString();
        record->modelId = query.value(5).toString();
        record->thinkingLevel = query.value(6).toString();
        record->createdAt = query.value(7).toLongLong();
        record->updatedAt = query.value(8).toLongLong();
    }
    return true;
}

bool RoleCardStore::insertRecord(const RoleCardRecord &record, qint64 *newId, QString *error) const
{
    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "INSERT INTO role_cards (name, avatar_path, system_prompt, provider_id, model_id,"
        " thinking_level, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(nonNullText(record.name));
    query.addBindValue(nonNullText(record.avatarPath));
    query.addBindValue(nonNullText(record.systemPrompt));
    query.addBindValue(nonNullText(record.providerId));
    query.addBindValue(nonNullText(record.modelId));
    query.addBindValue(nonNullText(record.thinkingLevel));
    query.addBindValue(record.createdAt > 0 ? record.createdAt : nowMillis());
    query.addBindValue(record.updatedAt > 0 ? record.updatedAt : nowMillis());
    if (!query.exec()) {
        if (error)
            *error = QStringLiteral("写入角色卡失败：%1").arg(query.lastError().text());
        return false;
    }
    if (newId)
        *newId = query.lastInsertId().toLongLong();
    return true;
}

qint64 RoleCardStore::create(const QString &name, const QString &systemPrompt,
                             const QString &providerId, const QString &modelId,
                             const QString &thinkingLevel, const QString &avatar)
{
    const QString trimmed = name.trimmed();
    if (!isAvailable()) {
        setLastError(QStringLiteral("数据库不可用，无法新建角色卡"));
        return -1;
    }
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("角色卡名称不能为空"));
        return -1;
    }
    if (nameExists(trimmed)) {
        setLastError(QStringLiteral("已存在同名角色卡：%1").arg(trimmed));
        return -1;
    }

    // 兼容调用方直接传入本地路径 / file URL：统一复制进托管目录后再入库
    QString avatarPath = avatar.trimmed();
    if (!avatarPath.isEmpty() && (avatarPath.startsWith(QStringLiteral("file:"))
                                  || QFileInfo::exists(toLocalPath(avatarPath))
                                  || QDir::isAbsolutePath(avatarPath))) {
        avatarPath = saveAvatar(avatarPath);
        if (avatarPath.isEmpty())
            return -1;  // saveAvatar 已写入 lastError
    }

    RoleCardRecord record;
    record.name = trimmed;
    record.systemPrompt = systemPrompt;
    record.providerId = providerId.trimmed();
    record.modelId = modelId.trimmed();
    record.thinkingLevel = thinkingLevel.trimmed();
    record.avatarPath = avatarPath;
    record.createdAt = nowMillis();
    record.updatedAt = record.createdAt;

    qint64 newId = -1;
    QString error;
    if (!insertRecord(record, &newId, &error)) {
        setLastError(error);
        return -1;
    }
    setLastError(QString());
    emitChanged();
    return newId;
}

bool RoleCardStore::update(qint64 id, const QString &name, const QString &systemPrompt,
                           const QString &providerId, const QString &modelId,
                           const QString &thinkingLevel, const QString &avatar)
{
    const QString trimmed = name.trimmed();
    if (!isAvailable()) {
        setLastError(QStringLiteral("数据库不可用，无法保存角色卡"));
        return false;
    }
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("角色卡名称不能为空"));
        return false;
    }
    if (nameExists(trimmed, id)) {
        setLastError(QStringLiteral("已存在同名角色卡：%1").arg(trimmed));
        return false;
    }

    RoleCardRecord existing;
    QString error;
    if (!fetchOne(id, &existing, &error)) {
        setLastError(error);
        return false;
    }

    QString avatarPath = avatar.trimmed();
    if (!avatarPath.isEmpty() && (avatarPath.startsWith(QStringLiteral("file:"))
                                  || QDir::isAbsolutePath(avatarPath))) {
        avatarPath = saveAvatar(avatarPath);
        if (avatarPath.isEmpty())
            return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral(
        "UPDATE role_cards SET name = ?, avatar_path = ?, system_prompt = ?, provider_id = ?,"
        " model_id = ?, thinking_level = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(trimmed);
    query.addBindValue(nonNullText(avatarPath));
    query.addBindValue(nonNullText(systemPrompt));
    query.addBindValue(nonNullText(providerId.trimmed()));
    query.addBindValue(nonNullText(modelId.trimmed()));
    query.addBindValue(nonNullText(thinkingLevel.trimmed()));
    query.addBindValue(nowMillis());
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(QStringLiteral("保存角色卡失败：%1").arg(query.lastError().text()));
        return false;
    }

    // 旧头像被替换且位于托管目录内：清理副本（失败不影响保存结果）
    if (!existing.avatarPath.isEmpty() && existing.avatarPath != avatarPath
        && isManagedAvatar(existing.avatarPath)) {
        QFile::remove(QDir(m_rootDir).filePath(existing.avatarPath));
    }
    setLastError(QString());
    emitChanged();
    return true;
}

bool RoleCardStore::remove(qint64 id)
{
    if (!isAvailable()) {
        setLastError(QStringLiteral("数据库不可用，无法删除角色卡"));
        return false;
    }

    RoleCardRecord existing;
    QString error;
    if (!fetchOne(id, &existing, &error)) {
        setLastError(error);
        return false;
    }

    QSqlQuery query(m_database->handle());
    query.prepare(QStringLiteral("DELETE FROM role_cards WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(QStringLiteral("删除角色卡失败：%1").arg(query.lastError().text()));
        return false;
    }

    // 头像副本只清理托管目录内的文件，用户原文件（选择来源）绝不删除
    if (!existing.avatarPath.isEmpty() && isManagedAvatar(existing.avatarPath))
        QFile::remove(QDir(m_rootDir).filePath(existing.avatarPath));
    setLastError(QString());
    emitChanged();
    return true;
}

bool RoleCardStore::exportToFile(const QString &path)
{
    if (!isAvailable()) {
        setLastError(QStringLiteral("数据库不可用，无法导出角色卡"));
        return false;
    }
    const QString local = toLocalPath(path);
    if (local.isEmpty()) {
        setLastError(QStringLiteral("导出路径无效"));
        return false;
    }

    QString error;
    const QList<RoleCardRecord> items = records(&error);
    if (!error.isEmpty()) {
        setLastError(error);
        return false;
    }

    QJsonArray array;
    for (const RoleCardRecord &record : items) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), QString::number(record.id));
        entry.insert(QStringLiteral("name"), record.name);
        entry.insert(QStringLiteral("avatar_path"), record.avatarPath);
        entry.insert(QStringLiteral("system_prompt"), record.systemPrompt);
        entry.insert(QStringLiteral("provider_id"), record.providerId);
        entry.insert(QStringLiteral("model_id"), record.modelId);
        entry.insert(QStringLiteral("thinking_level"), record.thinkingLevel);
        entry.insert(QStringLiteral("created_at"), QString::number(record.createdAt));
        entry.insert(QStringLiteral("updated_at"), QString::number(record.updatedAt));
        array.append(entry);
    }

    QSaveFile file(local);
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(QStringLiteral("无法写入导出文件：%1").arg(local));
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setLastError(QStringLiteral("导出文件写入失败：%1").arg(local));
        return false;
    }
    setLastError(QString());
    return true;
}

bool RoleCardStore::overwriteByName(const RoleCardRecord &record, QString *error) const
{
    QSqlQuery query(m_database->handle());
    // 覆盖语义：内容字段以导入文件为准，id 与 created_at 保留本机既有值
    query.prepare(QStringLiteral(
        "UPDATE role_cards SET avatar_path = ?, system_prompt = ?, provider_id = ?,"
        " model_id = ?, thinking_level = ?, updated_at = ? WHERE name = ?"));
    query.addBindValue(nonNullText(record.avatarPath));
    query.addBindValue(nonNullText(record.systemPrompt));
    query.addBindValue(nonNullText(record.providerId));
    query.addBindValue(nonNullText(record.modelId));
    query.addBindValue(nonNullText(record.thinkingLevel));
    query.addBindValue(record.updatedAt > 0 ? record.updatedAt : nowMillis());
    query.addBindValue(record.name);
    if (!query.exec()) {
        if (error)
            *error = QStringLiteral("覆盖角色卡失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

QVariantMap RoleCardStore::importFromFile(const QString &path, const QString &mode)
{
    QVariantMap result;
    int imported = 0;
    int skipped = 0;
    int overwritten = 0;
    int invalid = 0;
    QStringList conflicts;
    result.insert(QStringLiteral("imported"), 0);
    result.insert(QStringLiteral("skipped"), 0);
    result.insert(QStringLiteral("overwritten"), 0);
    result.insert(QStringLiteral("invalid"), 0);
    result.insert(QStringLiteral("conflicts"), QStringList());

    if (!isAvailable()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), QStringLiteral("数据库不可用，无法导入角色卡"));
        setLastError(result.value(QStringLiteral("error")).toString());
        return result;
    }

    const QString local = toLocalPath(path);
    QFile file(local);
    if (local.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"),
                      local.isEmpty() ? QStringLiteral("导入路径无效")
                                      : QStringLiteral("无法读取导入文件：%1").arg(local));
        setLastError(result.value(QStringLiteral("error")).toString());
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"),
                      QStringLiteral("导入失败：文件不是合法的角色卡 JSON 数组"));
        setLastError(result.value(QStringLiteral("error")).toString());
        return result;
    }

    // 解析有效条目（名称必填）；无效条目只计数、不中断其余导入
    QList<RoleCardRecord> entries;
    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            ++invalid;
            continue;
        }
        const QJsonObject object = value.toObject();
        const QString name = fieldValue(object, "name").trimmed();
        if (name.isEmpty()) {
            ++invalid;
            continue;
        }
        RoleCardRecord record;
        record.name = name;
        record.avatarPath = fieldValue(object, "avatar_path");
        record.systemPrompt = fieldValue(object, "system_prompt");
        record.providerId = fieldValue(object, "provider_id");
        record.modelId = fieldValue(object, "model_id");
        record.thinkingLevel = fieldValue(object, "thinking_level");
        // 时间戳缺失 / 非法时回退当前时间（保持表结构一致，不阻断导入）
        record.createdAt = fieldValue(object, "created_at").toLongLong();
        record.updatedAt = fieldValue(object, "updated_at").toLongLong();
        entries.append(record);
    }

    for (const RoleCardRecord &record : entries) {
        if (nameExists(record.name))
            conflicts.append(record.name);
    }

    if (mode == QStringLiteral("preview")) {
        // 只探测冲突，不写库：由 UI 拿 conflicts 询问用户覆盖或跳过
        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("imported"), 0);
        result.insert(QStringLiteral("invalid"), invalid);
        result.insert(QStringLiteral("conflicts"), conflicts);
        result.insert(QStringLiteral("error"), QString());
        return result;
    }

    const bool overwrite = mode == QStringLiteral("overwrite");
    if (!overwrite && mode != QStringLiteral("skip")) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), QStringLiteral("未知导入模式：%1").arg(mode));
        setLastError(result.value(QStringLiteral("error")).toString());
        return result;
    }

    QString firstError;
    bool changed = false;
    for (const RoleCardRecord &record : entries) {
        if (nameExists(record.name)) {
            if (overwrite) {
                QString error;
                if (overwriteByName(record, &error)) {
                    ++overwritten;
                    changed = true;
                } else if (firstError.isEmpty()) {
                    firstError = error;
                }
            } else {
                ++skipped;
            }
            continue;
        }
        qint64 newId = 0;
        QString error;
        if (insertRecord(record, &newId, &error)) {
            ++imported;
            changed = true;
        } else if (firstError.isEmpty()) {
            firstError = error;
        }
    }

    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("imported"), imported);
    result.insert(QStringLiteral("skipped"), skipped);
    result.insert(QStringLiteral("overwritten"), overwritten);
    result.insert(QStringLiteral("invalid"), invalid);
    result.insert(QStringLiteral("conflicts"), conflicts);
    result.insert(QStringLiteral("error"), firstError);
    setLastError(firstError);
    if (changed)
        emitChanged();
    return result;
}

QString RoleCardStore::managedAvatarDir() const
{
    return QDir(m_rootDir).filePath(QString::fromLatin1(kAvatarSubdir));
}

bool RoleCardStore::isManagedAvatar(const QString &relativePath) const
{
    if (relativePath.trimmed().isEmpty() || !isSafeRelativePath(relativePath))
        return false;
    return QDir::cleanPath(relativePath).startsWith(QLatin1String(kAvatarSubdir)
                                                    + QLatin1Char('/'));
}

QString RoleCardStore::saveAvatar(const QString &source)
{
    const QString local = toLocalPath(source);
    const QFileInfo info(local);
    if (local.isEmpty() || !info.exists() || !info.isFile()) {
        setLastError(QStringLiteral("头像文件不存在"));
        return QString();
    }
    if (!kAvatarSuffixes.contains(info.suffix().toLower())) {
        setLastError(QStringLiteral("不支持的头像格式（仅支持 %1）")
                         .arg(kAvatarSuffixes.join(QStringLiteral("/"))));
        return QString();
    }
    if (info.size() <= 0 || info.size() > kAvatarSizeLimit) {
        setLastError(QStringLiteral("头像文件大小超出限制"));
        return QString();
    }

    const QDir dir(managedAvatarDir());
    if (!dir.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("无法创建头像目录：%1").arg(dir.absolutePath()));
        return QString();
    }

    const QString fileName = QStringLiteral("rolecard-%1-%2.%3")
                                 .arg(QDateTime::currentMSecsSinceEpoch())
                                 .arg(++m_avatarSeq)
                                 .arg(info.suffix().toLower());
    const QString target = dir.filePath(fileName);
    if (QFile::exists(target) && !QFile::remove(target)) {
        setLastError(QStringLiteral("头像写入失败：%1").arg(target));
        return QString();
    }
    if (!QFile::copy(local, target)) {
        setLastError(QStringLiteral("头像复制失败，请检查本地数据目录权限"));
        return QString();
    }
    setLastError(QString());
    // 返回相对路径入库：配置可移植，不把用户绝对路径写进数据库
    return QStringLiteral("%1/%2").arg(QString::fromLatin1(kAvatarSubdir), fileName);
}

QString RoleCardStore::avatarUrl(const QString &relativePath) const
{
    const QString trimmed = relativePath.trimmed();
    if (trimmed.isEmpty() || !isSafeRelativePath(trimmed))
        return QString();
    return QUrl::fromLocalFile(QDir(m_rootDir).filePath(trimmed)).toString();
}

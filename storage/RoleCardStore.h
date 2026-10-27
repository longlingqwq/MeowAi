#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class Database;

/**
 * 角色卡数据记录（storage 层内部结构）。
 * avatarPath 一律保存「相对 App 根目录」的路径（如 assets/rolecards/xx.png）；
 * 空串表示无头像。导出 JSON 时原样携带该相对路径。
 */
struct RoleCardRecord
{
    qint64 id = 0;
    QString name;
    QString avatarPath;
    QString systemPrompt;
    QString providerId;
    QString modelId;
    QString thinkingLevel;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

/**
 * 角色卡存储（storage 层，M18）。
 *
 * 职责边界：
 *  - 只读写 role_cards 表与 <root>/assets/rolecards/ 头像副本，不做任何业务编排
 *    （套用角色卡 = 改厂商 / 模型 / 档位 / 系统提示词，由 UI 调 ChatController 完成）；
 *  - 名称必填且全表唯一（唯一索引兜底，写入前先校验给出友好错误）；
 *  - 导出为单个 JSON（数组，字段与表一致）；导入同名冲突必须由调用方显式选择
 *    覆盖或跳过（preview 模式只探测冲突、不写库），**绝不静默覆盖**；
 *  - 数据库不可用 / 文件不可读等失败路径一律返回失败 + lastError，不抛异常、不崩溃。
 */
class RoleCardStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit RoleCardStore(Database *database, const QString &rootDir, QObject *parent = nullptr);

    bool isAvailable() const;
    QString lastError() const;

    /// 角色卡列表（按 updated_at 倒序）。每项：
    /// {id, name, avatarPath, avatarUrl, systemPrompt, providerId, modelId, thinkingLevel, createdAt, updatedAt}
    Q_INVOKABLE QVariantList list();
    /// 单张卡；id 不存在时返回空 Map
    Q_INVOKABLE QVariantMap get(qint64 id);
    /// 名称是否已被占用；excludeId > 0 时排除自身（编辑改名场景）
    Q_INVOKABLE bool nameExists(const QString &name, qint64 excludeId = 0) const;

    /**
     * 新建角色卡。avatar 为 saveAvatar() 返回的相对路径或空串。
     * 返回新 id；名称为空 / 重名 / 数据库不可用时返回 -1 并写 lastError。
     */
    Q_INVOKABLE qint64 create(const QString &name, const QString &systemPrompt,
                              const QString &providerId, const QString &modelId,
                              const QString &thinkingLevel, const QString &avatar);
    /// 更新指定卡；失败（id 不存在 / 名称空 / 重名）返回 false 并写 lastError
    Q_INVOKABLE bool update(qint64 id, const QString &name, const QString &systemPrompt,
                            const QString &providerId, const QString &modelId,
                            const QString &thinkingLevel, const QString &avatar);
    /// 删除指定卡；其头像副本若位于托管目录内则一并清理（失败不影响删除结果）
    Q_INVOKABLE bool remove(qint64 id);

    /// 导出全部角色卡为单个 JSON 数组（字段与表一致）；失败返回 false 并写 lastError
    Q_INVOKABLE bool exportToFile(const QString &path);
    /**
     * 从 JSON 文件导入。mode 取值：
     *  - "preview"：只解析并回传同名冲突清单，**不写库**（供 UI 先询问用户）；
     *  - "skip"：同名卡跳过，其余导入；
     *  - "overwrite"：同名卡被导入内容覆盖（保留原 id 与创建时间），其余导入。
     * 返回 {ok, imported, skipped, overwritten, invalid, conflicts:[名称], error}。
     */
    Q_INVOKABLE QVariantMap importFromFile(const QString &path, const QString &mode);

    /**
     * 把头像源文件（本地路径或 file:// URL）复制进托管目录 assets/rolecards/。
     * 返回可入库的相对路径；源不存在 / 后缀不支持 / 超限时返回空串并写 lastError。
     */
    Q_INVOKABLE QString saveAvatar(const QString &source);
    /// 相对路径 → 绝对 file:// URL（QML 展示用）；空串或越界路径返回空串
    Q_INVOKABLE QString avatarUrl(const QString &relativePath) const;

    /// 结构化读取（C++ 侧 / 测试用）；数据库不可用时回传 error
    QList<RoleCardRecord> records(QString *error = nullptr) const;

signals:
    void changed();
    void lastErrorChanged();

private:
    bool fetchOne(qint64 id, RoleCardRecord *record, QString *error) const;
    bool insertRecord(const RoleCardRecord &record, qint64 *newId, QString *error) const;
    bool overwriteByName(const RoleCardRecord &record, QString *error) const;
    void setLastError(const QString &message);
    void emitChanged();
    QString managedAvatarDir() const;
    /// 头像副本是否落在托管目录内（删除前校验，绝不误删用户文件）
    bool isManagedAvatar(const QString &relativePath) const;
    static QVariantMap toMap(const RoleCardRecord &record, const QString &avatarUrl);

    Database *m_database = nullptr;
    QString m_rootDir;
    mutable QString m_lastError;
    int m_avatarSeq = 0;
};

#include "core/ai/ProviderCatalog.h"
#include "core/chat/ChatController.h"
#include "core/chat/ChatHistoryModel.h"
#include "platform/PlatformAdapter.h"
#include "storage/AiConfigStore.h"
#include "storage/ChatStore.h"
#include "storage/Database.h"
#include "storage/RoleCardStore.h"
#include "storage/SecureStore.h"
#include "fakes/FakeNetworkTransport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <QUrl>

#include <memory>

#ifndef MEOWAI_SOURCE_DIR
#define MEOWAI_SOURCE_DIR "."
#endif

namespace {

/// 明文可逆的平台替身：只用于让 SecureStore 在离线测试中可用
class FakePlatform : public PlatformAdapter
{
public:
    QString platformId() const override { return QStringLiteral("test"); }
    QString appDataRoot() const override { return QDir::tempPath(); }
    QString cacheRoot() const override { return QDir::tempPath(); }
    QString bundledSkillsRoot() const override { return QString(); }
    bool supportsCustomTitleBar() const override { return false; }
    bool isNetworkAvailable() const override { return true; }
    bool supportsSecretEncryption() const override { return true; }
    bool protectSecret(const QByteArray &plain, QByteArray *blob, QString *error) override
    {
        Q_UNUSED(error)
        *blob = plain;
        return true;
    }
    bool unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error) override
    {
        Q_UNUSED(error)
        *plain = blob;
        return true;
    }
    bool supportsSelfUpdate() const override { return false; }
    bool installUpdate(const QString &, QString *error) override
    {
        if (error)
            *error = QStringLiteral("不支持自更新");
        return false;
    }
};

QByteArray sseChunk(const QString &content)
{
    QJsonObject delta;
    delta.insert(QStringLiteral("content"), content);
    QJsonObject choice;
    choice.insert(QStringLiteral("delta"), delta);
    choice.insert(QStringLiteral("finish_reason"), QJsonValue::Null);
    QJsonArray choices;
    choices.append(choice);
    QJsonObject root;
    root.insert(QStringLiteral("choices"), choices);
    return QByteArrayLiteral("data: ") + QJsonDocument(root).toJson(QJsonDocument::Compact)
        + QByteArrayLiteral("\n\n");
}

QByteArray sseFinal(int prompt, int completion, int total)
{
    QJsonObject delta;
    QJsonObject choice;
    choice.insert(QStringLiteral("delta"), delta);
    choice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));
    QJsonArray choices;
    choices.append(choice);
    QJsonObject usage;
    usage.insert(QStringLiteral("prompt_tokens"), prompt);
    usage.insert(QStringLiteral("completion_tokens"), completion);
    usage.insert(QStringLiteral("total_tokens"), total);
    QJsonObject root;
    root.insert(QStringLiteral("choices"), choices);
    root.insert(QStringLiteral("usage"), usage);
    return QByteArrayLiteral("data: ") + QJsonDocument(root).toJson(QJsonDocument::Compact)
        + QByteArrayLiteral("\n\ndata: [DONE]\n\n");
}

/// 数出请求里各角色消息条数；firstContent 回传第 occurrence 条（从 0 起）的内容
int countRole(const QJsonArray &messages, const QString &role, QString *firstContent = nullptr,
              int occurrence = 0)
{
    int count = 0;
    for (const QJsonValue &value : messages) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("role")).toString() != role)
            continue;
        if (firstContent && count == occurrence)
            *firstContent = entry.value(QStringLiteral("content")).toString();
        ++count;
    }
    return count;
}

} // namespace

/**
 * 角色卡模块（M18）测试：
 *  - v5 迁移（全新库 / v4 增量升级 / 幂等重开）；
 *  - RoleCardStore：CRUD、名称必填与去重、头像托管（复制 / 展示 / 越界拒绝 / 随卡清理）、
 *    导出 JSON、导入冲突（preview / skip / overwrite，绝不静默覆盖）、损坏文件容错；
 *  - ChatController 系统提示词：作为第一条 system 消息下发、空提示词不下发、
 *    切换只影响下一条消息、不写会话历史。
 */
class TstRoleCard : public QObject
{
    Q_OBJECT

private slots:
    void schemaV5CreatesRoleCardsTable();
    void schemaV5UpgradesFromV4AndIsIdempotent();
    void crudLifecycleAndNameDedup();
    void avatarCopyResolveAndCleanup();
    void exportImportRoundTrip();
    void importConflictPreviewSkipOverwrite();
    void importInvalidFileFailsGracefully();
    void systemPromptInjectedAsFirstSystemMessage();
    void emptySystemPromptSendsNoExtraSystemMessage();
    void switchingPromptAffectsNextMessageOnly();

private:
    struct ChatHarness
    {
        QTemporaryDir tmp;
        FakePlatform platform;
        SecureStore secure{&platform, tmp.path()};
        Database db;
        std::unique_ptr<ChatStore> store;
        std::unique_ptr<ChatHistoryModel> model;
        AiConfigStore ai{QDir(tmp.path()).filePath(QStringLiteral("config.json"))};
        ProviderCatalog catalog{QStringLiteral(MEOWAI_SOURCE_DIR)
                                + QStringLiteral("/resources/providers.json")};
        FakeNetworkTransport transport;
        std::unique_ptr<ChatController> chat;

        bool init(QString *error)
        {
            if (!tmp.isValid())
                return false;
            if (!db.open(QDir(tmp.path()).filePath(QStringLiteral("metadata.db")), error))
                return false;
            if (!db.ensureSchema(error))
                return false;

            store = std::make_unique<ChatStore>(&db);
            model = std::make_unique<ChatHistoryModel>(store.get());
            if (!model->available()) {
                if (error)
                    *error = QStringLiteral("会话模型不可用");
                return false;
            }
            model->reload();

            AiConfigData config;
            config.providerId = QStringLiteral("custom");
            config.modelId = QStringLiteral("test-model");
            config.customBaseUrl = QStringLiteral("https://custom.example.com/v1");
            if (!ai.save(config, error))
                return false;
            if (!secure.save(SecureStore::providerApiKeyId(QStringLiteral("custom")),
                             QStringLiteral("sk-test"), error)) {
                return false;
            }
            if (!catalog.load(error))
                return false;

            chat = std::make_unique<ChatController>(&secure, model.get(), &catalog, &ai,
                                                    nullptr, nullptr, &transport);
            chat->setModelId(QStringLiteral("test-model"));
            return true;
        }

        void finishRound(const QString &first, const QString &second)
        {
            const int handle = transport.m_lastHandle;
            transport.pushChunk(handle, sseChunk(first));
            transport.pushChunk(handle, sseChunk(second));
            transport.pushChunk(handle, sseFinal(10, 20, 30));
            transport.pushFinished(handle);
        }

        QJsonArray lastRequestMessages()
        {
            const QJsonObject body =
                QJsonDocument::fromJson(transport.m_requests.last().body).object();
            return body.value(QStringLiteral("messages")).toArray();
        }
    };

    /// 在临时目录写一个「头像」文件（内容不要求是真实图片，存储层只按后缀与大小校验）
    QString writeFakeImage(const QString &dir, const QString &name) const
    {
        const QString path = QDir(dir).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return QString();
        file.write("PNGDATA");
        file.close();
        return path;
    }
};

// ------------------------------------------------------------ v5 迁移

void TstRoleCard::schemaV5CreatesRoleCardsTable()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error),
             qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));

    QSqlQuery query(db.handle());
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'")));
    QStringList tables;
    while (query.next())
        tables.append(query.value(0).toString());
    QVERIFY(tables.contains(QStringLiteral("role_cards")));

    QSqlQuery version(db.handle());
    QVERIFY(version.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'")));
    QVERIFY(version.next());
    QVERIFY(version.value(0).toInt() >= 5);

    // 名称唯一索引确实存在（去重的最后一道防线）
    QSqlQuery index(db.handle());
    QVERIFY(index.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'index'")));
    QStringList indexes;
    while (index.next())
        indexes.append(index.value(0).toString());
    QVERIFY(indexes.contains(QStringLiteral("idx_role_cards_name")));
}

void TstRoleCard::schemaV5UpgradesFromV4AndIsIdempotent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("meta.db"));
    QString error;

    {
        Database db;
        QVERIFY2(db.open(path, &error), qPrintable(error));
        QVERIFY2(db.ensureSchema(&error), qPrintable(error));

        // 模拟 v4 旧库：回退版本号并删除角色卡表，重开后必须走 v5 增量迁移补齐
        QSqlQuery demote(db.handle());
        QVERIFY(demote.exec(QStringLiteral("UPDATE meta SET value = '4'"
                                           " WHERE key = 'schema_version'")));
        QVERIFY(demote.exec(QStringLiteral("DROP TABLE role_cards")));

        // v1 数据不受迁移影响
        QSqlQuery seed(db.handle());
        QVERIFY(seed.exec(QStringLiteral("INSERT INTO conversations (title, created_at,"
                                         " updated_at) VALUES ('旧会话', 1, 1)")));
    }

    Database upgraded;
    QVERIFY2(upgraded.open(path, &error), qPrintable(error));
    QVERIFY2(upgraded.ensureSchema(&error), qPrintable(error));
    QVERIFY2(upgraded.ensureSchema(&error), qPrintable(error));  // 幂等可重复执行

    QSqlQuery version(upgraded.handle());
    QVERIFY(version.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'")));
    QVERIFY(version.next());
    QCOMPARE(version.value(0).toInt(), 5);

    QSqlQuery tables(upgraded.handle());
    QVERIFY(tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'")));
    QStringList names;
    while (tables.next())
        names.append(tables.value(0).toString());
    QVERIFY(names.contains(QStringLiteral("role_cards")));
    QVERIFY(names.contains(QStringLiteral("conversations")));
    QVERIFY(names.contains(QStringLiteral("messages")));

    QSqlQuery oldRow(upgraded.handle());
    QVERIFY(oldRow.exec(QStringLiteral("SELECT COUNT(*) FROM conversations")));
    QVERIFY(oldRow.next());
    QCOMPARE(oldRow.value(0).toInt(), 1);  // 既有 v4 数据未被破坏
}

// ------------------------------------------------------------ 存储层 CRUD

void TstRoleCard::crudLifecycleAndNameDedup()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error),
             qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));

    RoleCardStore store(&db, tmp.path());
    QVERIFY(store.isAvailable());

    // 名称必填
    QCOMPARE(store.create(QStringLiteral("   "), QString(), QString(), QString(), QString(),
                          QString()),
             qint64(-1));
    QVERIFY(!store.lastError().isEmpty());

    // 正常新建：字段完整往返
    const qint64 id = store.create(QStringLiteral(" 喵酱 "), QStringLiteral("你是喵酱"),
                                   QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
                                   QStringLiteral("high"), QString());
    QVERIFY(id > 0);
    QCOMPARE(store.lastError(), QString());

    const QVariantMap card = store.get(id);
    QCOMPARE(card.value(QStringLiteral("name")).toString(), QStringLiteral("喵酱"));
    QCOMPARE(card.value(QStringLiteral("systemPrompt")).toString(), QStringLiteral("你是喵酱"));
    QCOMPARE(card.value(QStringLiteral("providerId")).toString(), QStringLiteral("openai"));
    QCOMPARE(card.value(QStringLiteral("modelId")).toString(), QStringLiteral("gpt-4o-mini"));
    QCOMPARE(card.value(QStringLiteral("thinkingLevel")).toString(), QStringLiteral("high"));

    // 名称去重
    QVERIFY(store.nameExists(QStringLiteral("喵酱")));
    QVERIFY(!store.nameExists(QStringLiteral("其他")));
    QCOMPARE(store.create(QStringLiteral("喵酱"), QString(), QString(), QString(), QString(),
                          QString()),
             qint64(-1));

    const qint64 id2 = store.create(QStringLiteral("助理"), QString(), QStringLiteral("custom"),
                                    QStringLiteral("m2"), QString(), QString());
    QVERIFY(id2 > 0);
    QCOMPARE(store.list().size(), 2);

    // 改名成已有名称：拒绝；排除自身的重名判断允许保持原名
    QVERIFY(!store.update(id, QStringLiteral("助理"), QString(), QString(), QString(),
                          QString(), QString()));
    QVERIFY(store.update(id, QStringLiteral("喵酱"), QStringLiteral("你是喵酱 2"),
                         QStringLiteral("openai"), QStringLiteral("gpt-4o"),
                         QStringLiteral("low"), QString()));
    QCOMPARE(store.get(id).value(QStringLiteral("systemPrompt")).toString(),
             QStringLiteral("你是喵酱 2"));

    // 删除：存在才可删；删除后列表同步
    QVERIFY(store.remove(id));
    QVERIFY(!store.remove(id));
    QCOMPARE(store.list().size(), 1);
    QCOMPARE(store.list().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("助理"));
}

void TstRoleCard::avatarCopyResolveAndCleanup()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error),
             qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));
    RoleCardStore store(&db, tmp.path());

    const QString source = writeFakeImage(tmp.path(), QStringLiteral("avatar.png"));

    // 非法输入：不存在 / 后缀不支持 / 越界相对路径
    QCOMPARE(store.saveAvatar(QStringLiteral("/no/such/file.png")), QString());
    QVERIFY(!store.lastError().isEmpty());
    QCOMPARE(store.saveAvatar(writeFakeImage(tmp.path(), QStringLiteral("x.txt"))), QString());
    QCOMPARE(store.avatarUrl(QStringLiteral("../evil.png")), QString());
    QCOMPARE(store.avatarUrl(QString()), QString());

    // 正常复制：落进托管目录并返回相对路径；展示 URL 可解析回本地文件
    const QString relative = store.saveAvatar(source);
    QVERIFY(relative.startsWith(QStringLiteral("assets/rolecards/")));
    QVERIFY(QFile::exists(QDir(tmp.path()).filePath(relative)));

    const QUrl url(store.avatarUrl(relative));
    QVERIFY(url.isLocalFile());
    QVERIFY(QFile::exists(url.toLocalFile()));

    // 头像随卡保存；删除卡时托管目录内的副本一并清理
    const qint64 id = store.create(QStringLiteral("带头像"), QString(), QString(), QString(),
                                   QString(), relative);
    QVERIFY(id > 0);
    QCOMPARE(store.get(id).value(QStringLiteral("avatarPath")).toString(), relative);
    const QString absolute = QDir(tmp.path()).filePath(relative);
    QVERIFY(QFile::exists(absolute));
    QVERIFY(store.remove(id));
    QVERIFY(!QFile::exists(absolute));
}

// ------------------------------------------------------------ 导入 / 导出

void TstRoleCard::exportImportRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error),
             qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));
    RoleCardStore store(&db, tmp.path());

    const QString image = writeFakeImage(tmp.path(), QStringLiteral("a.png"));
    const QString avatar = store.saveAvatar(image);
    QVERIFY(store.create(QStringLiteral("喵酱"), QStringLiteral("你是喵酱"), QStringLiteral("openai"),
                         QStringLiteral("gpt-4o-mini"), QStringLiteral("high"), avatar) > 0);
    QVERIFY(store.create(QStringLiteral("助理"), QString(), QStringLiteral("custom"),
                         QStringLiteral("m2"), QString(), QString()) > 0);

    // 导出：单个 JSON 数组，字段与表一致
    const QString exportPath = QDir(tmp.path()).filePath(QStringLiteral("cards.json"));
    QVERIFY(store.exportToFile(exportPath));
    QFile file(exportPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    file.close();
    QCOMPARE(array.size(), 2);

    // 列表按 updated_at 倒序导出：按名称定位而非依赖顺序
    QJsonObject first;
    for (const QJsonValue &value : array) {
        if (value.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("喵酱"))
            first = value.toObject();
    }
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("喵酱"));
    QCOMPARE(first.value(QStringLiteral("system_prompt")).toString(), QStringLiteral("你是喵酱"));
    QCOMPARE(first.value(QStringLiteral("provider_id")).toString(), QStringLiteral("openai"));
    QCOMPARE(first.value(QStringLiteral("model_id")).toString(), QStringLiteral("gpt-4o-mini"));
    QCOMPARE(first.value(QStringLiteral("thinking_level")).toString(), QStringLiteral("high"));
    QCOMPARE(first.value(QStringLiteral("avatar_path")).toString(), avatar);

    // 清空后导入：全部还原（时间戳字段允许被导入文件带回）
    QVERIFY(store.remove(store.list().first().toMap().value(QStringLiteral("id")).toLongLong()));
    QVERIFY(store.remove(store.list().first().toMap().value(QStringLiteral("id")).toLongLong()));
    QCOMPARE(store.list().size(), 0);

    const QVariantMap result = store.importFromFile(exportPath, QStringLiteral("skip"));
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(result.value(QStringLiteral("imported")).toInt(), 2);
    QCOMPARE(store.list().size(), 2);
    QString restored;
    for (const QVariant &entry : store.list()) {
        if (entry.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("喵酱"))
            restored = entry.toMap().value(QStringLiteral("systemPrompt")).toString();
    }
    QCOMPARE(restored, QStringLiteral("你是喵酱"));
}

void TstRoleCard::importConflictPreviewSkipOverwrite()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error),
             qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));
    RoleCardStore store(&db, tmp.path());

    QVERIFY(store.create(QStringLiteral("喵酱"), QStringLiteral("原提示词"), QStringLiteral("openai"),
                         QStringLiteral("gpt-4o-mini"), QStringLiteral("high"), QString()) > 0);
    QVERIFY(store.create(QStringLiteral("保留卡"), QString(), QString(), QString(), QString(),
                         QString()) > 0);

    // 构造导入文件：一条同名（内容不同）、一条全新、一条缺名称（无效）
    QJsonArray array;
    QJsonObject conflict;
    conflict.insert(QStringLiteral("name"), QStringLiteral("喵酱"));
    conflict.insert(QStringLiteral("system_prompt"), QStringLiteral("导入提示词"));
    conflict.insert(QStringLiteral("model_id"), QStringLiteral("gpt-4o"));
    array.append(conflict);
    QJsonObject fresh;
    fresh.insert(QStringLiteral("name"), QStringLiteral("新卡"));
    fresh.insert(QStringLiteral("system_prompt"), QStringLiteral("new"));
    array.append(fresh);
    QJsonObject invalid;
    invalid.insert(QStringLiteral("system_prompt"), QStringLiteral("无名称"));
    array.append(invalid);
    const QString importPath = QDir(tmp.path()).filePath(QStringLiteral("import.json"));
    QFile file(importPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(array).toJson());
    file.close();

    // preview：只探测冲突，不写库
    const QVariantMap preview = store.importFromFile(importPath, QStringLiteral("preview"));
    QCOMPARE(preview.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(preview.value(QStringLiteral("conflicts")).toStringList(),
             QStringList{QStringLiteral("喵酱")});
    QCOMPARE(preview.value(QStringLiteral("invalid")).toInt(), 1);
    QCOMPARE(store.list().size(), 2);

    // skip：同名跳过（原内容不得被改动），新卡导入
    const QVariantMap skipped = store.importFromFile(importPath, QStringLiteral("skip"));
    QCOMPARE(skipped.value(QStringLiteral("imported")).toInt(), 1);
    QCOMPARE(skipped.value(QStringLiteral("skipped")).toInt(), 1);
    QCOMPARE(skipped.value(QStringLiteral("invalid")).toInt(), 1);
    QCOMPARE(store.list().size(), 3);
    const qint64 keptId = [&store]() {
        for (const QVariant &entry : store.list()) {
            const QVariantMap map = entry.toMap();
            if (map.value(QStringLiteral("name")).toString() == QStringLiteral("喵酱"))
                return map.value(QStringLiteral("id")).toLongLong();
        }
        return qint64(-1);
    }();
    QCOMPARE(store.get(keptId).value(QStringLiteral("systemPrompt")).toString(),
             QStringLiteral("原提示词"));

    // overwrite：同名被导入内容覆盖（id 与创建时间保留），总数不变。
    // 注意：上一轮 skip 已把「新卡」导入，本文件里的「新卡」此刻也是同名冲突 → overwritten 为 2
    const QVariantMap overwritten = store.importFromFile(importPath, QStringLiteral("overwrite"));
    QCOMPARE(overwritten.value(QStringLiteral("overwritten")).toInt(), 2);
    QCOMPARE(overwritten.value(QStringLiteral("imported")).toInt(), 0);
    QCOMPARE(store.list().size(), 3);
    QCOMPARE(store.get(keptId).value(QStringLiteral("systemPrompt")).toString(),
             QStringLiteral("导入提示词"));
    QCOMPARE(store.get(keptId).value(QStringLiteral("modelId")).toString(),
             QStringLiteral("gpt-4o"));
}

void TstRoleCard::importInvalidFileFailsGracefully()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error),
             qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));
    RoleCardStore store(&db, tmp.path());

    // 不存在的文件
    QVariantMap result = store.importFromFile(QStringLiteral("/no/such/file.json"),
                                              QStringLiteral("skip"));
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());

    // 非 JSON 数组
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("bad.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"not\":\"array\"}");
    file.close();
    result = store.importFromFile(path, QStringLiteral("skip"));
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
    QCOMPARE(store.list().size(), 0);  // 失败路径不崩溃、不写入任何数据

    // 未知模式
    result = store.importFromFile(path, QStringLiteral("unknown-mode"));
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
}

// ------------------------------------------------------------ 系统提示词下发

void TstRoleCard::systemPromptInjectedAsFirstSystemMessage()
{
    ChatHarness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    harness.chat->setSystemPrompt(QStringLiteral("你是喵酱，请用中文回答。"));
    harness.chat->send(QStringLiteral("你好"));

    const QJsonArray messages = harness.lastRequestMessages();

    // 角色卡提示词必须是**第一条** system 消息；时间上下文在其后（检索上下文再其后）
    QString firstSystem;
    QCOMPARE(countRole(messages, QStringLiteral("system"), &firstSystem), 2);
    QCOMPARE(firstSystem, QStringLiteral("你是喵酱，请用中文回答。"));
    QString secondSystem;
    countRole(messages, QStringLiteral("system"), &secondSystem, 1);
    QVERIFY(secondSystem.contains(QStringLiteral("当前时间")));

    // 用户消息存在，且 system 消息不在会话历史里（不落库）
    QCOMPARE(countRole(messages, QStringLiteral("user")), 1);
    const QList<MessageRecord> rows =
        harness.store->messages(harness.model->conversationId(), &error);
    for (const MessageRecord &row : rows)
        QVERIFY(row.role != QStringLiteral("system"));
}

void TstRoleCard::emptySystemPromptSendsNoExtraSystemMessage()
{
    ChatHarness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    // 空串 / 纯空白都视为未设置：不得出现空 system 消息
    harness.chat->setSystemPrompt(QString());
    harness.chat->send(QStringLiteral("第一问"));
    QCOMPARE(countRole(harness.lastRequestMessages(), QStringLiteral("system")), 1);
    harness.finishRound(QStringLiteral("答一"), QString());
    QVERIFY(!harness.chat->busy());

    harness.chat->setSystemPrompt(QStringLiteral("   "));
    harness.chat->send(QStringLiteral("第二问"));
    QCOMPARE(countRole(harness.lastRequestMessages(), QStringLiteral("system")), 1);
}

void TstRoleCard::switchingPromptAffectsNextMessageOnly()
{
    ChatHarness harness;
    QString error;
    QVERIFY2(harness.init(&error), qPrintable(error));

    harness.chat->setSystemPrompt(QStringLiteral("角色 A"));
    harness.chat->send(QStringLiteral("第一问"));
    harness.finishRound(QStringLiteral("答一"), QString());
    QVERIFY(!harness.chat->busy());

    // 切换提示词：下一条消息立即生效，且不改写历史消息
    harness.chat->setSystemPrompt(QStringLiteral("角色 B"));
    harness.chat->send(QStringLiteral("第二问"));

    QString firstSystem;
    countRole(harness.lastRequestMessages(), QStringLiteral("system"), &firstSystem);
    QCOMPARE(firstSystem, QStringLiteral("角色 B"));

    const QList<MessageRecord> rows =
        harness.store->messages(harness.model->conversationId(), &error);
    int promptRows = 0;
    for (const MessageRecord &row : rows) {
        QVERIFY(row.role == QStringLiteral("user") || row.role == QStringLiteral("assistant"));
        if (row.content.contains(QStringLiteral("角色")))
            ++promptRows;
    }
    QCOMPARE(promptRows, 0);  // 提示词不入库、不重写历史
}

QTEST_GUILESS_MAIN(TstRoleCard)
#include "tst_roecard.moc"

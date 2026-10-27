#include "core/skill/SkillCatalog.h"
#include "core/skill/SkillInstaller.h"
#include "core/skill/SkillRegistry.h"
#include "core/skill/SkillStateStore.h"

#include "core/ai/ProviderCatalog.h"
#include "core/chat/ChatController.h"
#include "fakes/FakeNetworkTransport.h"
#include "platform/PlatformAdapter.h"
#include "storage/AiConfigStore.h"
#include "storage/SecureStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#if defined(MEOWAI_HAVE_QZIPREADER)
#include <private/qzipwriter_p.h>
#endif

#ifndef MEOWAI_SOURCE_DIR
#define MEOWAI_SOURCE_DIR "."
#endif

namespace {

/// 明文可逆的平台替身：只为让 SecureStore 在离线测试中可用
class FakeSkillPlatform : public PlatformAdapter
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

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

/// 造一个合法技能目录：manifest.json + skill.md
QString makeSkill(const QString &root, const QString &id, const QString &name,
                  const QString &triggersJson, const QString &document)
{
    const QString dir = QDir(root).filePath(id);
    if (!QDir().mkpath(dir)) {
        return {};
    }
    const QString manifest =
        QStringLiteral(R"({"id":"%1","name":"%2","version":"1.0.0","description":"测试用技能",)"
                       R"("entry":"skill.md","triggers":%3})")
            .arg(id, name, triggersJson);
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")), manifest.toUtf8());
    writeFile(QDir(dir).filePath(QStringLiteral("skill.md")), document.toUtf8());
    return dir;
}

#if defined(MEOWAI_HAVE_QZIPREADER)

bool makeZip(const QString &zipPath, const QList<QPair<QString, QByteArray>> &files)
{
    QZipWriter writer(zipPath);
    if (!writer.isWritable()) {
        return false;
    }
    for (const auto &file : files) {
        writer.addFile(file.first, file.second);
    }
    writer.close();
    return QFileInfo::exists(zipPath);
}

QByteArray manifestJson(const QString &id, const QString &triggers = QStringLiteral("[\"测试\"]"))
{
    return QStringLiteral(R"({"id":"%1","name":"导入技能","version":"1.0.0",)"
                          R"("description":"来自技能包","entry":"skill.md","triggers":%2})")
        .arg(id, triggers)
        .toUtf8();
}

/// 标准技能包：pkg/manifest.json + pkg/skill.md
bool makeSkillPackage(const QString &zipPath, const QString &id, const QString &document,
                      const QByteArray &manifest = QByteArray())
{
    return makeZip(zipPath,
                   { { QStringLiteral("pkg/manifest.json"),
                       manifest.isEmpty() ? manifestJson(id) : manifest },
                     { QStringLiteral("pkg/skill.md"), document.toUtf8() } });
}

// --- 手工构造 zip：QZipWriter 会规范化 "../" 之类的条目名，测不出目录穿越，这里直接写字节 ---

void put16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void put32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

quint32 crc32Of(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (char raw : data) {
        crc ^= static_cast<unsigned char>(raw);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/// 只用 stored（不压缩）写一份合法 zip，条目名原样保留
bool makeRawZip(const QString &zipPath, const QList<QPair<QString, QByteArray>> &files)
{
    QByteArray out;
    QByteArray central;

    for (const auto &file : files) {
        const QByteArray name = file.first.toUtf8();
        const QByteArray data = file.second;
        const quint32 crc = crc32Of(data);
        const quint32 offset = static_cast<quint32>(out.size());

        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0x0800); // UTF-8 文件名
        put16(out, 0);      // stored
        put16(out, 0);
        put16(out, 0);
        put32(out, crc);
        put32(out, static_cast<quint32>(data.size()));
        put32(out, static_cast<quint32>(data.size()));
        put16(out, static_cast<quint16>(name.size()));
        put16(out, 0);
        out.append(name);
        out.append(data);

        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, crc);
        put32(central, static_cast<quint32>(data.size()));
        put32(central, static_cast<quint32>(data.size()));
        put16(central, static_cast<quint16>(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central.append(name);
    }

    const quint32 centralOffset = static_cast<quint32>(out.size());
    out.append(central);
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<quint16>(files.size()));
    put16(out, static_cast<quint16>(files.size()));
    put32(out, static_cast<quint32>(central.size()));
    put32(out, centralOffset);
    put16(out, 0);

    QFile file(zipPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(out) == out.size();
}

#endif

} // namespace

/**
 * 技能仓库（功能 #9 · M19）用例。
 *
 * 覆盖：manifest 校验、zip 目录穿越拒绝、启用状态持久化、触发词匹配、注入截断，
 * 以及导入体积上限、同 id 冲突、覆盖、删除权限等异常边界。
 */
class TstSkills : public QObject
{
    Q_OBJECT

private slots:
    void manifestRulesValidateFields();
    void registryParsesTriggersAndSource();
    void importRejectsPathTraversal();
    void importRejectsMissingTriggers();
    void importRejectsEmptyDocument();
    void importRejectsOversizePackage();
    void importRejectsOversizeExtracted();
    void importRejectsBundledConflict();
    void importInstallsValidPackage();
    void importOverwritesUserSkill();
    void stateStoreRoundTrip();
    void stateStoreFallsBackOnCorruptFile();
    void catalogPersistsEnabledState();
    void catalogModeSwitchPersists();
    void triggerMatchRules();
    void injectionBudgetTruncates();
    void injectionEmptyWithoutMatch();
    void manualSelectionRespectsEnabledState();
    void removeSkillOnlyForImported();
    void injectionOrderFollowsTimeThenSkill();
    void injectionOrderRoleCardFirstThenTimeThenSkill();
};

void TstSkills::manifestRulesValidateFields()
{
    QJsonObject base;
    base.insert(QStringLiteral("id"), QStringLiteral("demo"));
    base.insert(QStringLiteral("name"), QStringLiteral("演示"));
    base.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    base.insert(QStringLiteral("entry"), QStringLiteral("skill.md"));
    base.insert(QStringLiteral("triggers"), QJsonArray{QStringLiteral("演示")});

    SkillManifest manifest;
    QString reason;
    QVERIFY(SkillManifestRules::parseObject(base, &manifest, &reason));
    QCOMPARE(manifest.triggers, QStringList{QStringLiteral("演示")});

    // 缺必填字段
    QJsonObject missing = base;
    missing.remove(QStringLiteral("name"));
    QVERIFY(!SkillManifestRules::parseObject(missing, &manifest, &reason));
    QVERIFY(reason.contains(QStringLiteral("必填")));

    // id 含非法字符
    QJsonObject badId = base;
    badId.insert(QStringLiteral("id"), QStringLiteral("demo_1"));
    QVERIFY(!SkillManifestRules::parseObject(badId, &manifest, &reason));
    QVERIFY(reason.contains(QStringLiteral("字母")));

    // entry 越级
    QJsonObject badEntry = base;
    badEntry.insert(QStringLiteral("entry"), QStringLiteral("../skill.md"));
    QVERIFY(!SkillManifestRules::parseObject(badEntry, &manifest, &reason));

    // triggers 类型错误：必须是数组
    QJsonObject badTriggers = base;
    badTriggers.insert(QStringLiteral("triggers"), QStringLiteral("演示"));
    QVERIFY(!SkillManifestRules::parseObject(badTriggers, &manifest, &reason));
    QVERIFY(reason.contains(QStringLiteral("triggers")));

    // triggers 元素必须是字符串
    QJsonObject badTriggerItem = base;
    badTriggerItem.insert(QStringLiteral("triggers"), QJsonArray{1, 2});
    QVERIFY(!SkillManifestRules::parseObject(badTriggerItem, &manifest, &reason));
}

void TstSkills::registryParsesTriggersAndSource()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(!makeSkill(tmp.path(), QStringLiteral("alpha"), QStringLiteral("甲"),
                       QStringLiteral("[\"翻译\",\"translate\"]"), QStringLiteral("# 甲"))
                 .isEmpty());

    SkillRegistry registry(tmp.path(), SkillSource::Imported);
    QStringList warnings;
    QCOMPARE(registry.scan(&warnings), 1);
    QVERIFY(warnings.isEmpty());

    const Skill *skill = registry.find(QStringLiteral("alpha"));
    QVERIFY(skill != nullptr);
    QCOMPARE(skill->source(), SkillSource::Imported);
    QCOMPARE(skill->manifest().triggers,
             QStringList({ QStringLiteral("翻译"), QStringLiteral("translate") }));
}

void TstSkills::importRejectsPathTraversal()
{
    // 纯函数判定：绝对路径 / 盘符 / 上级目录 / 反斜杠变体
    QVERIFY(!SkillInstaller::isSafeEntryName(QStringLiteral("../evil.md")));
    QVERIFY(!SkillInstaller::isSafeEntryName(QStringLiteral("pkg/../../evil.md")));
    QVERIFY(!SkillInstaller::isSafeEntryName(QStringLiteral("/etc/passwd")));
    QVERIFY(!SkillInstaller::isSafeEntryName(QStringLiteral("C:/Windows/evil.dll")));
    QVERIFY(!SkillInstaller::isSafeEntryName(QStringLiteral("..\\evil.md")));
    QVERIFY(SkillInstaller::isSafeEntryName(QStringLiteral("pkg/skill.md")));

#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("evil.zip"));
    // QZipWriter 会规范化条目名，必须手工写字节才能造出真正的 "../" 条目
    QVERIFY(makeRawZip(zipPath,
                       { { QStringLiteral("pkg/manifest.json"), manifestJson(QStringLiteral("evil")) },
                         { QStringLiteral("pkg/skill.md"), QByteArrayLiteral("x") },
                         { QStringLiteral("../escape.md"), QByteArrayLiteral("boom") } }));

    const QString userRoot = QDir(tmp.path()).filePath(QStringLiteral("skills"));
    SkillInstaller installer(userRoot);
    const SkillImportResult result = installer.install(zipPath, {});
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("目录穿越")));
    // 失败后不留半成品
    QVERIFY(!QFileInfo::exists(QDir(userRoot).filePath(QStringLiteral("evil"))));
#endif
}

void TstSkills::importRejectsMissingTriggers()
{
#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("no-triggers.zip"));
    QVERIFY(makeZip(zipPath,
                    { { QStringLiteral("pkg/manifest.json"),
                        QByteArrayLiteral(R"({"id":"plain","name":"无触发词","version":"1.0.0","entry":"skill.md"})") },
                      { QStringLiteral("pkg/skill.md"), QByteArrayLiteral("# 无触发词") } }));

    SkillInstaller installer(QDir(tmp.path()).filePath(QStringLiteral("skills")));
    const SkillImportResult result = installer.install(zipPath, {});
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("triggers")));
    // 必须给出正确示例，而不是让用户猜字段
    QVERIFY(result.message.contains(QStringLiteral("\"triggers\"")));
#else
    QSKIP("当前构建缺少 QZipWriter，跳过 zip 用例");
#endif
}

void TstSkills::importRejectsEmptyDocument()
{
#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("empty-doc.zip"));
    QVERIFY(makeSkillPackage(zipPath, QStringLiteral("emptydoc"), QStringLiteral("   \n\t  ")));

    SkillInstaller installer(QDir(tmp.path()).filePath(QStringLiteral("skills")));
    const SkillImportResult result = installer.install(zipPath, {});
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("skill.md")));
#else
    QSKIP("当前构建缺少 QZipWriter，跳过 zip 用例");
#endif
}

void TstSkills::importRejectsOversizePackage()
{
#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("huge.zip"));
    // 只写体积、不写合法 zip：体积检查必须先于解压
    QFile file(zipPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QByteArray(21 * 1024 * 1024, 'a')) == 21 * 1024 * 1024);
    file.close();

    SkillInstaller installer(QDir(tmp.path()).filePath(QStringLiteral("skills")));
    const SkillImportResult result = installer.install(zipPath, {});
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("20.0 MB")));
    QVERIFY(result.message.contains(QStringLiteral("上限")));
#else
    QSKIP("当前构建缺少 QZipWriter，跳过 zip 用例");
#endif
}

void TstSkills::importRejectsOversizeExtracted()
{
#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("big-doc.zip"));
    QVERIFY(makeSkillPackage(zipPath, QStringLiteral("bigdoc"),
                             QString(4096, QLatin1Char('x'))));

    // 注入 1 KB 解压上限，验证「超限拒绝并提示实际限制」这条分支
    SkillInstaller installer(QDir(tmp.path()).filePath(QStringLiteral("skills")),
                             SkillInstaller::kDefaultMaxPackageBytes, 1024);
    const SkillImportResult result = installer.install(zipPath, {});
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("解压后体积")));
#else
    QSKIP("当前构建缺少 QZipWriter，跳过 zip 用例");
#endif
}

void TstSkills::importRejectsBundledConflict()
{
#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("conflict.zip"));
    QVERIFY(makeSkillPackage(zipPath, QStringLiteral("example"), QStringLiteral("# 冲突")));

    SkillInstaller installer(QDir(tmp.path()).filePath(QStringLiteral("skills")));
    const SkillImportResult result =
        installer.install(zipPath, { QStringLiteral("example") });
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("冲突")));
    QVERIFY(result.message.contains(QStringLiteral("manifest.id")));
#else
    QSKIP("当前构建缺少 QZipWriter，跳过 zip 用例");
#endif
}

void TstSkills::importInstallsValidPackage()
{
#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString zipPath = QDir(tmp.path()).filePath(QStringLiteral("ok.zip"));
    QVERIFY(makeSkillPackage(zipPath, QStringLiteral("translator"), QStringLiteral("# 翻译技能")));

    const QString userRoot = QDir(tmp.path()).filePath(QStringLiteral("skills"));
    SkillInstaller installer(userRoot);
    const SkillImportResult result = installer.install(zipPath, {});
    QVERIFY2(result.ok, qPrintable(result.message));
    QCOMPARE(result.skillId, QStringLiteral("translator"));
    QVERIFY(QFileInfo::exists(QDir(userRoot).filePath(QStringLiteral("translator/manifest.json"))));
    QVERIFY(QFileInfo::exists(QDir(userRoot).filePath(QStringLiteral("translator/skill.md"))));
    // 不留暂存目录
    QVERIFY(!QFileInfo::exists(QDir(userRoot).filePath(QStringLiteral(".translator.importing"))));

    // 目录能被打包进 SkillCatalog 并出现在列表里
    QTemporaryDir bundledTmp;
    QTemporaryDir stateTmp;
    SkillStateStore store(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userRoot, &store);
    catalog.reload();
    QCOMPARE(catalog.importedCount(), 1);
    QCOMPARE(catalog.ids(), QStringList{QStringLiteral("translator")});
#else
    QSKIP("当前构建缺少 QZipWriter，跳过 zip 用例");
#endif
}

void TstSkills::importOverwritesUserSkill()
{
#if defined(MEOWAI_HAVE_QZIPREADER)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString userRoot = QDir(tmp.path()).filePath(QStringLiteral("skills"));
    SkillInstaller installer(userRoot);

    const QString first = QDir(tmp.path()).filePath(QStringLiteral("v1.zip"));
    QVERIFY(makeSkillPackage(first, QStringLiteral("upgradable"), QStringLiteral("# v1")));
    QVERIFY(installer.install(first, {}).ok);

    const QString second = QDir(tmp.path()).filePath(QStringLiteral("v2.zip"));
    QVERIFY(makeZip(second,
                    { { QStringLiteral("manifest.json"),
                        QByteArrayLiteral(R"({"id":"upgradable","name":"升级版","version":"2.0.0",)"
                                          R"("entry":"skill.md","triggers":["升级"]})") },
                      { QStringLiteral("skill.md"), QByteArrayLiteral("# v2") } }));
    const SkillImportResult result = installer.install(second, {});
    QVERIFY2(result.ok, qPrintable(result.message));
    QVERIFY(result.message.contains(QStringLiteral("覆盖")));

    QFile doc(QDir(userRoot).filePath(QStringLiteral("upgradable/skill.md")));
    QVERIFY(doc.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(doc.readAll()), QStringLiteral("# v2"));
#else
    QSKIP("当前构建缺少 QZipWriter，跳过 zip 用例");
#endif
}

void TstSkills::stateStoreRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("skills_state.json"));

    SkillStateStore store(path);
    SkillStateData data;
    data.mode = SkillStateStore::manualMode();
    data.enabled.insert(QStringLiteral("alpha"), false);
    data.enabled.insert(QStringLiteral("beta"), true);

    QString error;
    QVERIFY2(store.save(data, &error), qPrintable(error));

    const SkillStateData loaded = store.load(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.mode, SkillStateStore::manualMode());
    QVERIFY(!loaded.isEnabled(QStringLiteral("alpha")));
    QVERIFY(loaded.isEnabled(QStringLiteral("beta")));
    // 未记录的技能默认启用
    QVERIFY(loaded.isEnabled(QStringLiteral("unknown")));

    // 文件确实是 JSON 且字段名固定
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("mode")).toString(), QStringLiteral("manual"));
    QVERIFY(root.value(QStringLiteral("enabled")).isObject());
}

void TstSkills::stateStoreFallsBackOnCorruptFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("skills_state.json"));
    QVERIFY(writeFile(path, QByteArrayLiteral("{not-json")));

    SkillStateStore store(path);
    QString error;
    const SkillStateData data = store.load(&error);
    QVERIFY(!error.isEmpty());
    QCOMPARE(data.mode, SkillStateStore::autoMode());
    QVERIFY(data.isEnabled(QStringLiteral("anything")));
}

void TstSkills::catalogPersistsEnabledState()
{
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());
    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("alpha"), QStringLiteral("甲"),
                       QStringLiteral("[\"翻译\"]"), QStringLiteral("# 甲"))
                 .isEmpty());

    const QString statePath = QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json"));

    {
        SkillStateStore store(statePath);
        SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
        catalog.reload();
        QVERIFY(catalog.isEnabled(QStringLiteral("alpha")));
        QVERIFY(catalog.setSkillEnabled(QStringLiteral("alpha"), false));
        QVERIFY(!catalog.isEnabled(QStringLiteral("alpha")));
    }

    // 新实例（模拟重启）应读到禁用状态
    {
        SkillStateStore store(statePath);
        SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
        catalog.reload();
        QVERIFY(!catalog.isEnabled(QStringLiteral("alpha")));
        QVERIFY(catalog.matchAuto(QStringLiteral("帮我翻译")).isEmpty());
    }
}

void TstSkills::catalogModeSwitchPersists()
{
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());
    const QString statePath = QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json"));

    {
        SkillStateStore store(statePath);
        SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
        QCOMPARE(catalog.mode(), QStringLiteral("auto"));
        QVERIFY(!catalog.manualMode());
        catalog.setMode(QStringLiteral("manual"));
        QVERIFY(catalog.manualMode());
        // 非法值不生效
        catalog.setMode(QStringLiteral("hybrid"));
        QCOMPARE(catalog.mode(), QStringLiteral("manual"));
    }

    SkillStateStore store(statePath);
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
    QCOMPARE(catalog.mode(), QStringLiteral("manual"));
}

void TstSkills::triggerMatchRules()
{
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());

    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("alpha"), QStringLiteral("甲"),
                       QStringLiteral("[\"翻译\"]"), QStringLiteral("# 甲"))
                 .isEmpty());
    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("beta"), QStringLiteral("乙"),
                       QStringLiteral("[\"润色\"]"), QStringLiteral("# 乙"))
                 .isEmpty());
    QVERIFY(!makeSkill(userTmp.path(), QStringLiteral("gamma"), QStringLiteral("丙"),
                       QStringLiteral("[\"Example\"]"), QStringLiteral("# 丙"))
                 .isEmpty());

    SkillStateStore store(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
    catalog.reload();

    // 中文子串匹配
    QCOMPARE(catalog.matchAuto(QStringLiteral("帮我翻译这段")), QStringList{QStringLiteral("alpha")});
    // 英文不区分大小写
    QCOMPARE(catalog.matchAuto(QStringLiteral("this is an EXAMPLE")),
             QStringList{QStringLiteral("gamma")});
    // 按首次出现位置排序：润色在前
    QCOMPARE(catalog.matchAuto(QStringLiteral("先润色，再翻译")),
             QStringList({ QStringLiteral("beta"), QStringLiteral("alpha") }));
    // 无命中
    QVERIFY(catalog.matchAuto(QStringLiteral("今天天气如何")).isEmpty());
    // 禁用后不再命中
    QVERIFY(catalog.setSkillEnabled(QStringLiteral("alpha"), false));
    QVERIFY(catalog.matchAuto(QStringLiteral("翻译")).isEmpty());
}

void TstSkills::injectionBudgetTruncates()
{
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());

    const QString huge = QStringLiteral("字").repeated(5000);
    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("huge"), QStringLiteral("长文"),
                       QStringLiteral("[\"长文\"]"), huge)
                 .isEmpty());

    SkillStateStore store(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
    catalog.reload();

    const SkillCatalog::Injection injection = catalog.composeInjection(QStringLiteral("长文测试"), {});
    QVERIFY(!injection.text.isEmpty());
    QCOMPARE(injection.skillIds, QStringList{QStringLiteral("huge")});
    QCOMPARE(injection.text.size(), SkillCatalog::kMaxInjectionChars);
    // 会话中不留截断提示
    QVERIFY(!injection.text.contains(QStringLiteral("截断")));
    QVERIFY(injection.text.contains(QStringLiteral("## 技能：")));
}

void TstSkills::injectionEmptyWithoutMatch()
{
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());
    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("alpha"), QStringLiteral("甲"),
                       QStringLiteral("[\"翻译\"]"), QStringLiteral("# 甲"))
                 .isEmpty());

    SkillStateStore store(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
    catalog.reload();

    const SkillCatalog::Injection injection = catalog.composeInjection(QStringLiteral("无关内容"), {});
    QVERIFY(injection.text.isEmpty());
    QVERIFY(injection.skillIds.isEmpty());
}

void TstSkills::manualSelectionRespectsEnabledState()
{
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());
    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("alpha"), QStringLiteral("甲"),
                       QStringLiteral("[\"翻译\"]"), QStringLiteral("# 甲的文档"))
                 .isEmpty());
    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("beta"), QStringLiteral("乙"),
                       QStringLiteral("[\"润色\"]"), QStringLiteral("# 乙的文档"))
                 .isEmpty());

    SkillStateStore store(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
    catalog.reload();
    catalog.setMode(QStringLiteral("manual"));

    // 手动模式：不看触发词，只看选择
    const SkillCatalog::Injection picked =
        catalog.composeInjection(QStringLiteral("完全无关的问题"),
                                 { QStringLiteral("alpha") });
    QVERIFY(picked.text.contains(QStringLiteral("# 甲的文档")));

    // 禁用后即使被选择也不注入
    QVERIFY(catalog.setSkillEnabled(QStringLiteral("alpha"), false));
    const SkillCatalog::Injection disabled =
        catalog.composeInjection(QStringLiteral("随便"), { QStringLiteral("alpha") });
    QVERIFY(disabled.text.isEmpty());

    // 上限 2 个：多传的会被裁掉
    const SkillCatalog::Injection capped =
        catalog.composeInjection(QStringLiteral("随便"),
                                 { QStringLiteral("beta"), QStringLiteral("beta"),
                                   QStringLiteral("alpha") });
    QCOMPARE(capped.skillIds, QStringList{QStringLiteral("beta")});
}

void TstSkills::removeSkillOnlyForImported()
{
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());
    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("alpha"), QStringLiteral("甲"),
                       QStringLiteral("[\"翻译\"]"), QStringLiteral("# 甲"))
                 .isEmpty());
    QVERIFY(!makeSkill(userTmp.path(), QStringLiteral("gamma"), QStringLiteral("丙"),
                       QStringLiteral("[\"摘要\"]"), QStringLiteral("# 丙"))
                 .isEmpty());

    SkillStateStore store(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &store);
    catalog.reload();

    const QVariantMap bundled = catalog.removeSkill(QStringLiteral("alpha"));
    QVERIFY(!bundled.value(QStringLiteral("ok")).toBool());
    QVERIFY(bundled.value(QStringLiteral("message")).toString().contains(QStringLiteral("内置")));
    QVERIFY(QFileInfo::exists(QDir(bundledTmp.path()).filePath(QStringLiteral("alpha"))));

    const QVariantMap imported = catalog.removeSkill(QStringLiteral("gamma"));
    QVERIFY(imported.value(QStringLiteral("ok")).toBool());
    QVERIFY(!QFileInfo::exists(QDir(userTmp.path()).filePath(QStringLiteral("gamma"))));
    QCOMPARE(catalog.ids(), QStringList{QStringLiteral("alpha")});
}

/**
 * 端到端校验注入位置：用假网络层截获真实请求体，
 * 断言 system 消息顺序为「当前时间 → 技能」，且禁用后不再出现技能消息。
 */
void TstSkills::injectionOrderFollowsTimeThenSkill()
{
    QTemporaryDir tmp;
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(tmp.isValid() && bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());

    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("codeword"), QStringLiteral("暗号应答"),
                       QStringLiteral("[\"暗号\"]"),
                       QStringLiteral("# 暗号应答\n回答第一句以「喵～」开头。"))
                 .isEmpty());

    FakeSkillPlatform platform;
    SecureStore secure(&platform, tmp.path());
    AiConfigStore ai(QDir(tmp.path()).filePath(QStringLiteral("config.json")));

    QString error;
    AiConfigData config;
    config.providerId = QStringLiteral("custom");
    config.modelId = QStringLiteral("test-model");
    config.customBaseUrl = QStringLiteral("https://custom.example.com/v1");
    QVERIFY2(ai.save(config, &error), qPrintable(error));
    QVERIFY2(secure.save(SecureStore::providerApiKeyId(QStringLiteral("custom")),
                         QStringLiteral("sk-test"), &error),
             qPrintable(error));

    ProviderCatalog providers(QStringLiteral(MEOWAI_SOURCE_DIR) + QStringLiteral("/resources/providers.json"));
    QVERIFY2(providers.load(&error), qPrintable(error));

    FakeNetworkTransport transport;
    ChatController chat(&secure, nullptr, &providers, &ai, nullptr, nullptr, &transport);
    chat.setModelId(QStringLiteral("test-model"));

    SkillStateStore state(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &state);
    catalog.reload();
    chat.attachSkills(&catalog);

    chat.send(QStringLiteral("暗号：你好"));
    QCOMPARE(transport.m_requests.size(), 1);

    const QJsonObject body = QJsonDocument::fromJson(transport.m_requests.first().body).object();
    const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
    QVERIFY(messages.size() >= 2);

    // 0：当前时间；1：技能（角色卡落地后应插在两者之间）；最后：用户消息
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QVERIFY(messages.at(0).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("当前时间")));
    QCOMPARE(messages.at(1).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QVERIFY(messages.at(1).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("暗号应答")));
    QCOMPARE(messages.last().toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("user"));

    // 禁用后：无命中即不注入，system 只剩当前时间
    chat.cancel();
    QVERIFY(catalog.setSkillEnabled(QStringLiteral("codeword"), false));
    chat.send(QStringLiteral("暗号：再来一次"));
    QCOMPARE(transport.m_requests.size(), 2);

    const QJsonArray second =
        QJsonDocument::fromJson(transport.m_requests.at(1).body).object()
            .value(QStringLiteral("messages")).toArray();
    QCOMPARE(second.size(), 2);
    QCOMPARE(second.at(1).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("user"));
}

/**
 * 融合回归：M18 角色卡与 M19 技能同时存在时，system 消息顺序必须是
 * 「角色卡 → 当前时间 → 技能」。这是两支合并后的固定契约——角色卡是人格设定，
 * 必须排在所有背景资料（时间 / 技能 / 资料库）之前，否则模型会把人格当资料读。
 */
void TstSkills::injectionOrderRoleCardFirstThenTimeThenSkill()
{
    QTemporaryDir tmp;
    QTemporaryDir bundledTmp;
    QTemporaryDir userTmp;
    QTemporaryDir stateTmp;
    QVERIFY(tmp.isValid() && bundledTmp.isValid() && userTmp.isValid() && stateTmp.isValid());

    QVERIFY(!makeSkill(bundledTmp.path(), QStringLiteral("codeword"), QStringLiteral("暗号应答"),
                       QStringLiteral("[\"暗号\"]"),
                       QStringLiteral("# 暗号应答\n回答第一句以「喵～」开头。"))
                 .isEmpty());

    FakeSkillPlatform platform;
    SecureStore secure(&platform, tmp.path());
    AiConfigStore ai(QDir(tmp.path()).filePath(QStringLiteral("config.json")));

    QString error;
    AiConfigData config;
    config.providerId = QStringLiteral("custom");
    config.modelId = QStringLiteral("test-model");
    config.customBaseUrl = QStringLiteral("https://custom.example.com/v1");
    QVERIFY2(ai.save(config, &error), qPrintable(error));
    QVERIFY2(secure.save(SecureStore::providerApiKeyId(QStringLiteral("custom")),
                         QStringLiteral("sk-test"), &error),
             qPrintable(error));

    ProviderCatalog providers(QStringLiteral(MEOWAI_SOURCE_DIR) + QStringLiteral("/resources/providers.json"));
    QVERIFY2(providers.load(&error), qPrintable(error));

    FakeNetworkTransport transport;
    ChatController chat(&secure, nullptr, &providers, &ai, nullptr, nullptr, &transport);
    chat.setModelId(QStringLiteral("test-model"));

    SkillStateStore state(QDir(stateTmp.path()).filePath(QStringLiteral("skills_state.json")));
    SkillCatalog catalog(bundledTmp.path(), userTmp.path(), &state);
    catalog.reload();
    chat.attachSkills(&catalog);

    chat.setSystemPrompt(QStringLiteral("你是喵酱，自称喵。"));
    chat.send(QStringLiteral("暗号：你好"));
    QCOMPARE(transport.m_requests.size(), 1);

    const QJsonArray messages =
        QJsonDocument::fromJson(transport.m_requests.first().body).object()
            .value(QStringLiteral("messages")).toArray();
    QVERIFY(messages.size() >= 3);

    // 0 = 角色卡（第一条 system，人格设定）
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QVERIFY(messages.at(0).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("喵酱")));
    // 1 = 当前时间
    QCOMPARE(messages.at(1).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QVERIFY(messages.at(1).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("当前时间")));
    // 2 = 技能
    QCOMPARE(messages.at(2).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QVERIFY(messages.at(2).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("暗号应答")));
    // 末尾 = 用户消息
    QCOMPARE(messages.last().toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("user"));

    // 清空提示词后必须回到「时间 → 技能」，不残留空 system
    chat.cancel();
    chat.setSystemPrompt(QString());
    chat.send(QStringLiteral("暗号：再来一次"));
    QCOMPARE(transport.m_requests.size(), 2);

    const QJsonArray second =
        QJsonDocument::fromJson(transport.m_requests.at(1).body).object()
            .value(QStringLiteral("messages")).toArray();
    QCOMPARE(second.size(), 3);
    QVERIFY(second.at(0).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("当前时间")));
    QVERIFY(second.at(1).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("暗号应答")));
}

QTEST_GUILESS_MAIN(TstSkills)
#include "tst_skills.moc"

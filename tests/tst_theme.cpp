#include "core/theme/ThemeManager.h"
#include "storage/AppPaths.h"
#include "storage/PersonalizationStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

namespace {

/// 在指定路径写入内容；返回路径便于链式使用
QString writeFile(const QString &path, const QByteArray &content = QByteArray("x"))
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(content);
    file.close();
    return path;
}

QJsonObject readRoot(const QString &configFile)
{
    QFile file(configFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

} // namespace

/**
 * 个性化模块（功能 #11）单元测试。
 *
 * 使用 QTemporaryDir 隔离本地数据目录，覆盖正常往返与全部异常边界
 * （配置缺失 / 损坏、资源丢失、非法格式、空文件、超大文件、路径穿越、依赖为空）。
 */
class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void defaultsWhenConfigMissing();
    void roundTripBackground();
    void savePreservesOtherKeys();
    void corruptConfigFallsBack();
    void missingAssetFallsBackToDefault();
    void importMissingSourceFails();
    void importUnsupportedExtensionRejected();
    void importEmptyFileRejected();
    void importOversizeRejected();
    void pathTraversalRejected();
    void clearBackgroundRemovesFile();
    void resetAllRestoresDefaults();
    void splashLifecycle();
    void systemFontRejectsUnknown();
    void importFontRejectedInHeadless();
    void nullStoreDoesNotCrash();
};

void TestTheme::defaultsWhenConfigMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(theme.available());
    QCOMPARE(theme.fontFamily(), QString());
    QCOMPARE(theme.backgroundSource(), QString());
    QCOMPARE(theme.avatarSource(), QString());
    QCOMPARE(theme.splashSource(), QString());
    QCOMPARE(theme.splashEnabled(), false);
    QCOMPARE(theme.splashSoundEnabled(), true);
}

void TestTheme::roundTripBackground()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString source = writeFile(dir.filePath(QStringLiteral("bg.png")));
    QVERIFY(!source.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(theme.setBackground(source));
    QVERIFY(theme.backgroundSource().contains(QStringLiteral("assets/backgrounds")));
    QVERIFY(theme.lastError().isEmpty());

    // 重新载入（模拟重启）后配置应保持一致
    PersonalizationStore rereadStore(paths.configFile());
    ThemeManager reread(paths, &rereadStore);
    reread.load();
    QCOMPARE(reread.backgroundSource(), theme.backgroundSource());
}

void TestTheme::savePreservesOtherKeys()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    QJsonObject root;
    root.insert(QStringLiteral("model"), QJsonObject{ { QStringLiteral("provider"),
                                                        QStringLiteral("openai") } });
    QJsonObject section;
    section.insert(QStringLiteral("font_family"), QStringLiteral("ExistingFamily"));
    root.insert(QStringLiteral("personalization"), section);
    QVERIFY(!writeFile(paths.configFile(), QJsonDocument(root).toJson()).isEmpty());

    const QString source = writeFile(dir.filePath(QStringLiteral("bg.png")));
    QVERIFY(!source.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();
    QCOMPARE(theme.fontFamily(), QStringLiteral("ExistingFamily"));
    QVERIFY(theme.setBackground(source));

    // 其他模块的键必须原样保留
    const QJsonObject after = readRoot(paths.configFile());
    QCOMPARE(after.value(QStringLiteral("model")).toObject().value(QStringLiteral("provider")).toString(),
             QStringLiteral("openai"));
    QCOMPARE(after.value(QStringLiteral("personalization"))
                 .toObject()
                 .value(QStringLiteral("font_family"))
                 .toString(),
             QStringLiteral("ExistingFamily"));
}

void TestTheme::corruptConfigFallsBack()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));
    QVERIFY(!writeFile(paths.configFile(), QByteArray("{ this is not json")).isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load(); // 不得崩溃

    QVERIFY(theme.available());
    QCOMPARE(theme.backgroundSource(), QString());
    QVERIFY(!theme.lastError().isEmpty());
}

void TestTheme::missingAssetFallsBackToDefault()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    PersonalizationStore store(paths.configFile());
    PersonalizationConfig config;
    config.backgroundSource = QStringLiteral("assets/backgrounds/nope.png");
    config.avatarSource = QStringLiteral("assets/avatars/nope.png");
    QVERIFY(store.save(config, &error));

    ThemeManager theme(paths, &store);
    theme.load();

    QCOMPARE(theme.backgroundSource(), QString());
    QCOMPARE(theme.avatarSource(), QString());
    QVERIFY(!theme.lastError().isEmpty());
}

void TestTheme::importMissingSourceFails()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(!theme.setAvatar(dir.filePath(QStringLiteral("not-there.png"))));
    QVERIFY(!theme.lastError().isEmpty());
    QCOMPARE(theme.avatarSource(), QString());
}

void TestTheme::importUnsupportedExtensionRejected()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString source = writeFile(dir.filePath(QStringLiteral("note.txt")));
    QVERIFY(!source.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(!theme.setBackground(source));
    QVERIFY(theme.lastError().contains(QStringLiteral("不支持")));
}

void TestTheme::importEmptyFileRejected()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString source = writeFile(dir.filePath(QStringLiteral("empty.png")), QByteArray());
    QVERIFY(!source.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(!theme.setBackground(source));
    QVERIFY(theme.lastError().contains(QStringLiteral("空")));
}

void TestTheme::importOversizeRejected()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString source = dir.filePath(QStringLiteral("huge.png"));
    QFile big(source);
    QVERIFY(big.open(QIODevice::WriteOnly));
    QVERIFY(big.resize(21 * 1024 * 1024)); // 超过 20 MB 上限
    big.close();

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(!theme.setBackground(source));
    QVERIFY(theme.lastError().contains(QStringLiteral("过大")));
}

void TestTheme::pathTraversalRejected()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    QVERIFY(!PersonalizationStore::isSafeRelativePath(QStringLiteral("../escape.png")));
    QVERIFY(!PersonalizationStore::isSafeRelativePath(QStringLiteral("C:/windows/system32/x.png")));
    QVERIFY(!PersonalizationStore::isSafeRelativePath(QStringLiteral("/etc/passwd")));
    QVERIFY(PersonalizationStore::isSafeRelativePath(QStringLiteral("assets/avatars/a.png")));

    // 越权路径不得被写入配置
    PersonalizationStore store(paths.configFile());
    PersonalizationConfig config;
    config.backgroundSource = QStringLiteral("../escape.png");
    QVERIFY(!store.save(config, &error));
    QVERIFY(!error.isEmpty());

    // 手工写入的越权路径在读取时应被忽略
    QJsonObject section;
    section.insert(QStringLiteral("background_source"), QStringLiteral("../escape.png"));
    QJsonObject root;
    root.insert(QStringLiteral("personalization"), section);
    QVERIFY(!writeFile(paths.configFile(), QJsonDocument(root).toJson()).isEmpty());

    ThemeManager theme(paths, &store);
    theme.load();
    QCOMPARE(theme.backgroundSource(), QString());
}

void TestTheme::clearBackgroundRemovesFile()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString source = writeFile(dir.filePath(QStringLiteral("bg.png")));
    QVERIFY(!source.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();
    QVERIFY(theme.setBackground(source));

    const QString imported = QUrl(theme.backgroundSource()).toLocalFile();
    QVERIFY(QFile::exists(imported));

    QVERIFY(theme.clearBackground());
    QCOMPARE(theme.backgroundSource(), QString());
    QVERIFY(!QFile::exists(imported));
}

void TestTheme::resetAllRestoresDefaults()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString background = writeFile(dir.filePath(QStringLiteral("bg.png")));
    const QString avatar = writeFile(dir.filePath(QStringLiteral("avatar.png")));
    const QString splash = writeFile(dir.filePath(QStringLiteral("splash.mp4")));
    QVERIFY(!background.isEmpty() && !avatar.isEmpty() && !splash.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();
    QVERIFY(theme.setBackground(background));
    QVERIFY(theme.setAvatar(avatar));
    QVERIFY(theme.setSplash(splash));

    QVERIFY(theme.resetAll());
    QCOMPARE(theme.backgroundSource(), QString());
    QCOMPARE(theme.avatarSource(), QString());
    QCOMPARE(theme.splashSource(), QString());
    QCOMPARE(theme.splashEnabled(), false);

    PersonalizationStore rereadStore(paths.configFile());
    ThemeManager reread(paths, &rereadStore);
    reread.load();
    QCOMPARE(reread.backgroundSource(), QString());
    QCOMPARE(reread.avatarSource(), QString());
    QCOMPARE(reread.splashSource(), QString());
}

void TestTheme::splashLifecycle()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString splash = writeFile(dir.filePath(QStringLiteral("splash.mp4")));
    QVERIFY(!splash.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(theme.setSplash(splash));
    QVERIFY(theme.splashEnabled()); // 选定视频后自动开启
    QVERIFY(theme.splashSoundEnabled());
    QVERIFY(theme.splashSource().contains(QStringLiteral("assets/splash")));

    theme.setSplashSoundEnabled(false);

    PersonalizationStore rereadStore(paths.configFile());
    ThemeManager reread(paths, &rereadStore);
    reread.load();
    QCOMPARE(reread.splashSource(), theme.splashSource());
    QCOMPARE(reread.splashEnabled(), true);
    QCOMPARE(reread.splashSoundEnabled(), false);

    QVERIFY(reread.clearSplash());
    QCOMPARE(reread.splashSource(), QString());
    QCOMPARE(reread.splashEnabled(), false);
}

void TestTheme::systemFontRejectsUnknown()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    // 无界面测试环境没有字体数据库，任何字体都应被拒绝且不崩溃
    QVERIFY(theme.availableFontFamilies().isEmpty());
    QVERIFY(!theme.setSystemFont(QStringLiteral("DefinitelyNotAFontFamilyXYZ")));
    QVERIFY(!theme.lastError().isEmpty());
    QCOMPARE(theme.fontFamily(), QString());
}

void TestTheme::importFontRejectedInHeadless()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    const QString fakeFont = writeFile(dir.filePath(QStringLiteral("fake.ttf")));
    QVERIFY(!fakeFont.isEmpty());

    PersonalizationStore store(paths.configFile());
    ThemeManager theme(paths, &store);
    theme.load();

    QVERIFY(!theme.importFont(fakeFont));
    QVERIFY(!theme.lastError().isEmpty());
    QCOMPARE(theme.fontFamily(), QString());
}

void TestTheme::nullStoreDoesNotCrash()
{
    QTemporaryDir dir;
    AppPaths paths(dir.path());
    QString error;
    QVERIFY(paths.ensureLayout(&error));

    ThemeManager theme(paths, nullptr);
    theme.load(); // 不得崩溃

    QVERIFY(!theme.available());
    QVERIFY(!theme.lastError().isEmpty());
    QVERIFY(!theme.setBackground(writeFile(dir.filePath(QStringLiteral("bg.png")))));
    QVERIFY(!theme.resetAll());
    QCOMPARE(theme.backgroundSource(), QString());
}

QTEST_GUILESS_MAIN(TestTheme)
#include "tst_theme.moc"

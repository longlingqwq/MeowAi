#include "core/theme/ThemePresetManager.h"
#include "storage/ThemeStore.h"

#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

/// 预设 JSON 由测试构造：只为验证解析 / 应用 / 持久化，不依赖可执行程序的资源
QByteArray presetJson(const QString &id, const QString &name, const QString &accent, int radius,
                      const QString &easing)
{
    QJsonObject tokens;
    tokens.insert(QStringLiteral("ink0"), accent);
    tokens.insert(QStringLiteral("rMd"), radius);
    tokens.insert(QStringLiteral("easeOut"), easing);
    tokens.insert(QStringLiteral("extraToken"), QStringLiteral("#FFFFFFFF"));

    QJsonObject root;
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("tokens"), tokens);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(data);
    file.close();
    return true;
}

} // namespace

/// 令牌写入目标：模拟 QML 单例 Theme 的可写属性
class TokenTarget : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor ink0 READ ink0 WRITE setInk0 NOTIFY ink0Changed)
    Q_PROPERTY(int rMd READ rMd WRITE setRMd NOTIFY rMdChanged)
    Q_PROPERTY(int easeOut READ easeOut WRITE setEaseOut NOTIFY easeOutChanged)

public:
    using QObject::QObject;

    QColor ink0() const { return m_ink0; }
    void setInk0(const QColor &value)
    {
        m_ink0 = value;
        emit ink0Changed();
    }
    int rMd() const { return m_rMd; }
    void setRMd(int value)
    {
        m_rMd = value;
        emit rMdChanged();
    }
    int easeOut() const { return m_easeOut; }
    void setEaseOut(int value)
    {
        m_easeOut = value;
        emit easeOutChanged();
    }

signals:
    void ink0Changed();
    void rMdChanged();
    void easeOutChanged();

private:
    QColor m_ink0;
    int m_rMd = 0;
    int m_easeOut = -1;
};

/**
 * 主题预设（阶段三）边界测试：
 *  - JSON 解析（颜色 / 整数 / 缓动名三种值形态）；
 *  - 令牌写入目标对象，并暴露契约漂移（未识别令牌）；
 *  - 目录扫描 + 选择持久化（重启后保持）；
 *  - 损坏 JSON / 未知主题等异常不崩溃。
 */
class TstThemePreset : public QObject
{
    Q_OBJECT

private slots:
    void parsePresetHandlesValueForms();
    void parsePresetRejectsBrokenInput();
    void applyToObjectWritesTokensAndReportsDrift();
    void managerLoadsPresetsAndPersistsSelection();
    void managerRejectsUnknownPreset();
    void managerWithoutStoreKeepsDefault();
};

void TstThemePreset::parsePresetHandlesValueForms()
{
    ThemePreset preset;
    QString error;
    QVERIFY2(ThemePresetManager::parsePreset(
                 presetJson(QStringLiteral("theme9"), QStringLiteral("测试"),
                            QStringLiteral("#FF3B82F6"), 14, QStringLiteral("InOutCubic")),
                 &preset, &error),
             qPrintable(error));

    QCOMPARE(preset.id, QStringLiteral("theme9"));
    QCOMPARE(preset.name, QStringLiteral("测试"));

    const QVariant color = preset.tokens.value(QStringLiteral("ink0"));
    QVERIFY(color.canConvert<QColor>());
    QCOMPARE(color.value<QColor>(), QColor(QStringLiteral("#FF3B82F6")));

    QCOMPARE(preset.tokens.value(QStringLiteral("rMd")).toInt(), 14);
    QCOMPARE(preset.tokens.value(QStringLiteral("easeOut")).toInt(),
             static_cast<int>(QEasingCurve::InOutCubic));
}

void TstThemePreset::parsePresetRejectsBrokenInput()
{
    ThemePreset preset;
    QString error;

    QVERIFY(!ThemePresetManager::parsePreset(QByteArrayLiteral("{ not json"), &preset, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!ThemePresetManager::parsePreset(QByteArrayLiteral("{\"id\":\"x\"}"), &preset,
                                             &error));
    QVERIFY(!error.isEmpty());

    // 未知缓动名 / 非法颜色都必须被拒绝，而不是静默写入
    QVERIFY(!ThemePresetManager::parsePreset(
        presetJson(QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("#FF000000"), 6,
                   QStringLiteral("NotAnEasing")),
        &preset, &error));

    QVERIFY(!ThemePresetManager::parsePreset(
        presetJson(QStringLiteral("x"), QStringLiteral("x"), QStringLiteral("not-a-color"), 6,
                   QStringLiteral("OutQuint")),
        &preset, &error));
}

void TstThemePreset::applyToObjectWritesTokensAndReportsDrift()
{
    ThemePreset preset;
    QString error;
    QVERIFY(ThemePresetManager::parsePreset(
        presetJson(QStringLiteral("theme9"), QStringLiteral("测试"), QStringLiteral("#FF3B82F6"), 14,
                   QStringLiteral("OutQuint")),
        &preset, &error));

    ThemePresetManager manager(nullptr, QString());
    TokenTarget target;

    const QStringList missing = manager.applyToObject(&target, preset);
    QCOMPARE(target.ink0(), QColor(QStringLiteral("#FF3B82F6")));
    QCOMPARE(target.rMd(), 14);
    QCOMPARE(target.easeOut(), static_cast<int>(QEasingCurve::OutQuint));

    // 目标对象未识别的令牌必须被显式报告（契约漂移），而不是静默漏色
    QVERIFY(missing.contains(QStringLiteral("extraToken")));
}

void TstThemePreset::managerLoadsPresetsAndPersistsSelection()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QDir dir(tmp.path());
    const QString presetsDir = dir.filePath(QStringLiteral("presets"));
    QVERIFY(QDir().mkpath(presetsDir));
    QVERIFY(writeFile(QDir(presetsDir).filePath(QStringLiteral("theme1.json")),
                      presetJson(QStringLiteral("theme1"), QStringLiteral("深色"),
                                 QStringLiteral("#FFF2A63C"), 10, QStringLiteral("OutQuint"))));
    QVERIFY(writeFile(QDir(presetsDir).filePath(QStringLiteral("theme2.json")),
                      presetJson(QStringLiteral("theme2"), QStringLiteral("浅蓝"),
                                 QStringLiteral("#FF3B82F6"), 10, QStringLiteral("OutQuint"))));

    const QString config = dir.filePath(QStringLiteral("config.json"));
    ThemeStore store(config);

    {
        ThemePresetManager manager(&store, presetsDir);
        manager.load();
        QCOMPARE(manager.presets().size(), 2);
        QCOMPARE(manager.currentPresetId(), QStringLiteral("theme1"));
        QVERIFY(manager.lastError().isEmpty());

        TokenTarget target;
        manager.attachThemeObject(&target);
        QCOMPARE(target.ink0(), QColor(QStringLiteral("#FFF2A63C")));

        QVERIFY(manager.selectPreset(QStringLiteral("theme2")));
        QCOMPARE(manager.currentPresetId(), QStringLiteral("theme2"));
        QCOMPARE(target.ink0(), QColor(QStringLiteral("#FF3B82F6")));
    }

    // config.json 只保留 theme 段，且不破坏其他键
    QJsonObject root;
    QVERIFY(writeFile(config, QByteArray()));  // 先清空，验证写入的确由 selectPreset 完成
    {
        ThemePresetManager manager(&store, presetsDir);
        manager.load();
        QVERIFY(manager.selectPreset(QStringLiteral("theme2")));
    }
    {
        QFile file(config);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        file.close();
        QVERIFY(document.isObject());
        root = document.object();
    }
    QCOMPARE(root.value(QStringLiteral("theme"))
                  .toObject()
                  .value(QStringLiteral("preset"))
                  .toString(),
             QStringLiteral("theme2"));

    // 重启（新建 manager）后选择保持
    ThemePresetManager restarted(&store, presetsDir);
    restarted.load();
    QCOMPARE(restarted.currentPresetId(), QStringLiteral("theme2"));
}

void TstThemePreset::managerRejectsUnknownPreset()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString presetsDir = tmp.path();
    QVERIFY(writeFile(QDir(presetsDir).filePath(QStringLiteral("theme1.json")),
                      presetJson(QStringLiteral("theme1"), QStringLiteral("深色"),
                                 QStringLiteral("#FFF2A63C"), 10, QStringLiteral("OutQuint"))));

    ThemeStore store(QDir(presetsDir).filePath(QStringLiteral("config.json")));
    ThemePresetManager manager(&store, presetsDir);
    manager.load();
    QCOMPARE(manager.currentPresetId(), QStringLiteral("theme1"));

    QVERIFY(!manager.selectPreset(QStringLiteral("不存在")));
    QVERIFY(!manager.lastError().isEmpty());
    QCOMPARE(manager.currentPresetId(), QStringLiteral("theme1"));
}

void TstThemePreset::managerWithoutStoreKeepsDefault()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // 无预设目录 + 无存储：不崩溃、无预设、当前 id 为空
    ThemePresetManager manager(nullptr, QDir(tmp.path()).filePath(QStringLiteral("empty")));
    manager.load();
    QVERIFY(manager.presets().isEmpty());
    QVERIFY(manager.currentPresetId().isEmpty());
    QVERIFY(!manager.lastError().isEmpty());

    // 无 QML 单例：只记录错误，不崩溃
    manager.attachThemeObject(nullptr);
    QVERIFY(!manager.lastError().isEmpty());
}

QTEST_GUILESS_MAIN(TstThemePreset)
#include "tst_themepreset.moc"

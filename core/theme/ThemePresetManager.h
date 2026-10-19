#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QJsonValue;
class ThemeStore;

/**
 * 主题预设：一组**设计令牌的命名集合**。
 *
 * 预设只换值、不换名——令牌名是全局契约（见 docs/PROMPT.md 硬性约束），
 * 因此新增主题只需增加一个 JSON 预设文件，**零代码**。
 */
struct ThemePreset
{
    QString id;
    QString name;
    /// 令牌名 -> 已解析值（颜色为 QColor，尺寸 / 时长为 int，缓动为 QEasingCurve::Type 值）
    QVariantMap tokens;
};

/**
 * 主题预设管理器（core 层）。
 *
 * 职责边界（不与其他模块杂糅）：
 *  - 从资源目录扫描并解析预设 JSON；
 *  - 把选中预设的令牌**逐个写入 QML 单例 `Theme` 对象**（普通属性赋值会触发变更通知，
 *    绑定即时刷新），因此 `ui/Theme.qml` 的令牌必须是可变属性；
 *  - 选择经 `storage/ThemeStore` 持久化到 `config.json` 的 `theme` 段（与
 *    `personalization` 段**分开**），重启后保持。
 *
 * 本类**不依赖 Qt Qml 模块**：QML 单例对象由 main.cpp 解析后经 attachThemeObject 注入，
 * 保持 core 层不被 QML 类型污染，也便于用测试替身做单元测试。
 */
class ThemePresetManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPresetId READ currentPresetId NOTIFY currentPresetChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    ThemePresetManager(ThemeStore *store, QString presetRoot, QObject *parent = nullptr);
    ~ThemePresetManager() override;

    /// 扫描预设目录并读取已选主题 id（缺失 / 非法时回落到首个预设）
    void load();

    /// 注入 QML 单例 `Theme` 对象并立即应用当前预设（对象为空时只记错误，不崩溃）
    void attachThemeObject(QObject *themeObject);

    QString currentPresetId() const;
    QVariantList presets() const;
    QString lastError() const;

    /// 切换主题：应用 + 持久化；id 不存在时返回 false 并写 lastError
    Q_INVOKABLE bool selectPreset(const QString &id);

    /// 解析单个预设 JSON（静态，便于单元测试）
    static bool parsePreset(const QByteArray &json, ThemePreset *preset, QString *error);
    /// 解析单个令牌值：`#AARRGGBB` -> QColor，数字 -> int，其它字符串 -> 缓动类型
    static QVariant tokenValue(const QJsonValue &value, bool *ok);

    /// 把预设令牌写入目标对象；返回目标对象**未识别**的令牌名（用于发现契约漂移）
    QStringList applyToObject(QObject *target, const ThemePreset &preset) const;

signals:
    void currentPresetChanged();
    void presetsChanged();
    void lastErrorChanged();

private:
    void setLastError(const QString &message);
    void applyCurrentPreset();
    const ThemePreset *findPreset(const QString &id) const;

    ThemeStore *m_store = nullptr;
    QString m_presetRoot;
    QList<ThemePreset> m_presets;
    QString m_currentId;
    QString m_lastError;
    QObject *m_themeObject = nullptr;
};

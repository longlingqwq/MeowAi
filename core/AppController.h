#pragma once

#include <QObject>
#include <QString>

#include <memory>

class AppPaths;
class ChatController;
class Database;
class DocumentRetriever;
class PlatformAdapter;
class PersonalizationStore;
class SecureStore;
class SkillRegistry;
class ThemeManager;
class UpdateManager;

/**
 * 顶层门面（AppController）：
 * UI(QML) 只与本对象交互，由它按层调用 storage / platform / providers / core 模块，
 * 保证“UI 不写业务逻辑、不直连数据库”（见 docs/AGENT.md §2）。
 */
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString platformId READ platformId CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    QString appName() const;
    QString platformId() const;
    QString statusMessage() const;

    /// 准备本地目录、数据库与内置 Skill；任何失败都只体现在 statusMessage，不崩溃
    Q_INVOKABLE void initialize();

    ThemeManager *themeManager() const;
    UpdateManager *updateManager() const;
    /// 个性化配置存储（功能 #11）；只读写 config.json 的 personalization 段
    PersonalizationStore *personalizationStore() const;
    /// 敏感数据保险库（功能 #5）；初始化后可用，失败时为 nullptr
    SecureStore *secureStore() const;
    /// 对话控制器（功能 #3 / #4）；初始化后可用，失败时为 nullptr
    ChatController *chatController() const;

signals:
    void statusMessageChanged();

private:
    void setStatusMessage(const QString &message);

    PlatformAdapter *m_platform = nullptr;
    std::unique_ptr<AppPaths> m_paths;
    std::unique_ptr<Database> m_database;
    std::unique_ptr<SkillRegistry> m_skills;
    std::unique_ptr<DocumentRetriever> m_retriever;
    std::unique_ptr<PersonalizationStore> m_personalizationStore;
    std::unique_ptr<SecureStore> m_secureStore;
    std::unique_ptr<ChatController> m_chat;
    ThemeManager *m_themeManager = nullptr;
    UpdateManager *m_updateManager = nullptr;
    QString m_statusMessage;
};

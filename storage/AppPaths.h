#pragma once

#include <QString>
#include <QStringList>

class PlatformAdapter;

/**
 * 本地目录布局（见 docs/PROJECT_CHARTER.md §6）：
 *
 *   <AppData>/
 *     config.json     # 模型配置（Key 加密）与个性化配置
 *     library/        # 资料库原始文件
 *     categories/     # AI 生成的分类文件 (.json)
 *     backups/        # AI 修改前的备份文件
 *     skills/         # 内置 Skill 文档 (.md + manifest)
 *     assets/         # 用户自定义资源（头像、背景图、字体库）
 */
class AppPaths
{
public:
    explicit AppPaths(QString root);

    /// 由平台适配器推导根目录
    static AppPaths fromPlatform(const PlatformAdapter &adapter);

    QString root() const;
    QString configFile() const;
    QString libraryDir() const;
    QString categoriesDir() const;
    QString backupsDir() const;
    QString skillsDir() const;
    QString assetsDir() const;

    /// 需要保证存在的目录集合（config.json 是文件，不在其中）
    QStringList requiredDirectories() const;

    /// 创建全部目录；任一步失败即返回 false 并写入 error，绝不抛异常
    bool ensureLayout(QString *error) const;

private:
    QString m_root;
};

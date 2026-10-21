#pragma once

#include "storage/AppPaths.h"

#include <QString>
#include <QStringList>

/**
 * 文件分类骨架（功能 #7）：上传时由 AI 粗览生成分类文件并保存到 categories/。
 * 阶段一只固定接口，不发起 AI 请求。
 */
class CategoryManager
{
public:
    explicit CategoryManager(AppPaths paths);

    /// 读取某文件的分类；阶段一返回空列表
    QStringList categoriesFor(const QString &fileId) const;

    /// 请求 AI 粗览并生成分类文件；阶段一未实现，返回 false
    bool generateCategories(const QString &fileId, QString *error);

private:
    AppPaths m_paths;
};

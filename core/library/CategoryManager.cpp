#include "core/library/CategoryManager.h"

#include <utility>

CategoryManager::CategoryManager(AppPaths paths)
    : m_paths(std::move(paths))
{
}

QStringList CategoryManager::categoriesFor(const QString &fileId) const
{
    Q_UNUSED(fileId)
    return {};
}

bool CategoryManager::generateCategories(const QString &fileId, QString *error)
{
    Q_UNUSED(fileId)
    if (error) {
        *error = QStringLiteral("分类生成尚未实现（阶段二接入）");
    }
    return false;
}

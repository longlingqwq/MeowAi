#pragma once

#include <QSqlDatabase>
#include <QString>

/**
 * SQLite 访问封装（元数据 / 索引）。
 *
 * 约束：UI(QML) 不得直连数据库，数据库只允许被 core / storage 内部使用；
 * 所有失败路径都返回 false + error，不抛异常、不崩溃。
 */
class Database
{
public:
    Database();
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// 打开（或新建）数据库文件；父目录不存在时尝试创建
    bool open(const QString &dbPath, QString *error);
    void close();
    bool isOpen() const;

    /// 建立最小 schema；阶段二再按模块扩展表结构
    bool ensureSchema(QString *error);

    /// FTS5 全文索引是否可用（`ensureSchema` 调用后有效）。
    /// 不可用时资料库的关键词检索由 LibraryStore 降级为 LIKE 扫描。
    bool fts5Available() const;

    QSqlDatabase handle() const;

private:
    QString m_connectionName;
    bool m_open = false;
    bool m_fts5Available = false;
};

#include "storage/AppPaths.h"
#include "storage/Database.h"

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

/**
 * 存储层边界测试：目录缺失、非法根路径、数据库不可打开等异常流不得崩溃。
 */
class TstStorage : public QObject
{
    Q_OBJECT

private slots:
    void ensureLayoutCreatesAllDirectories();
    void ensureLayoutRejectsEmptyRoot();
    void ensureLayoutFailsOnBlockedRoot();
    void databaseOpenRejectsEmptyPath();
    void databaseOpenFailsGracefullyOnBlockedPath();
    void databaseSchemaRoundTrip();
    void schemaV2CreatesLibraryTables();
    void schemaV2MigrationIsIdempotentAndKeepsV1Tables();
};

void TstStorage::ensureLayoutCreatesAllDirectories()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const AppPaths paths(QDir(tmp.path()).filePath(QStringLiteral("meowai")));
    QString error;
    QVERIFY2(paths.ensureLayout(&error), qPrintable(error));

    QVERIFY(QDir(paths.libraryDir()).exists());
    QVERIFY(QDir(paths.categoriesDir()).exists());
    QVERIFY(QDir(paths.backupsDir()).exists());
    QVERIFY(QDir(paths.skillsDir()).exists());
    QVERIFY(QDir(paths.assetsDir()).exists());
}

void TstStorage::ensureLayoutRejectsEmptyRoot()
{
    const AppPaths paths{QString()};
    QString error;
    QVERIFY(!paths.ensureLayout(&error));
    QVERIFY(!error.isEmpty());
}

void TstStorage::ensureLayoutFailsOnBlockedRoot()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // 用一个普通文件占住父路径，制造“无法创建目录”的异常场景
    const QString blocker = QDir(tmp.path()).filePath(QStringLiteral("blocker"));
    QFile file(blocker);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    const AppPaths paths(blocker + QStringLiteral("/sub"));
    QString error;
    QVERIFY(!paths.ensureLayout(&error));
    QVERIFY(!error.isEmpty());
}

void TstStorage::databaseOpenRejectsEmptyPath()
{
    Database db;
    QString error;
    QVERIFY(!db.open(QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!db.isOpen());
}

void TstStorage::databaseOpenFailsGracefullyOnBlockedPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString blocker = QDir(tmp.path()).filePath(QStringLiteral("blocker"));
    QFile file(blocker);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    Database db;
    QString error;
    QVERIFY(!db.open(blocker + QStringLiteral("/nested.db"), &error));
    QVERIFY(!db.isOpen());
}

void TstStorage::databaseSchemaRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error), qPrintable(error));
    QVERIFY(db.isOpen());
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));

    // schema 未就绪时必须给出错误而不是崩溃
    Database closed;
    QString closedError;
    QVERIFY(!closed.ensureSchema(&closedError));
    QVERIFY(!closedError.isEmpty());
}

void TstStorage::schemaV2CreatesLibraryTables()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Database db;
    QString error;
    QVERIFY2(db.open(QDir(tmp.path()).filePath(QStringLiteral("meta.db")), &error), qPrintable(error));
    QVERIFY2(db.ensureSchema(&error), qPrintable(error));

    QSqlQuery query(db.handle());
    QVERIFY2(query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'")),
             qPrintable(query.lastError().text()));
    QStringList tables;
    while (query.next())
        tables.append(query.value(0).toString());

    const QStringList required = { QStringLiteral("collections"), QStringLiteral("library_files"),
                                   QStringLiteral("file_collections"), QStringLiteral("folder_links"),
                                   QStringLiteral("chunks"), QStringLiteral("chunk_vectors"),
                                   QStringLiteral("citations") };
    for (const QString &table : required) {
        QVERIFY2(tables.contains(table), qPrintable(QStringLiteral("缺少资料库表：%1").arg(table)));
    }

    // FTS 索引表是否存在，必须与探测结果一致：不可用时关键词检索降级为 LIKE
    QCOMPARE(tables.contains(QStringLiteral("chunk_fts")), db.fts5Available());
    qInfo("SQLite FTS5 available: %s", db.fts5Available() ? "yes" : "no (LIKE fallback)");

    QSqlQuery version(db.handle());
    QVERIFY(version.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'")));
    QVERIFY(version.next());
    // v2 测试只要求库至少完成 v2 迁移；后续迁移（v3/v4）会继续抬高版本号
    QVERIFY(version.value(0).toInt() >= 2);
}

void TstStorage::schemaV2MigrationIsIdempotentAndKeepsV1Tables()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString path = QDir(tmp.path()).filePath(QStringLiteral("meta.db"));
    QString error;

    {
        Database db;
        QVERIFY2(db.open(path, &error), qPrintable(error));
        QVERIFY2(db.ensureSchema(&error), qPrintable(error));
        // 重复执行必须幂等（应用每次启动都会调用）
        QVERIFY2(db.ensureSchema(&error), qPrintable(error));
    }

    // 重新打开同一文件：v2 已是当前版本，迁移应直接通过且能力探测仍然正确
    Database reopened;
    QVERIFY2(reopened.open(path, &error), qPrintable(error));
    QVERIFY2(reopened.ensureSchema(&error), qPrintable(error));

    QSqlQuery query(reopened.handle());
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'")));
    QStringList tables;
    while (query.next())
        tables.append(query.value(0).toString());

    // v1 的会话历史表不得因迁移被删除
    QVERIFY(tables.contains(QStringLiteral("conversations")));
    QVERIFY(tables.contains(QStringLiteral("messages")));
    QVERIFY(tables.contains(QStringLiteral("message_attachments")));
    QVERIFY(tables.contains(QStringLiteral("collections")));

    // 入库可用的片段表确实可写（确保迁移不只是建了空壳）
    QSqlQuery insert(reopened.handle());
    QVERIFY(insert.exec(QStringLiteral("INSERT INTO library_files (display_name, source_kind,"
                                       " source_path, kind, created_at)"
                                       " VALUES ('a.txt', 'upload', '/tmp/a.txt', 'text', 1)")));
    QVERIFY(insert.exec(QStringLiteral("INSERT INTO chunks (file_id, ordinal, text, created_at)"
                                       " VALUES (1, 0, '预算表', 1)")));
}

QTEST_GUILESS_MAIN(TstStorage)
#include "tst_storage.moc"

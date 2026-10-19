#include "storage/AppPaths.h"
#include "storage/Database.h"

#include <QDir>
#include <QFile>
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

QTEST_GUILESS_MAIN(TstStorage)
#include "tst_storage.moc"

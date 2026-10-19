#include "storage/Database.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

} // namespace

Database::Database()
    : m_connectionName(QStringLiteral("meowai_%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

Database::~Database()
{
    close();
}

bool Database::open(const QString &dbPath, QString *error)
{
    if (dbPath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("数据库路径为空"));
        return false;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setError(error, QStringLiteral("缺少 QSQLITE 驱动，无法打开数据库"));
        return false;
    }

    const QFileInfo info(dbPath);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, QStringLiteral("无法创建数据库目录：%1").arg(info.absolutePath()));
        return false;
    }

    close();

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        const QString reason = db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        setError(error, QStringLiteral("打开数据库失败：%1").arg(reason));
        return false;
    }

    m_open = true;
    return true;
}

void Database::close()
{
    if (!m_open) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, /*open=*/false);
        if (db.isValid() && db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
}

bool Database::isOpen() const
{
    return m_open;
}

bool Database::ensureSchema(QString *error)
{
    if (!m_open) {
        setError(error, QStringLiteral("数据库尚未打开"));
        return false;
    }

    QSqlQuery query(QSqlDatabase::database(m_connectionName, /*open=*/false));
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT"
        ")");

    if (!query.exec(sql)) {
        setError(error, QStringLiteral("创建 schema 失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connectionName, /*open=*/false);
}

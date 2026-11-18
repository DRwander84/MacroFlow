#include "DatabaseManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace mmr {

QString DatabaseManager::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/records.db";
}

DatabaseManager::DatabaseManager(QString path)
    : path_(std::move(path)), connection_("mmr_" + QUuid::createUuid().toString(QUuid::Id128)) {}

DatabaseManager::~DatabaseManager() {
    {
        QSqlDatabase db = QSqlDatabase::database(connection_, false);
        if (db.isValid() && db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(connection_);
}

bool DatabaseManager::tryOpen(QString* error) {
    {
        QSqlDatabase old = QSqlDatabase::database(connection_, false);
        if (old.isValid()) { old.close(); }
    }
    if (QSqlDatabase::contains(connection_)) QSqlDatabase::removeDatabase(connection_);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection_);
    db.setDatabaseName(path_);
    if (!db.open()) {
        if (error) *error = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    // Header check: fails immediately on a non-SQLite / damaged file.
    if (!q.exec("PRAGMA user_version") || !q.next()) {
        if (error) *error = q.lastError().text();
        return false;
    }
    const int version = q.value(0).toInt();
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");
    if (version > 1) {
        if (error) *error = "Database was created by a newer version of the application.";
        return false;
    }
    if (version < 1) {
        if (!db.transaction()) { if (error) *error = db.lastError().text(); return false; }
        const bool ok = q.exec(
            "CREATE TABLE IF NOT EXISTS records ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "duration_us INTEGER NOT NULL,"
            "event_count INTEGER NOT NULL,"
            "format_version INTEGER NOT NULL,"
            "monitor_geometry TEXT NOT NULL DEFAULT '',"
            "playback_meta TEXT NOT NULL DEFAULT '',"
            "event_data BLOB NOT NULL)") && q.exec("PRAGMA user_version=1");
        if (!ok) { if (error) *error = q.lastError().text(); db.rollback(); return false; }
        db.commit();
    }
    // The table must exist and be readable.
    if (!q.exec("SELECT id, name, created_at, duration_us, event_count, format_version FROM records LIMIT 1")) {
        if (error) *error = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::open(QString* error) {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QString err;
    if (tryOpen(&err)) return true;

    // Corrupt or unreadable: keep the old file for manual recovery, start fresh.
    QSqlDatabase::database(connection_, false).close();
    QSqlDatabase::removeDatabase(connection_);
    const QString backup = path_ + ".corrupt-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QFile::remove(backup);
    if (QFile::exists(path_) && !QFile::rename(path_, backup)) {
        if (error) *error = "Records database is unreadable and could not be moved aside: " + err;
        return false;
    }
    QFile::remove(path_ + "-wal");
    QFile::remove(path_ + "-shm");
    QString err2;
    if (!tryOpen(&err2)) {
        if (error) *error = "Could not create a new records database: " + err2;
        return false;
    }
    recovered_ = true;
    return true;
}

}  // namespace mmr

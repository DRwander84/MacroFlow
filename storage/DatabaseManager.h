#pragma once
#include <QSqlDatabase>
#include <QString>

namespace mmr {

// Owns the SQLite connection (records only; user settings live in a separate file).
class DatabaseManager {
public:
    static QString defaultPath();
    explicit DatabaseManager(QString path);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // Opens (and migrates) the database. A corrupt file is moved aside and replaced.
    bool open(QString* error);
    bool recoveredFromCorruption() const { return recovered_; }
    QSqlDatabase database() const { return QSqlDatabase::database(connection_, false); }
    QString path() const { return path_; }

private:
    bool tryOpen(QString* error);
    QString path_, connection_;
    bool recovered_ = false;
};

}  // namespace mmr

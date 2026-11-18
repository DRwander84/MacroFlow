#include "MacroRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace mmr {
namespace {
constexpr int kMaxName = 100;

RecordMeta fromQuery(const QSqlQuery& q) {
    RecordMeta m;
    m.id = q.value(0).toLongLong();
    m.name = q.value(1).toString();
    m.created = QDateTime::fromMSecsSinceEpoch(q.value(2).toLongLong());
    m.durationUs = q.value(3).toLongLong();
    m.eventCount = q.value(4).toInt();
    m.formatVersion = q.value(5).toInt();
    m.monitorGeometry = q.value(6).toString();
    m.playbackMeta = q.value(7).toString();
    return m;
}
const char* kMetaCols = "id, name, created_at, duration_us, event_count, format_version, monitor_geometry, playback_meta";
}  // namespace

QString MacroRepository::normalizeName(const QString& name) {
    QString n = name.simplified();
    if (n.size() > kMaxName) n.truncate(kMaxName);
    return n;
}

std::optional<qint64> MacroRepository::insert(const QString& name, const std::vector<MacroEvent>& events,
                                              qint64 durationUs, const QString& geometry, const QString& meta,
                                              QString* error) {
    QSqlDatabase db = db_.database();
    const QString n = normalizeName(name);
    if (n.isEmpty()) { if (error) *error = "Name cannot be empty."; return std::nullopt; }
    if (events.empty()) { if (error) *error = "Nothing to save."; return std::nullopt; }
    const auto bytes = serializeEvents(events);
    if (!db.transaction()) { if (error) *error = db.lastError().text(); return std::nullopt; }
    QSqlQuery q(db);
    q.prepare("INSERT INTO records (name, created_at, duration_us, event_count, format_version, monitor_geometry, "
              "playback_meta, event_data) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(n);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(durationUs);
    q.addBindValue(int(events.size()));
    q.addBindValue(kFormatVersion);
    q.addBindValue(geometry);
    q.addBindValue(meta);
    q.addBindValue(QByteArray(reinterpret_cast<const char*>(bytes.data()), int(bytes.size())));
    if (!q.exec()) {
        if (error) *error = q.lastError().text();
        db.rollback();
        return std::nullopt;
    }
    const qint64 id = q.lastInsertId().toLongLong();
    if (!db.commit()) { if (error) *error = db.lastError().text(); db.rollback(); return std::nullopt; }
    return id;
}

QList<RecordMeta> MacroRepository::list(QString* error) {
    QList<RecordMeta> out;
    QSqlQuery q(db_.database());
    if (!q.exec(QString("SELECT %1 FROM records ORDER BY created_at DESC, id DESC").arg(kMetaCols))) {
        if (error) *error = q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(fromQuery(q));
    return out;
}

std::optional<RecordMeta> MacroRepository::meta(qint64 id, QString* error) {
    QSqlQuery q(db_.database());
    q.prepare(QString("SELECT %1 FROM records WHERE id = ?").arg(kMetaCols));
    q.addBindValue(id);
    if (!q.exec()) { if (error) *error = q.lastError().text(); return std::nullopt; }
    if (!q.next()) { if (error) *error = "Record not found."; return std::nullopt; }
    return fromQuery(q);
}

bool MacroRepository::loadEvents(qint64 id, std::vector<MacroEvent>& out, QString* error) {
    out.clear();
    QSqlQuery q(db_.database());
    q.prepare("SELECT event_data, format_version, event_count FROM records WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec()) { if (error) *error = q.lastError().text(); return false; }
    if (!q.next()) { if (error) *error = "Record not found."; return false; }
    const QByteArray blob = q.value(0).toByteArray();
    std::string err;
    if (!deserializeEvents(reinterpret_cast<const uint8_t*>(blob.constData()), size_t(blob.size()),
                           q.value(1).toInt(), out, &err)) {
        if (error) *error = "This record is damaged and cannot be played: " + QString::fromStdString(err);
        return false;
    }
    if (int(out.size()) != q.value(2).toInt()) {
        out.clear();
        if (error) *error = "This record is damaged: event count mismatch.";
        return false;
    }
    return true;
}

bool MacroRepository::rename(qint64 id, const QString& newName, QString* error) {
    const QString n = normalizeName(newName);
    if (n.isEmpty()) { if (error) *error = "Name cannot be empty."; return false; }
    QSqlQuery q(db_.database());
    q.prepare("UPDATE records SET name = ? WHERE id = ?");
    q.addBindValue(n);
    q.addBindValue(id);
    if (!q.exec()) { if (error) *error = q.lastError().text(); return false; }
    return q.numRowsAffected() > 0;
}

bool MacroRepository::remove(qint64 id, QString* error) {
    QSqlQuery q(db_.database());
    q.prepare("DELETE FROM records WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec()) { if (error) *error = q.lastError().text(); return false; }
    return q.numRowsAffected() > 0;
}

}  // namespace mmr

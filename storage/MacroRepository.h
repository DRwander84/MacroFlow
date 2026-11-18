#pragma once
#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>
#include <vector>

#include "DatabaseManager.h"
#include "core/MacroTypes.h"

namespace mmr {

struct RecordMeta {
    qint64 id = 0;
    QString name;
    QDateTime created;
    qint64 durationUs = 0;  // ACTIVE recording time (pauses excluded)
    int eventCount = 0;
    int formatVersion = kFormatVersion;
    QString monitorGeometry;
    QString playbackMeta;
};

class MacroRepository {
public:
    explicit MacroRepository(DatabaseManager& db) : db_(db) {}

    std::optional<qint64> insert(const QString& name, const std::vector<MacroEvent>& events, qint64 durationUs,
                                 const QString& monitorGeometry, const QString& playbackMeta, QString* error);
    QList<RecordMeta> list(QString* error);  // metadata only, event blobs are not loaded
    std::optional<RecordMeta> meta(qint64 id, QString* error);
    bool loadEvents(qint64 id, std::vector<MacroEvent>& out, QString* error);  // validated
    bool rename(qint64 id, const QString& newName, QString* error);
    bool remove(qint64 id, QString* error);

    static QString normalizeName(const QString& name);

private:
    DatabaseManager& db_;
};

}  // namespace mmr

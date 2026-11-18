#include <QCoreApplication>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "TestSupport.h"
#include "storage/MacroRepository.h"
using namespace mmr;

static std::vector<MacroEvent> sample() {
    std::vector<MacroEvent> v;
    for (int i = 0; i < 100; ++i) { MacroEvent e; e.timeUs = i * 1000; e.x = -500 + i; e.y = i; v.push_back(e); }
    MacroEvent w; w.type = EventType::Wheel; w.delta = -120; w.timeUs = 100000; v.push_back(w);
    return v;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath("records.db");
    QString err;
    {
        DatabaseManager db(path);
        CHECK(db.open(&err));
        MacroRepository repo(db);
        auto id = repo.insert("  My   macro ", sample(), 100000, "v0,0,1920,1080", "loop=infinite", &err);
        CHECK(id.has_value());
        auto list = repo.list(&err);
        CHECK(list.size() == 1 && list[0].name == "My macro" && list[0].eventCount == 101);
        std::vector<MacroEvent> out;
        CHECK(repo.loadEvents(*id, out, &err) && out == sample());            // save + restore
        CHECK(repo.rename(*id, "Renamed", &err) && repo.meta(*id, &err)->name == "Renamed");
        CHECK(!repo.rename(*id, "   ", &err));                                // empty name rejected
        // corrupt the blob -> must be refused, not crash
        QSqlQuery q(db.database());
        CHECK(q.exec(QString("UPDATE records SET event_data = x'0102030405' WHERE id=%1").arg(*id)));
        CHECK(!repo.loadEvents(*id, out, &err) && out.empty());
        CHECK(repo.remove(*id, &err));
        CHECK(repo.list(&err).isEmpty());
    }
    {   // persistence across "restart"
        DatabaseManager db(path);
        CHECK(db.open(&err));
        MacroRepository repo(db);
        auto id = repo.insert("persist", sample(), 1, "", "", &err);
        CHECK(id.has_value());
    }
    {
        DatabaseManager db(path);
        CHECK(db.open(&err));
        MacroRepository repo(db);
        CHECK(repo.list(&err).size() == 1);
    }
    {   // garbage file -> recovered with a fresh DB, old file kept
        QFile f(path); CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write(QByteArray(4096, 'x')); f.close();
        DatabaseManager db(path);
        CHECK(db.open(&err));
        CHECK(db.recoveredFromCorruption());
        MacroRepository repo(db);
        CHECK(repo.list(&err).isEmpty());
    }
    TEST_MAIN_END();
}

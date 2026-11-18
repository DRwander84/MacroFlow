#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <vector>

#include "core/ShortcutTypes.h"

namespace mmr {

// RegisterHotKey based global shortcuts. Registration is transactional: if any new combo
// fails, everything is rolled back to the previously working set.
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter, public IHotkeyBackend {
    Q_OBJECT
public:
    explicit GlobalHotkeys(QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    bool apply(const BindingMap& bindings, std::string* error) override;
    void setSuspended(bool suspended) override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void triggered(int action);  // ShortcutAction as int

private:
    struct Reg {
        int id = 0;
        KeyCombo combo;
        std::vector<ShortcutAction> actions;
    };
    bool registerSet(const BindingMap& bindings, std::string* error);
    void unregisterAll();

    std::vector<Reg> regs_;
    BindingMap current_;
    bool suspended_ = false;
    int nextId_ = 1;
};

}  // namespace mmr

#pragma once
#include <QAbstractNativeEventFilter>
#include <QWidget>

namespace mmr {

// Small glowing status dot drawn over the bottom-right corner of the app's taskbar icon
// (ITaskbarList3 overlay icon). Green = recording, yellow = recording paused, nothing = otherwise.
// Auto Clicker: a mouse-pointer icon while it clicks, the same pointer crossed by a red slash while it is paused.
class TaskbarBadge final : public QAbstractNativeEventFilter {
public:
    enum class Mode { None, Recording, Paused, AutoClick, AutoClickPaused };

    explicit TaskbarBadge(QWidget* window);
    ~TaskbarBadge() override;
    TaskbarBadge(const TaskbarBadge&) = delete;
    TaskbarBadge& operator=(const TaskbarBadge&) = delete;

    void set(Mode mode);  // UI thread; cheap no-op when the mode did not change

    // Explorer (re)creates the window's taskbar button after the first show and after an Explorer restart:
    // the overlay is applied again then.
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    bool apply();
    void* icon(Mode mode);  // HICON, created lazily and cached

    QWidget* window_;
    void* taskbar_ = nullptr;  // ITaskbarList3*
    void* icons_[4] = {nullptr, nullptr, nullptr, nullptr};
    unsigned buttonCreatedMsg_ = 0;
    Mode mode_ = Mode::None;
    bool filterAllowed_ = false;
};

}  // namespace mmr

#pragma once
#include <QObject>
#include <vector>

#include "core/MacroTypes.h"
#include "core/MarkerBuilder.h"

namespace mmr {

class OverlayWidget;

// Optional, click-through, always-on-top markers (one transparent window per monitor).
// Coordinates are physical virtual-desktop pixels, exactly like recorded events.
class ClickOverlay : public QObject {
    Q_OBJECT
public:
    explicit ClickOverlay(QObject* parent = nullptr);
    ~ClickOverlay() override;

public slots:
    void beginLive();                                    // recording started: clear + show
    void feed(const mmr::MacroEvent& e);                 // live event while recording
    void showRecord(const std::vector<mmr::MacroEvent>& events);  // preview all markers of a record
    void hideAll();                                      // hide + free everything

private:
    void createWidgets();
    void destroyWidgets();
    void repaintMarker(size_t index);

    MarkerBuilder builder_;
    std::vector<OverlayWidget*> widgets_;
    bool visible_ = false;
};

}  // namespace mmr

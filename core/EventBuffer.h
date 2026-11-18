#pragma once
#include <memory>
#include <vector>

#include "MacroTypes.h"

namespace mmr {

// Chunked append-only buffer. push() never copies previously stored events and only
// allocates once per kChunk events, which keeps the low-level hook callback cheap.
class EventBuffer {
public:
    static constexpr size_t kChunk = 8192;
    explicit EventBuffer(size_t maxEvents = 3'000'000) : maxEvents_(maxEvents) {}

    bool push(const MacroEvent& e);
    size_t size() const { return size_; }
    bool full() const { return size_ >= maxEvents_; }
    void clear();
    std::vector<MacroEvent> take();  // flatten + clear

private:
    std::vector<std::unique_ptr<MacroEvent[]>> chunks_;
    size_t size_ = 0;
    size_t maxEvents_;
};

}  // namespace mmr

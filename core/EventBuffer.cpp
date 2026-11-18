#include "EventBuffer.h"

namespace mmr {

bool EventBuffer::push(const MacroEvent& e) {
    if (size_ >= maxEvents_) return false;
    if (size_ % kChunk == 0) chunks_.push_back(std::make_unique<MacroEvent[]>(kChunk));
    chunks_.back()[size_ % kChunk] = e;
    ++size_;
    return true;
}

void EventBuffer::clear() {
    chunks_.clear();
    size_ = 0;
}

std::vector<MacroEvent> EventBuffer::take() {
    std::vector<MacroEvent> out;
    out.reserve(size_);
    for (size_t i = 0; i < size_; ++i) out.push_back(chunks_[i / kChunk][i % kChunk]);
    clear();
    return out;
}

}  // namespace mmr

#include "observed_memory.h"
#include <algorithm>
#include <stdexcept>

void ObservedMemoryDevice::ObserveWord(u32 offset, Observer observer) {
    if (uint64_t(offset) + 4 > size) throw std::invalid_argument("observed word outside device");
    wordOffset_ = offset;
    observer_ = std::move(observer);
}

u32 ObservedMemoryDevice::ObservedWord() const {
    u32 value;
    std::memcpy(&value, memAddress + wordOffset_, 4);
    return value;
}

bool ObservedMemoryDevice::Overlaps(u32 offset, u32 length) const {
    return observer_ && length && offset < wordOffset_ + 4 && uint64_t(offset) + length > wordOffset_;
}

void ObservedMemoryDevice::Notify(u32 offset, u32 length, const void* data, bool write, u32 before) {
    const u32 first = std::max(offset, wordOffset_), last = std::min(offset + length, wordOffset_ + 4);
    const auto* bytes = static_cast<const u8*>(data) + (first - offset);
    observer_({baseAddress + offset, length, before, ObservedWord(), write, false,
               baseAddress + first, std::vector<u8>(bytes, bytes + (last - first))});
}

void ObservedMemoryDevice::Read(u32 offset, void* buffer, u32 length) {
    MemoryDevice::Read(offset, buffer, length);
    if (Overlaps(offset, length)) Notify(offset, length, buffer, false, ObservedWord());
}

void ObservedMemoryDevice::Write(u32 offset, const void* buffer, u32 length) {
    if (!Overlaps(offset, length)) { MemoryDevice::Write(offset, buffer, length); return; }
    const auto before = ObservedWord();
    MemoryDevice::Write(offset, buffer, length);
    Notify(offset, length, buffer, true, before);
}

u32 ObservedMemoryDevice::Read32(u32 offset) {
    const auto value = MemoryDevice::Read32(offset);
    if (Overlaps(offset, 4)) Notify(offset, 4, &value, false, ObservedWord());
    return value;
}

void ObservedMemoryDevice::Write32(u32 offset, u32 value) {
    if (!Overlaps(offset, 4)) { MemoryDevice::Write32(offset, value); return; }
    const auto before = ObservedWord();
    MemoryDevice::Write32(offset, value);
    Notify(offset, 4, &value, true, before);
}

void* ObservedMemoryDevice::Map(u32 offset) {
    ++mappingExposures_;
    // A raw pointer may subsequently change any part of this region without
    // dispatch. Report exposure; do not claim a complete history in that case.
    if (observer_) {
        const auto value = ObservedWord();
        observer_({baseAddress + offset, 0, value, value, false, true, baseAddress + wordOffset_, {}});
    }
    return MemoryDevice::Map(offset);
}

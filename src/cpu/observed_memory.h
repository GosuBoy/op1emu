#pragma once
#include "io.h"
#include <cstring>

// Passive storage observation, not a hardware register model. Keep the same
// byte/lane behavior as MemoryDevice. No masks, clocks or readback overrides.
class ObservedMemoryDevice : public MemoryDevice {
public:
    using MemoryDevice::MemoryDevice;
    struct Access {
        u32 address, bytes, before, after;
        bool write, mapping;
        u32 overlapAddress;
        std::vector<u8> data; // only overlapping bytes, in ascending address order
    };
    using Observer = std::function<void(const Access&)>;
    void ObserveWord(u32 offset, Observer observer);
    u32 ObservedWord() const;
    uint64_t MappingExposures() const { return mappingExposures_; }
    void Read(u32 offset, void* buffer, u32 length) override;
    void Write(u32 offset, const void* buffer, u32 length) override;
    u32 Read32(u32 offset) override;
    void Write32(u32 offset, u32 value) override;
    void* Map(u32 offset) override;
private:
    bool Overlaps(u32 offset, u32 length) const;
    void Notify(u32 offset, u32 length, const void* data, bool write, u32 before);
    u32 wordOffset_ = 0;
    Observer observer_;
    uint64_t mappingExposures_ = 0;
};

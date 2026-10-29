#pragma once

#include "common.h"
#include <array>
#include <deque>
#include <functional>
#include <optional>

class TimeSource;

// Bounded, digital primary SPORT TX model. See docs/sport-transmit.md for
// the hardware contract and the deliberately explicit timing assumptions.
class SportTxTransport {
public:
    struct Word {
        uint64_t ns, index;
        u32 value;
        u8 bits, slot;
        bool lsbFirst;
        bool repeated, valid;
    };
    struct Snapshot {
        uint64_t accepted, transmitted, underflows, overflows;
        size_t fifo;
        bool holding, shifting, supported;
        u16 status;
    };
    explicit SportTxTransport(const TimeSource& time);
    u32 Read(u32 offset);
    void Write(u32 offset, u32 value);
    void Advance();
    uint64_t NextEventNs() const;
    u32 Take(u32 bytes) const;
    u32 Accept(const void* data, u32 bytes);
    Snapshot Inspect();
    struct ExternalInputs { u32 bitHz, frameClocks; bool clock, sync; };
    ExternalInputs InspectExternalInputs() const { return {externalHz_, externalFrameClocks_, clockPresent_, syncPresent_}; }

    // Synthetic input, never inferred from a dummy I2C ACK. frameClocks is
    // the full LRCLK period for stereo, or the TFS period for normal framing.
    void SetExternalClock(u32 bitHz, u32 frameClocks, bool clock = true, bool sync = true);
    void SetExternalSignals(bool clock, bool sync);
    void SetDMAEnabled(bool enabled);
    void SetWordCallback(std::function<void(const Word&)> cb) { wordCallback_ = std::move(cb); }
    void SetInterruptCallback(std::function<void(bool, bool)> cb) { interrupt_ = std::move(cb); Publish(); }
    void SetNotify(std::function<void()> cb) { notify_ = std::move(cb); }

private:
    unsigned Bits() const { return (tcr2_ & 31) + 1; }
    unsigned Bytes() const { return Bits() > 16 ? 4 : 2; }
    size_t Capacity() const { return Bits() > 16 ? 4 : 8; }
    bool Enabled() const { return tcr1_ & 1; }
    bool Stereo() const { return tcr2_ & 0x200; }
    bool Supported() const;
    uint64_t ClockHz() const;
    uint64_t ClockDenominator() const { return (tcr1_ & 2) ? 2 * (uint64_t(clkdiv_) + 1) : 1; }
    uint64_t SlotClocks() const;
    bool SyncPresent() const;
    uint64_t NextEdgeAction() const;
    uint64_t AtEdge(uint64_t edges) const;
    void Edge(uint64_t ns);
    void Publish();
    void Abort();
    bool Enqueue(u32 value);

    const TimeSource& time_;
    u16 tcr1_ = 0, tcr2_ = 0, clkdiv_ = 0, fsdiv_ = 0, mcmc1_ = 0, mcmc2_ = 0;
    u32 externalHz_ = 0, externalFrameClocks_ = 0;
    bool clockPresent_ = false, syncPresent_ = false, dmaEnabled_ = false;
    bool underflow_ = false, overflow_ = false;
    bool configurationValid_ = true;
    bool txLine_ = false, errorLine_ = false;
    std::deque<u32> fifo_;
    std::optional<u32> hold_, shift_;
    std::optional<u32> lastWord_;
    bool shiftRepeated_ = false, shiftValid_ = false;
    unsigned remaining_ = 0;
    u8 slot_ = 0, shiftSlot_ = 0;
    uint64_t lastNs_ = 0, fraction_ = 0, edge_ = 0, nextSlot_ = 1;
    uint64_t accepted_ = 0, transmitted_ = 0, underflows_ = 0, overflows_ = 0;
    std::function<void(const Word&)> wordCallback_;
    std::function<void(bool, bool)> interrupt_;
    std::function<void()> notify_;
};

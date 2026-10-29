#include "sport_tx.h"
#include "time_source.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
constexpr uint64_t kSecond = 1'000'000'000;
uint64_t Ceil(unsigned __int128 n, uint64_t d) {
    const auto result = (n + d - 1) / d;
    return result > UINT64_MAX ? UINT64_MAX : uint64_t(result);
}
}

SportTxTransport::SportTxTransport(const TimeSource& time) : time_(time), lastNs_(time.Nanoseconds()) {}

bool SportTxTransport::Supported() const {
    // Normal, single primary data channel, no multichannel/companding. The
    // physical edge polarity and early/late FS are abstracted at word level.
    return configurationValid_ && Bits() >= 3 && !(tcr1_ & 0xc) && !(tcr2_ & 0x100) && !(mcmc2_ & 0x10) &&
           !(Stereo() && !(tcr1_ & 0x200) && (externalFrameClocks_ & 1));
}

uint64_t SportTxTransport::ClockHz() const {
    if (!Enabled() || !Supported()) return 0;
    // Same explicit 100 MHz SCLK assumption as the existing DMA model.
    if (tcr1_ & 2) return 100'000'000ULL;
    return clockPresent_ ? externalHz_ : 0;
}

uint64_t SportTxTransport::SlotClocks() const {
    if (!(tcr1_ & 0x400)) return Bits();
    if (tcr1_ & 0x200) return uint64_t(fsdiv_) + 1;
    return Stereo() ? externalFrameClocks_ / 2 : externalFrameClocks_;
}

bool SportTxTransport::SyncPresent() const {
    return SlotClocks() >= Bits() && (!(tcr1_ & 0x400) || (tcr1_ & 0x200) || syncPresent_);
}

uint64_t SportTxTransport::NextEdgeAction() const {
    uint64_t distance = UINT64_MAX;
    if (!hold_ && !fifo_.empty()) distance = 1;
    if (shift_) distance = std::min(distance, uint64_t(remaining_));
    if (SyncPresent()) distance = std::min(distance, nextSlot_ - edge_);
    return distance;
}

uint64_t SportTxTransport::AtEdge(uint64_t edges) const {
    const auto hz = ClockHz();
    if (!hz || edges == UINT64_MAX) return UINT64_MAX;
    const uint64_t delta = Ceil((unsigned __int128)edges * kSecond * ClockDenominator() - fraction_, hz);
    return delta > UINT64_MAX - lastNs_ ? UINT64_MAX : lastNs_ + delta;
}

uint64_t SportTxTransport::NextEventNs() const { return AtEdge(NextEdgeAction()); }

void SportTxTransport::Publish() {
    const bool tx = Enabled() && !dmaEnabled_ && fifo_.size() < Capacity();
    const bool error = underflow_ || overflow_;
    if (tx != txLine_ || error != errorLine_) {
        txLine_ = tx;
        errorLine_ = error;
        if (interrupt_) interrupt_(tx, error);
    }
}

void SportTxTransport::Edge(uint64_t ns) {
    if (shift_ && !remaining_) {
        const Word word{ns, transmitted_++, *shift_, u8(Bits()), shiftSlot_, bool(tcr1_ & 0x10), shiftRepeated_, shiftValid_};
        if (shiftValid_) lastWord_ = *shift_;
        shift_.reset();
        if (wordCallback_) wordCallback_(word);
    }
    // Collapse the FIFO -> hold handoff to this serial edge. In particular,
    // it cannot happen without a clock, nor borrow writes from the future.
    if (!hold_ && !fifo_.empty()) {
        hold_ = fifo_.front();
        fifo_.pop_front();
        if (notify_) notify_();
    }
    if (edge_ >= nextSlot_) {
        if (SyncPresent() && !shift_) {
            if (hold_) {
                shift_ = hold_;
                hold_.reset();
                remaining_ = Bits();
                shiftSlot_ = slot_;
                shiftRepeated_ = false;
                shiftValid_ = true;
            } else {
                // Data-dependent internal FS is suppressed on empty TX.
                // External/data-independent FS can repeat the old data.
                if (!(tcr1_ & 0x200) || (tcr1_ & 0x800) || !(tcr1_ & 0x400)) {
                    underflow_ = true;
                    ++underflows_;
                    shift_ = lastWord_.value_or(0);
                    remaining_ = Bits();
                    shiftSlot_ = slot_;
                    shiftRepeated_ = true;
                    shiftValid_ = lastWord_.has_value();
                }
            }
        }
        if (Stereo()) slot_ ^= 1;
        nextSlot_ = edge_ + SlotClocks();
    }
    Publish();
}

void SportTxTransport::Advance() {
    const uint64_t now = time_.Nanoseconds(), hz = ClockHz();
    if (!hz) { lastNs_ = now; return; }
    const auto scaled = (unsigned __int128)(now - lastNs_) * hz + fraction_;
    const uint64_t denominator = kSecond * ClockDenominator();
    const uint64_t due = uint64_t(scaled / denominator);
    uint64_t left = due, moved = 0;
    while (left) {
        const uint64_t distance = NextEdgeAction();
        const uint64_t step = std::min(left, distance);
        if (shift_) remaining_ -= unsigned(step);
        edge_ += step;
        moved += step;
        left -= step;
        // Absent FS still advances its oscillator phase, without starting
        // words. Restoring FS waits for the next scheduled slot.
        if (!SyncPresent() && nextSlot_ <= edge_) {
            const uint64_t period = std::max<uint64_t>(SlotClocks(), 1);
            nextSlot_ += ((edge_ - nextSlot_) / period + 1) * period;
        }
        if (distance == step) Edge(AtEdge(moved));
    }
    fraction_ = uint64_t(scaled % denominator);
    lastNs_ = now;
}

void SportTxTransport::Abort() {
    fifo_.clear(); hold_.reset(); shift_.reset(); lastWord_.reset(); remaining_ = 0;
    underflow_ = overflow_ = false;
    configurationValid_ = true;
    edge_ = fraction_ = 0; nextSlot_ = 1;
    slot_ = (Stereo() && (tcr2_ & 0x400)) ? 1 : 0;
    lastNs_ = time_.Nanoseconds();
}

u32 SportTxTransport::Read(u32 offset) {
    Advance();
    switch (offset) {
    case 0x00: return tcr1_;
    case 0x04: return tcr2_;
    case 0x08: return clkdiv_;
    case 0x0c: return fsdiv_;
    case 0x30: return (fifo_.size() >= Capacity() ? 8 : 0) | (underflow_ ? 16 : 0) |
                      (overflow_ ? 32 : 0) | (!hold_ ? 64 : 0);
    case 0x38: return mcmc1_;
    case 0x3c: return mcmc2_;
    default: return 0;
    }
}

bool SportTxTransport::Enqueue(u32 value) {
    if (fifo_.size() >= Capacity()) return false;
    fifo_.push_back(Bits() == 32 ? value : value & ((1u << Bits()) - 1));
    ++accepted_;
    return true;
}

void SportTxTransport::Write(u32 offset, u32 value) {
    Advance();
    const bool reconfigure = Enabled() &&
        ((offset == 0 && (value & 1) && ((value ^ tcr1_) & 0x7e1e)) ||
         (offset == 4 && (value & 0x71f) != tcr2_) ||
         (offset == 8 && u16(value) != clkdiv_) || (offset == 0xc && u16(value) != fsdiv_) ||
         (offset == 0x38 && u16(value) != mcmc1_) || (offset == 0x3c && u16(value) != mcmc2_));
    switch (offset) {
    case 0x00: {
        const bool was = Enabled();
        tcr1_ = value & 0x7e1f;
        if (!Enabled()) Abort();
        else if (!was) { lastNs_ = time_.Nanoseconds(); edge_ = fraction_ = 0; nextSlot_ = 1; }
        break;
    }
    // Reconfiguration while running is outside the hardware programming
    // contract. Preserve the configured value, abort in-flight state, and
    // require a disable/enable cycle in guests that change the word format.
    case 0x04:
        if (tcr2_ != (value & 0x71f)) { tcr2_ = value & 0x71f; Abort(); }
        break;
    case 0x08: clkdiv_ = value; break;
    case 0x0c: fsdiv_ = value; break;
    case 0x10:
        if (!Enqueue(value)) { overflow_ = true; ++overflows_; }
        break;
    case 0x30:
        if (value & 16) underflow_ = false;
        if (value & 32) overflow_ = false;
        break;
    case 0x38: mcmc1_ = value; break;
    case 0x3c: mcmc2_ = value; break;
    }
    if (reconfigure) configurationValid_ = false;
    if (notify_) notify_();
    Publish();
}

u32 SportTxTransport::Take(u32 bytes) const {
    if (!Enabled() || !Supported()) return 0;
    return u32(std::min<size_t>(bytes / Bytes(), Capacity() - fifo_.size()) * Bytes());
}

u32 SportTxTransport::Accept(const void* data, u32 bytes) {
    Advance();
    const u32 take = Take(bytes), width = Bytes();
    for (u32 offset = 0; offset < take; offset += width) {
        u32 word = 0;
        std::memcpy(&word, static_cast<const u8*>(data) + offset, width);
        Enqueue(word);
    }
    Publish();
    return take;
}

SportTxTransport::Snapshot SportTxTransport::Inspect() {
    const auto status = u16(Read(0x30));
    return {accepted_, transmitted_, underflows_, overflows_, fifo_.size(), bool(hold_), bool(shift_), Supported(), status};
}

void SportTxTransport::SetExternalClock(u32 bitHz, u32 frameClocks, bool clock, bool sync) {
    if (bitHz > 50'000'000 || !frameClocks || frameClocks > 131'072)
        throw std::invalid_argument("synthetic SPORT clock requires <=50 MHz and 1..131072 clocks per frame");
    Advance();
    externalHz_ = bitHz; externalFrameClocks_ = frameClocks;
    clockPresent_ = clock; syncPresent_ = sync;
    if (notify_) notify_();
}

void SportTxTransport::SetExternalSignals(bool clock, bool sync) {
    Advance();
    clockPresent_ = clock; syncPresent_ = sync;
    if (notify_) notify_();
}

void SportTxTransport::SetDMAEnabled(bool enabled) { dmaEnabled_ = enabled; Publish(); }

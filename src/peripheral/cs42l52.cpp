#include "cs42l52.h"

CS42L52::CS42L52(const TimeSource& time, u32 testMclkHz) : time_(time), mclkHz_(testMclkHz) {
    resetSince_ = time_.Nanoseconds();
    ResetRegisters();
}

void CS42L52::ResetRegisters() {
    regs_.fill(0);
    regs_[1] = 0xe3; // Explicit B1 test identity, DS680F2 6.1.
    regs_[2] = 1; regs_[3] = 7; regs_[4] = 5; regs_[5] = 0xa0;
    regs_[0x0d] = 0x60; regs_[0x0e] = 2; regs_[0x32] = 0x3b;
    map_ = initStep_ = 0;
    expectingMap_ = true; increment_ = initComplete_ = false;
    pdnSince_ = time_.Nanoseconds();
}

CS42L52::State CS42L52::Inspect() const {
    State s{reset_, !reset_ && !shortReset_, initComplete_, bool(regs_[2] & 1),
            bool(regs_[2] & 1) && time_.Nanoseconds() - pdnSince_ >= 100'000,
            earlyMclkRemoval_, bool(regs_[6] & 0x80), bool(regs_[7] & 0x10),
            mclkHz_, 0, 0, regs_[5], regs_[6], regs_[7], regs_[2], regs_[3], regs_[4],
            u8(regs_[0x0d] & 3), u8(regs_[0x0f] >> 4), "running"};
    if (s.reset) s.clockReason = "reset";
    else if (shortReset_) s.clockReason = "reset_pulse_below_1ms";
    else if (!s.master) s.clockReason = "slave_no_external_serial_source";
    else if (s.tristate) s.clockReason = "serial_pins_high_impedance";
    else if (!s.mclkHz) s.clockReason = "missing_mclk";
    // DS680F2 Table 1: manual single-speed, RATIO=00, DIV2=0 at
    // 12.288/11.2896 MHz gives 48/44.1 kHz respectively (MCLK/256).
    else if ((s.mclkHz != 12'288'000 && s.mclkHz != 11'289'600) || s.clock != 0x20)
        s.clockReason = "unsupported_clock_configuration";
    else if (s.interface1 != 0x80 || s.interface2 != 0) s.clockReason = "unsupported_serial_configuration";
    // The bounded active-mode experiment does not qualify pin waveforms in
    // global power-down. Fail closed rather than invent PDN clock semantics.
    else if (s.powerDown) s.clockReason = "powerdown_clock_waveform_unqualified";
    else { s.bitHz = s.mclkHz / 4; s.frameHz = s.mclkHz / 256; }
    return s;
}

void CS42L52::Publish() {
    const auto hz = Inspect().bitHz;
    if (clockSink_ && hz != publishedHz_) { publishedHz_ = hz; clockSink_(hz); }
}

void CS42L52::Emit(const char* op, unsigned reg, unsigned requested, unsigned before, unsigned after, bool ack) {
    if (observer_) observer_({time_.Nanoseconds(), op, reg, requested, before, after, ack, Inspect()});
}

void CS42L52::UpdateReset() {
    const bool asserted = resetHold_ || !resetPinHigh_;
    if (asserted == reset_) return;
    reset_ = asserted;
    if (reset_) { resetSince_ = time_.Nanoseconds(); shortReset_ = false; ResetRegisters(); }
    else shortReset_ = time_.Nanoseconds() - resetSince_ < 1'000'000;
    Publish(); Emit("reset");
}

bool CS42L52::SetPinInput(int pin, GPIOPinLevel level) {
    if (pin != 0) return false;
    resetPinHigh_ = level == GPIOPinLevel::High;
    UpdateReset();
    return true;
}

void CS42L52::HoldReset(bool hold) { resetHold_ = hold; UpdateReset(); }

void CS42L52::SetMclk(u32 hz) {
    if (mclkHz_ && !hz && !reset_ && (!Inspect().powerDown || !Inspect().powerDownSettled))
        earlyMclkRemoval_ = true;
    const auto before = mclkHz_;
    mclkHz_ = hz; Publish(); Emit("mclk", 0, hz, before, hz);
}

bool CS42L52::SupportedRegister(unsigned reg) const {
    return (reg >= 1 && reg <= 7) || (reg >= 0x0c && reg <= 0x0f) ||
           reg == 0 || reg == 0x32 || reg == 0x3e || reg == 0x47;
}

bool CS42L52::Store(u8 value) {
    const auto before = regs_[map_];
    bool ack = SupportedRegister(map_);
    // Reserved bits must retain their defaults (6); do not silently mask.
    if ((map_ == 2 && (value & 0x60)) || (map_ == 3 && (value & 0xe0)) ||
        (map_ == 7 && (value & 0x80))) ack = false;
    if (map_ == 0 || map_ == 0x32 || map_ == 0x3e || map_ == 0x47) {
        // Only the published 4.11 sequence is implemented for hidden registers.
        static constexpr u8 addresses[]{0, 0x3e, 0x47, 0x32, 0x32, 0};
        static constexpr u8 values[]{0x99, 0xba, 0x80, 0xbb, 0x3b, 0};
        ack = map_ == addresses[initStep_] && value == values[initStep_];
        if (ack) { if (++initStep_ == 6) { initComplete_ = true; initStep_ = 0; } }
    }
    if (ack && map_ != 1) {
        regs_[map_] = value;
        if (map_ == 2 && !(before & 1) && (value & 1)) pdnSince_ = time_.Nanoseconds();
    }
    Publish(); Emit("write", map_, value, before, regs_[map_], ack);
    return ack;
}

bool CS42L52::Write(const u8* buffer, u32 length) {
    if (!Inspect().controlAvailable) { Emit("write_unavailable", 0, 0, 0, 0, false); return false; }
    for (u32 i = 0; i < length; ++i) {
        if (expectingMap_) {
            map_ = buffer[i] & 0x7f; increment_ = buffer[i] & 0x80; expectingMap_ = false;
            Emit("map", map_, buffer[i]);
        } else {
            if (!Store(buffer[i])) return false;
            if (increment_) map_ = (map_ + 1) & 0x7f;
        }
    }
    return true;
}

bool CS42L52::Read(u8* buffer, u32 length) {
    if (!Inspect().controlAvailable) { Emit("read_unavailable", 0, 0, 0, 0, false); return false; }
    for (u32 i = 0; i < length; ++i) {
        if (!SupportedRegister(map_) || map_ == 0 || map_ == 0x3e || map_ == 0x47) {
            Emit("read_unsupported", map_, 0, 0, 0, false); return false;
        }
        buffer[i] = regs_[map_]; Emit("read", map_, 0, buffer[i], buffer[i]);
        if (increment_) map_ = (map_ + 1) & 0x7f;
    }
    return true;
}

void CS42L52::Stop() { expectingMap_ = true; Emit("stop"); }

#pragma once
#include "cpu/gpio.h"
#include "cpu/twi.h"
#include "cpu/time_source.h"
#include <array>

// Opt-in component experiment, independently specified from Cirrus DS680F2.
// This is not a declaration of the component or wiring fitted to an OP-1.
class CS42L52 final : public I2CPeripheral, public GPIOPeripheral {
public:
    struct State {
        bool reset, controlAvailable, initComplete, powerDown, powerDownSettled;
        bool earlyMclkRemoval, master, tristate;
        u32 mclkHz, bitHz, frameHz;
        u8 clock, interface1, interface2, power1, power2, power3;
        u8 masterMute, outputMute;
        const char* clockReason;
    };
    struct Event {
        uint64_t ns;
        const char* operation;
        unsigned reg, requested, before, after;
        bool ack;
        State state;
    };
    using Observer = std::function<void(const Event&)>;
    explicit CS42L52(const TimeSource& time, u32 testMclkHz);
    u32 Address() const override { return 0x4a; } // Declared AD0=0 test input.
    bool Read(u8* buffer, u32 length) override;
    bool Write(const u8* buffer, u32 length) override;
    void Stop() override;
    State Inspect() const;
    void SetObserver(Observer observer) { observer_ = std::move(observer); Emit("observe"); }
    void SetClockSink(std::function<void(u32)> sink) { clockSink_ = std::move(sink); Publish(); }
    void SetMclk(u32 hz);
    void HoldReset(bool hold); // External test clamp; not a codec register.
    int GetPinCount() const override { return 1; }
    GPIOPinDirection GetDirection(int) const override { return GPIOPinDirection::Input; }
    GPIOPinLevel GetPinOutput(int) const override { return GPIOPinLevel::Low; }
    bool SetPinInput(int pin, GPIOPinLevel level) override;
private:
    void ResetRegisters();
    void UpdateReset();
    void Publish();
    void Emit(const char* op, unsigned reg = 0, unsigned requested = 0,
              unsigned before = 0, unsigned after = 0, bool ack = true);
    bool SupportedRegister(unsigned reg) const;
    bool Store(u8 value);
    const TimeSource& time_;
    std::array<u8, 128> regs_{};
    u32 mclkHz_ = 0;
    bool reset_ = true, resetPinHigh_ = false, resetHold_ = false, shortReset_ = false;
    bool expectingMap_ = true, increment_ = false, initComplete_ = false, earlyMclkRemoval_ = false;
    u8 map_ = 0, initStep_ = 0;
    uint64_t resetSince_ = 0, pdnSince_ = 0;
    Observer observer_;
    std::function<void(u32)> clockSink_;
    u32 publishedHz_ = UINT32_MAX;
};

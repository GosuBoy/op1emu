#pragma once

#include "cpu/io.h"
#include "cpu/gpio.h"
#include <vector>
#include <array>
#include <map>
#include <functional>
#include <optional>

class OLED : public GPIOPeripheral {
public:
    constexpr static int DATA_PINS = 16;

    OLED(std::array<GPIOConnection, DATA_PINS> bus, GPIOConnection cs, GPIOConnection rs, GPIOConnection rd, GPIOConnection wr);

    GPIOPinDirection GetDirection(int pin) const override;
    bool SetPinInput(int pin, GPIOPinLevel level) override;
    GPIOPinLevel GetPinOutput(int pin) const override;
    int GetPinCount() const override;
    using Observer = std::function<void(bool, u32, bool, u32)>; // read, command, supported, bus value
    void SetObserver(Observer observer) { observer_ = std::move(observer); }
    // Explicit test profile only. No identified panel, ROM or reset wiring.
    void SetSyntheticDdb(std::array<u8, 5> reply) { ddb_ = reply; levels_.fill(true); ddbActive_ = false; ddbIndex_ = 0; }
    const std::optional<std::array<u8, 5>>& SyntheticDdb() const { return ddb_; }

protected:
    u32 ReadFromBus() const;
    void WriteToBus(u32 value) const;

    u32 data = 0;
    bool chipSelection = false;
    bool registerSelection = false;
    bool read = false;
    bool write = false;
    std::map<u32, Register> registers;

    std::array<GPIOConnection, DATA_PINS> dataBus;
    Register* selectedRegister = nullptr;
    u32 command_ = 0;
    Observer observer_;
    std::optional<std::array<u8, 5>> ddb_;
    std::array<bool, 4> levels_{{true, true, true, true}};
    bool ddbActive_ = false;
    unsigned ddbIndex_ = 0;
};

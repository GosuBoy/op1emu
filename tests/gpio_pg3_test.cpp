// Synthetic regression for the original OP-1 firmware 246 PG3 notification
// configuration. Its static MAIN disassembly configures Port G pin 3 as a
// falling-edge input in MASKB; SIC source 41 (bank 1, bit 9) is routed to
// IVG13. A selected handler acknowledges the edge with PORTGIO_CLEAR bit 3.
//
// Source: op1-research/screen-rendering-animation/gpio-notification-static/
// FINDINGS.md, verified against the ADSP-BF52x Hardware Reference.
// This is a register/IRQ contract, not a claim that PG3 is physically vblank,
// that no other pins share MASKB, or that actual display scanout occurred.
#include "cpu/gpio.h"
#include "cpu/sic.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr u32 kPg3 = 1u << 3;
constexpr u32 kPg4 = 1u << 4;
constexpr u32 kSicPortGB = 1u << 9;
constexpr u16 kIvg13 = 1u << 13;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        std::exit(1);
    }
}

void Stock246Pg3FallingEdge() {
    SIC sic(0xFFC00100);
    GPIO portG("PORTG", 0xFFC01500);
    std::vector<u16> raised;
    sic.SetLineRaisedCallback([&](u16 newlyRaised) { raised.push_back(newlyRaised); });
    portG.BindInterruptB(41, [&](int source, int level) {
        sic.SetInterruptLevel(source, level);
    });

    // Firmware's SIC_IAR5 nibble 1 selects assignment value 6 -> IVG13.
    const u32 iar5 = sic.Read32(0x54);
    sic.Write32(0x54, (iar5 & ~0xF0u) | 0x60u);
    sic.Write32(0x4C, kSicPortGB);  // SIC_IMASK1 bit 9, source 41
    Check(sic.GetIVG(41) == 13, "Port G interrupt B routes to IVG13");
    Check(sic.Read32(0x54) == ((iar5 & ~0xF0u) | 0x60u),
          "SIC_IAR5 preserves other source assignments");

    // The firmware also selects GPIO function via PORTG_FER and writes a
    // grouped hysteresis register. Neither is modeled by this GPIO class,
    // so reproduce only the independently mapped GPIO/SIC registers.
    portG.Write32(0x30, 0);       // PORTGIO_DIR: PG3 input
    portG.Write32(0x40, kPg3);    // PORTGIO_INEN
    portG.Write32(0x34, kPg3);    // PORTGIO_POLAR: active low
    portG.Write32(0x38, kPg3);    // PORTGIO_EDGE: edge triggered
    portG.Write32(0x3C, 0);       // PORTGIO_BOTH: only selected edge
    portG.Write32(0x28, kPg3);    // PORTGIO_MASKB_SET
    Check((portG.Read32(0x20) & kPg3) != 0, "PG3 enabled in interrupt B");
    Check(sic.Lines() == 0 && raised.empty(), "no interrupt before an input transition");

    Check(portG.SetPinInput(3, GPIOPinLevel::High), "PG3 accepts initial high");
    Check(portG.GetPinOutput(3) == GPIOPinLevel::High, "PG3 initial high");
    Check(sic.Lines() == 0, "initial high does not trigger a falling edge");

    Check(portG.SetPinInput(3, GPIOPinLevel::Low), "PG3 accepts falling edge");
    Check(portG.GetPinOutput(3) == GPIOPinLevel::Low, "PG3 pin is low");
    Check((sic.Read32(0x60) & kSicPortGB) != 0, "SIC_ISR1 reports Port G interrupt B");
    Check((sic.Read32(0x20) & (1u << 15)) == 0, "not DMA0 completion (source 15)");
    Check(sic.Lines() == kIvg13 && raised == std::vector<u16>{kIvg13},
          "first falling edge asserts IVG13 once");

    // An edge latch stays pending until acknowledged, not until pin release.
    Check(portG.SetPinInput(3, GPIOPinLevel::High), "PG3 accepts rising transition");
    Check(sic.Lines() == kIvg13 && raised.size() == 1,
          "rising input leaves sticky falling-edge interrupt pending");
    Check(portG.SetPinInput(3, GPIOPinLevel::Low), "PG3 accepts another falling transition");
    Check(sic.Lines() == kIvg13 && raised.size() == 1,
          "another falling transition does not generate an extra rising IRQ while pending");

    // The stock handler writes bit 3 to PORTGIO_CLEAR to acknowledge the
    // sticky edge latch. This must not alter the externally driven PG3 level.
    const u32 inputDataBeforeAck = portG.Read32(0x00);
    portG.Write32(0x04, kPg3);  // PORTGIO_CLEAR
    Check((sic.Read32(0x60) & kSicPortGB) == 0 && sic.Lines() == 0,
          "PG3 acknowledgment deasserts source 41 / IVG13");
    Check(portG.Read32(0x00) == inputDataBeforeAck,
          "edge acknowledgment must not change input data");
    Check(portG.GetPinOutput(3) == GPIOPinLevel::Low,
          "edge acknowledgment must not change the physical input level");

    const auto risingAfterAck = raised.size();
    Check(portG.SetPinInput(3, GPIOPinLevel::Low), "PG3 can remain low");
    Check(raised.size() == risingAfterAck && sic.Lines() == 0,
          "same low input does not fake a new falling edge after acknowledgment");

    Check(portG.SetPinInput(3, GPIOPinLevel::High), "PG3 returns high");
    Check(sic.Lines() == 0, "rising edge does not trigger configured falling edge");
    Check(portG.SetPinInput(3, GPIOPinLevel::Low), "PG3 falls again");
    Check(raised.size() == risingAfterAck + 1 && raised.back() == kIvg13,
          "high-then-low rearms the falling-edge interrupt");

    portG.Write32(0x24, kPg3);  // PORTGIO_MASKB_CLEAR
    Check(sic.Lines() == 0 && (sic.Read32(0x60) & kSicPortGB) == 0,
          "masking pending GPIO edge lowers SIC source without clearing its latch");
    portG.Write32(0x28, kPg3);  // PORTGIO_MASKB_SET
    Check(sic.Lines() == kIvg13 && raised.size() == risingAfterAck + 2,
          "unmasking pending edge republishes the interrupt");
    portG.Write32(0x04, kPg3);  // acknowledge again
    Check(sic.Lines() == 0 && portG.GetPinOutput(3) == GPIOPinLevel::Low,
          "second acknowledgment clears IRQ while preserving PG3");

    // PORTGIO_CLEAR still clears actual GPIO *output* data. The fix should
    // not alter ordinary output-pin semantics while preserving input pins.
    portG.Write32(0x30, kPg4);  // PG4 output; PG3 stays input
    portG.Write32(0x08, kPg4);  // PORTGIO_SET
    Check(portG.GetPinOutput(4) == GPIOPinLevel::High, "PG4 output was set");
    portG.Write32(0x04, kPg4);  // PORTGIO_CLEAR for output
    Check(portG.GetPinOutput(4) == GPIOPinLevel::Low, "PG4 output was cleared");
    Check(portG.GetPinOutput(3) == GPIOPinLevel::Low && sic.Lines() == 0,
          "clearing another pin does not disturb PG3");
}
} // namespace

int main() {
    Stock246Pg3FallingEdge();
    std::puts("gpio-pg3-test: stock246 falling edge, acknowledgment, SIC routing passed");
}

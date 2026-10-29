// Digital component contract against DS680F2, not an OP-1 hardware oracle.
#include "peripheral/cs42l52.h"
#include <stdexcept>
#include <string>
#include <iostream>
#undef R
#undef W
static void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Rig {
    TimeSource time{true};
    CS42L52 codec{time, 12'288'000};
    u32 wireHz = 999;
    Rig() { codec.SetClockSink([&](u32 hz) { wireHz = hz; }); }
    void WaitNs(uint64_t ns) { time.Idle(TimeSource::CyclesAt(ns)); }
    void Release() { WaitNs(1'000'000); codec.SetPinInput(0, GPIOPinLevel::High); }
    bool W(u8 reg, u8 value) {
        // TWI dispatches MAP and data separately; STOP terminates framing.
        const bool a = codec.Write(&reg, 1), b = codec.Write(&value, 1);
        codec.Stop(); return a && b;
    }
    u8 R(u8 reg) {
        u8 v = 0;
        Check(codec.Write(&reg, 1), "MAP failed"); codec.Stop();
        Check(codec.Read(&v, 1), "read failed"); codec.Stop(); return v;
    }
    void Active() { Check(W(5, 0x20) && W(6, 0x80) && W(2, 0), "configuration failed"); }
    bool Reason(const char* text) { return std::string(codec.Inspect().clockReason) == text; }
};
int main() {
    Rig r;
    u8 v = 1;
    Check(r.wireHz == 0 && !r.codec.Read(&v, 1) && !r.codec.Write(&v, 1), "reset control/clock availability");
    r.Release();
    Check(r.R(1) == 0xe3 && r.R(2) == 1 && r.R(3) == 7 && r.R(4) == 5 && r.R(5) == 0xa0 &&
          r.R(6) == 0 && r.R(7) == 0 && r.R(0x0d) == 0x60 && r.R(0x0e) == 2, "documented defaults");
    Check(r.W(1, 0) && r.R(1) == 0xe3, "identity must be read-only");
    Check(!r.W(2, 0x61) && r.R(2) == 1, "unsupported reserved-bit write changed state");
    Check(!r.W(0x20, 1), "unimplemented volume controls must not silently succeed");
    v = 0x20; Check(r.codec.Write(&v, 1), "unsupported MAP framing"); r.codec.Stop();
    Check(!r.codec.Read(&v, 1), "unimplemented register read must fail"); r.codec.Stop();
    Check(!r.W(0x3e, 0xba), "hidden initialization order must be explicit");
    v = 0x3e; Check(r.codec.Write(&v, 1), "hidden MAP"); r.codec.Stop();
    Check(!r.codec.Read(&v, 1), "hidden command readback must not be invented"); r.codec.Stop();
    Check(r.W(0, 0x99) && r.W(0x3e, 0xba) && r.W(0x47, 0x80) && r.R(0x32) == 0x3b &&
          r.W(0x32, 0xbb) && r.W(0x32, 0x3b) && r.W(0, 0) && r.codec.Inspect().initComplete,
          "required initialization sequence/readback");
    // MAP auto-increment works across fragmented writes and block reads.
    const u8 data[]{0x85, 0x20, 0x80, 0};
    Check(r.codec.Write(data, 2) && r.codec.Write(data + 2, 2), "fragmented increment write"); r.codec.Stop();
    v = 0x85; Check(r.codec.Write(&v, 1), "read MAP"); r.codec.Stop();
    u8 got[3]{}; Check(r.codec.Read(got, 3) && got[0] == 0x20 && got[1] == 0x80 && got[2] == 0,
                      "block increment read"); r.codec.Stop();
    Check(r.Reason("powerdown_clock_waveform_unqualified") && r.wireHz == 0,
          "powerdown pin waveform outside supported active-mode contract");
    r.Active();
    Check(r.wireHz == 3'072'000 && r.codec.Inspect().frameHz == 48'000, "documented master divider");
    Check(r.W(0x0d, 0x63) && r.W(0x0f, 0xf0) && r.wireHz == 3'072'000 &&
          r.codec.Inspect().masterMute == 3 && r.codec.Inspect().outputMute == 15, "mute gated serial clocks");
    Check(r.W(0x0d, 0x60) && r.W(0x0f, 0) && r.wireHz == 3'072'000, "unmute changed clocks");
    Check(r.W(7, 0x10) && r.wireHz == 0 && r.Reason("serial_pins_high_impedance"), "3ST_SP master pins");
    Check(r.W(7, 0) && r.wireHz == 3'072'000, "tri-state release");
    r.codec.SetMclk(0);
    Check(r.wireHz == 0 && r.Reason("missing_mclk") && r.R(5) == 0x20 && r.codec.Inspect().earlyMclkRemoval,
          "MCLK absence/control availability/early removal diagnostic");
    r.codec.SetMclk(12'288'000); Check(r.wireHz == 3'072'000, "MCLK resume");
    r.codec.SetMclk(11'289'600);
    Check(r.wireHz == 2'822'400 && r.codec.Inspect().frameHz == 44'100 &&
          r.R(5) == 0x20 && r.R(6) == 0x80 && r.R(7) == 0, "44.1 kHz manual master divider");
    Check(r.W(5, 0xa0) && r.wireHz == 0, "44.1 kHz AUTO must not silently fall back");
    Check(r.W(5, 0x20) && r.wireHz == 2'822'400, "44.1 kHz manual restore");
    Check(r.W(7, 0x10) && r.wireHz == 0 && r.W(7, 0) && r.wireHz == 2'822'400,
          "44.1 kHz tri-state gate");
    r.codec.SetMclk(0); Check(r.wireHz == 0, "44.1 kHz clock loss");
    r.codec.SetMclk(11'289'600); Check(r.wireHz == 2'822'400, "44.1 kHz clock resume");
    r.codec.SetMclk(12'288'000); Check(r.wireHz == 3'072'000, "48 kHz profile preserved");
    Check(r.W(2, 1) && r.W(6, 0) && r.Reason("slave_no_external_serial_source") && r.wireHz == 0, "slave source");
    Check(r.W(6, 0x80) && r.W(2, 0) && r.wireHz == 3'072'000, "master source restore");
    Check(r.W(5, 0) && r.wireHz == 0 && r.Reason("unsupported_clock_configuration") && r.R(5) == 0,
          "unsupported clock silently fell back");
    Check(r.W(5, 0x20) && r.W(6, 0x84) && r.wireHz == 0 && r.Reason("unsupported_serial_configuration"),
          "unsupported format silently fell back");
    Check(r.W(6, 0x80) && r.W(7, 0x40) && r.wireHz == 0, "unsupported SCLK=MCLK combination");
    Check(r.W(7, 0) && r.wireHz == 3'072'000, "restore supported clock");
    r.codec.SetMclk(12'000'000); Check(r.wireHz == 0 && r.Reason("unsupported_clock_configuration"), "MCLK fallback");
    r.codec.SetPinInput(0, GPIOPinLevel::Low);
    Check(r.wireHz == 0 && r.codec.Inspect().reset, "reset assertion");
    r.WaitNs(1'000'000); r.codec.SetPinInput(0, GPIOPinLevel::High);
    Check(r.R(5) == 0xa0 && r.R(0x0d) == 0x60 && !r.codec.Inspect().initComplete, "reset restores defaults");
    Rig shortPulse;
    shortPulse.WaitNs(999'997); shortPulse.codec.SetPinInput(0, GPIOPinLevel::High);
    Check(!shortPulse.codec.Inspect().controlAvailable && shortPulse.Reason("reset_pulse_below_1ms"),
          "out-of-spec reset pulse must be explicit");
    Rig noMclk;
    noMclk.codec.SetMclk(0); noMclk.Release();
    Check(noMclk.R(1) == 0xe3, "control port incorrectly requires audio MCLK");
    Rig shutdown;
    shutdown.Release(); shutdown.Active();
    Check(shutdown.W(0x0c, 3) && shutdown.W(0x0d, 0x63) && shutdown.W(0x0f, 0xf0) &&
          shutdown.W(0x0e, 0) && shutdown.W(2, 1), "power-down setup");
    shutdown.WaitNs(99'997); Check(!shutdown.codec.Inspect().powerDownSettled, "100 us delay rounded early");
    shutdown.WaitNs(3); Check(shutdown.codec.Inspect().powerDownSettled, "100 us delay not observed");
    shutdown.codec.SetMclk(0); Check(!shutdown.codec.Inspect().earlyMclkRemoval, "valid shutdown flagged early");
    std::cout << "CS42L52 digital test contract PASS\n";
}

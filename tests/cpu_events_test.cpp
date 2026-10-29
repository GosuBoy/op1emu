// The CPU's event queue runs on guest time: an event runs at the first
// ProcessEvents() whose guest time has reached its due time, events due
// together in the order they were queued.
#include "cpu/coretimer.h"
#include "cpu/nand.h"
#include "cpu/cpu.h"
#include "cpu_state.h"
#include "mmr.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

static void Require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct NoFlash : NandFlash {
    void SendCommand(u8) override {}
    void SendAddress(u8) override {}
    u8 ReadData() override { return 0xff; }
    void WriteData(u8) override {}
    bool IsDataReady() const override { return true; }
    bool IsBusy() const override { return false; }
    void StartPageRead() override {}
    void StartPageWrite() override {}
    u32 PageRead(u8*, u32) override { return 0; }
    u32 PageWrite(const u8*, u32) override { return 0; }
    void SetReadCallback(ReadCallback) override {}
};

class TestCpu : public BlackFinCpu {
public:
    TestCpu() : BlackFinCpu(true) { cpuThread_.store(std::this_thread::get_id()); AttachNandFlash(std::make_shared<NoFlash>()); }
    void Advance(uint64_t packets) { time_.Retire(packets); }
    using BlackFinCpu::ProcessEvents;
    using BlackFinCpu::ServiceDevices;
    using BlackFinCpu::DueCycles;
    CoreTimer& Timer() { return *coreTimer; }
    bcore::CpuState& State() { return *cpuState_; }
};

int main() {
    // Isolate OTP creation performed by BlackFinCpu's constructor.
    char directory[] = "/tmp/op1-cpu-events-test-XXXXXX";
    Require(mkdtemp(directory) != nullptr, "create isolated CPU directory");
    auto original = std::filesystem::current_path();
    std::filesystem::current_path(directory);
    {
        TestCpu cpu;
        std::vector<std::string> ran;
        auto log = [&ran](const char* name) { return [&ran, name] { ran.push_back(name); }; };
        using std::chrono::nanoseconds;

        cpu.QueueEvent(log("a"), nanoseconds(10));
        cpu.QueueEvent(log("b"), nanoseconds(10));
        cpu.QueueEvent(log("c"), nanoseconds(5));
        cpu.ProcessEvents();
        Require(ran.empty(), "nothing is due at guest time 0");
        cpu.Advance(1);  // 2 ns
        cpu.ProcessEvents();
        Require(ran.empty(), "an event 5 ns away is not due after 2 ns");
        cpu.Advance(1);  // 5 ns
        cpu.ProcessEvents();
        Require(ran == std::vector<std::string>{"c"}, "the 5 ns event runs at 5 ns");
        cpu.Advance(2);  // 10 ns
        cpu.ProcessEvents();
        Require((ran == std::vector<std::string>{"c", "a", "b"}), "events due together run in queue order");

        ran.clear();
        cpu.QueueEvent([&] {
            ran.push_back("outer");
            cpu.QueueEvent(log("now"), nanoseconds(0));
            cpu.QueueEvent(log("later"), nanoseconds(1));
        }, nanoseconds(0));
        cpu.ProcessEvents();
        Require((ran == std::vector<std::string>{"outer", "now"}),
                "an event queued for now from an event runs in the same call");
        cpu.Advance(1);
        cpu.ProcessEvents();
        Require((ran == std::vector<std::string>{"outer", "now", "later"}), "and one queued for later after it");

        // Another thread's event is timed from the CPU thread's next call.
        ran.clear();
        std::thread([&] { cpu.QueueEvent(log("key"), nanoseconds(4)); }).join();
        cpu.Advance(100);
        cpu.ProcessEvents();
        Require(ran.empty(), "another thread's delay starts when the CPU thread takes the event");
        cpu.Advance(1);  // 2.5 ns later: not yet
        cpu.ProcessEvents();
        Require(ran.empty(), "4 ns have not passed");
        cpu.Advance(1);
        cpu.ProcessEvents();
        Require(ran == std::vector<std::string>{"key"}, "the event runs 4 ns after it was taken");

        // Each core timer expiry latches IVG6, also while TINT is still set.
        const uint32_t ilatAddr = bcore::CEC_MMR_BASE + 4 * bcore::CEC_IDX_ILAT;
        auto ilat = [&] { return bcore::cec_mmr_read(&cpu.State(), ilatAddr) & (1u << 6); };
        cpu.Timer().Write32(0x04, 10);
        cpu.Timer().Write32(0x00, 7);
        cpu.Advance(10);
        cpu.Timer().Advance(cpu.Time().Cycles());
        Require(ilat() && cpu.Timer().InterruptPending(), "the first expiry latches IVG6");
        bcore::cec_mmr_write(&cpu.State(), ilatAddr, 1u << 6);  // the guest clears ILAT, not TINT
        cpu.Advance(10);
        cpu.Timer().Advance(cpu.Time().Cycles());
        Require(ilat(), "the next expiry latches IVG6 again");

        // PIO TX must be on the CPU's event schedule even without a running
        // DMA channel or subsequent MMIO reads to settle the peripheral.
        cpu.ConfigureSportTxTransport(1'000'000, 32);
        std::vector<SportTxTransport::Word> output;
        cpu.SetSportTxWordCallback([&](int n, const auto& word) {
            Require(n == 0, "only SPORT0 was enabled");
            if (word.valid && !word.repeated) output.push_back(word);
        });
        auto& emu = cpu.GetEmulator();
        emu.MemoryWrite16(0xffc00804, 15);
        emu.MemoryWrite16(0xffc00810, 0x1234);
        const auto origin = cpu.Time().Nanoseconds();
        emu.MemoryWrite16(0xffc00800, 0x4401);
        Require(cpu.DueCycles(-1) <= TimeSource::CyclesAt(origin + 1000), "SPORT deadline bounds CPU chaining/IDLE");
        cpu.Advance(400); cpu.ServiceDevices(-1);
        Require(output.empty(), "a word is not transmitted at its first clock edge");
        cpu.Advance(6400); cpu.ServiceDevices(-1);
        Require(output.size() == 1 && output[0].value == 0x1234 && output[0].ns == origin + 17'000,
                "CPU device service completes PIO serialization without DMA or polling");
        Require(emu.MemoryRead32(0xffc00120) & (1u << 17), "CPU binds SPORT0 PIO service to SIC17");
        emu.MemoryWrite16(0xffc00800, 0);

        // DMA4 can complete while every word is still clockless. Its actual
        // CPU interrupt binding (shared with PIO) must survive until W1C.
        emu.MemoryWrite32(0x1000, 0x4321);
        emu.MemoryWrite32(0xffc00d04, 0x1000);
        emu.MemoryWrite16(0xffc00d10, 1);
        emu.MemoryWrite16(0xffc00d14, 2);
        emu.MemoryWrite16(0xffc00d08, 0x85);
        cpu.ConfigureSportTxTransport();
        emu.MemoryWrite16(0xffc00800, 0x4401);
        cpu.ServiceDevices(-1);
        Require(emu.MemoryRead16(0xffc00d28) == 1 && output.size() == 1,
                "CPU DMA4 completes acceptance without serial output");
        Require(emu.MemoryRead32(0xffc00120) & (1u << 17), "CPU binds DMA4 completion to SIC17");
        emu.MemoryWrite16(0xffc00d28, 1);
        Require(!(emu.MemoryRead32(0xffc00120) & (1u << 17)), "CPU DMA4 IRQ acknowledgment clears shared line with DMAEN set");
    }
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    std::puts("cpu-events-test: ok");
}

#include "../ext/bcore/src/cec.h"
#include "cpu/cpu.h"
#include "cpu/debugger.h"
#include "cpu/nand.h"
#include "cpu_state.h"
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>
struct NoFlash : NandFlash {
    void SendCommand(u8) override {}
    void SendAddress(u8) override {}
    u8 ReadData() override { return 255; }
    void WriteData(u8) override {}
    bool IsDataReady() const override { return true; }
    bool IsBusy() const override { return false; }
    void StartPageRead() override {}
    void StartPageWrite() override {}
    u32 PageRead(u8 *, u32) override { return 0; }
    u32 PageWrite(const u8 *, u32) override { return 0; }
    void SetReadCallback(ReadCallback) override {}
};
class TestCpu : public BlackFinCpu {
  public:
    TestCpu(bool deterministic = true) : BlackFinCpu(deterministic) { AttachNandFlash(std::make_shared<NoFlash>()); }
    auto &State() { return *cpuState_; }
};
static void Check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> static void Reject(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    Check(rejected, "expected rejection");
}
int main() try {
    char dir[] = "/tmp/op1-debugger-test-XXXXXX";
    Check(mkdtemp(dir), "temporary directory");
    auto cwd = std::filesystem::current_path();
    std::filesystem::current_path(dir);
    {
        TestCpu cpu;
        cpu.SetChaining(true);
        auto &mem = cpu.GetEmulator();
        constexpr u32 pc = 0xffa02000;
        // R0+=1; R2=CYCLES; JUMP.S -4. An existing chained translation precedes attachment.
        mem.MemoryWrite16(pc, 0x6408);
        mem.MemoryWrite16(pc + 2, 0x3196);
        mem.MemoryWrite16(pc + 4, 0x2ffe);
        cpu.SetPC(pc);
        cpu.Run();
        CpuDebugger debug(cpu);
        debug.AddPcStop(pc);
        debug.AddPcStop(pc + 2);
        cpu.Run();
        Check(debug.Stopped(), "entry breakpoint");
        const auto first = debug.Snapshot();
        const auto time = cpu.Time().Cycles();
        const auto r0 = cpu.State().dpregs[0];
        bool wrongThread = false;
        std::thread([&] {
            try {
                debug.Snapshot();
            } catch (const std::logic_error &) {
                wrongThread = true;
            }
        }).join();
        Check(wrongThread, "inspection thread rejected");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        for (int i = 0; i < 3; ++i) {
            cpu.Run();
            Check(first == debug.Snapshot(), "frozen state");
        }
        Check(time == cpu.Time().Cycles(), "wall pause advances no guest time");
        Check(debug.ReadRam(pc, 6).size() == 6, "plain code RAM read");
        for (const auto &range : std::vector<std::pair<u32, u32>>{
                 {0xffc00810, 4}, {0xfffffff0, 32}, {0x09000000, 1}, {pc, 4097}, {pc, 0}}) {
            const auto a = range.first, n = range.second;
            Reject([&] { debug.ReadRam(a, n); });
        }
        mem.AddReadHook(0x2000, [](u32) {
            throw std::runtime_error("debug must never call hook");
            return 0;
        });
        Reject([&] { debug.ReadRam(0x2000, 1); });
        struct NoReadWatcher : PageWatcher {
            void BeforeAccess() override { throw std::runtime_error("inspection invoked DMA settlement"); }
            bool HostWorkAfterAccess() const override { return false; }
        } watcher;
        mem.WatchPages(&watcher, {0x4000u >> PAGE_BITS});
        Reject([&] { debug.ReadRam(0x4000, 4); });
        mem.WatchPages(nullptr, {});
        Reject([&] { debug.AddPcStop(0x09000000); });
        Reject([&] { debug.AddPcStop(pc + 1); });
        Reject([&] { debug.AddPcStop(0xffc00800); });
        Reject([&] { debug.RemovePcStop(0x4000); });
        Reject([&] { cpu.SetChaining(true); });
        debug.Continue();
        cpu.Run();
        Check(cpu.State().dpregs[0] == r0 + 1, "continue executes current block once");
        cpu.Run();
        Check(debug.Stopped(), "later genuine revisit");
        Check(debug.Status()["targets"][0]["hits"] == 2, "two actual hits");
        Check(!debug.Status()["targets"][1]["verified"].get<bool>(), "inside-block target not falsely hit");
        debug.RemovePcStop(pc);
        debug.Continue();
        cpu.Run();
        Reject([&] { debug.ReadRam(pc, 1); });
        bool event = false;
        cpu.QueueEvent([&] { event = true; }, std::chrono::nanoseconds(10000));
        bcore::cec_mmr_write(&cpu.State(), bcore::CEC_MMR_BASE + 4 * bcore::CEC_IDX_IMASK, 0);
        bcore::cec_raise(&cpu.State(), 9);
        std::thread([&] { cpu.HaltExecution(HaltReason::Break); }).join();
        cpu.Run();
        Check(debug.Stopped(), "off-thread pause request");
        auto pending = debug.Snapshot();
        Check((pending["interrupts"]["ilat"].get<u32>() & (1u << 9)) != 0, "pending interrupt visible");
        Check(pending["events"]["count"] == 1, "queued event preserved");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        Check(pending == debug.Snapshot() && !event, "pending state frozen");
        debug.Continue();
        for (unsigned i = 0; i < 10000 && !event; ++i)
            cpu.Run();
        Check(event, "same queued event delivered after resume");
    }
    {
        TestCpu real(false);
        Reject([&] { CpuDebugger debug(real); });
    }
    std::filesystem::current_path(cwd);
    std::filesystem::remove_all(dir);
    std::cout << "debugger boundary/state tests passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}

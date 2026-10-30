#include "cpu/cpu.h"
#include "cpu/nand.h"
#include "cpu/startup_trace.h"
#include "cpu_state.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unistd.h>
struct UnitFlash : NandFlash {
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
struct BoundedCpu : BlackFinCpu {
    BoundedCpu() : BlackFinCpu(true, true) { AttachNandFlash(std::make_shared<UnitFlash>()); }
};
static void Check(bool v, const char *why) {
    if (!v)
        throw std::runtime_error(why);
}
int main() try {
    const auto cwd = std::filesystem::current_path();
    const auto dir = std::filesystem::temp_directory_path() / ("op1-bounds-" + std::to_string(getpid()));
    std::filesystem::create_directory(dir);
    std::filesystem::current_path(dir);
    {
        BoundedCpu cpu;
        auto &m = cpu.GetEmulator();
        m.MemoryWrite16(0x1000, 0x6408); // R0 += 1
        m.MemoryWrite16(0x1002, 0x2fff); // JUMP.S 0x1000
        cpu.SetPC(0x1000);
        cpu.SetCycleCeiling(257);
        for (unsigned i = 0; i < 257; ++i) {
            auto before = cpu.Time().Cycles();
            cpu.Run();
            Check(cpu.Time().Cycles() == before + 1, "one packet per bounded Run");
        }
        Check(cpu.GetRegister(RegIndex::R0) == 129, "loop preserved at exact packet boundary");
        Check(cpu.Time().Cycles() == 257 && cpu.Time().Packets() == 257, "no cycle overshoot");
        for (unsigned i = 0; i < 3; ++i)
            cpu.Run();
        Check(cpu.Time().Cycles() == 257, "ceiling stops additional instructions");
    }
    {
        BoundedCpu cpu;
        auto &m = cpu.GetEmulator();
        m.MemoryWrite16(0x1000, 0x0020);
        m.MemoryWrite16(0x1002, 0x2000); // IDLE; self-loop
        cpu.SetPC(0x1000);
        cpu.SetCycleCeiling(7);
        bool later = false;
        cpu.QueueEvent([&] { later = true; }, std::chrono::nanoseconds(20));
        cpu.Run();
        Check(cpu.Time().Cycles() == 7 && cpu.Time().Packets() == 1, "IDLE clamps before ceiling");
        Check(cpu.Time().IdleCycles() == 6 && !later, "later event not serviced beyond ceiling");
    }
    {
        BoundedCpu cpu;
        cpu.SetCycleCeiling(0);
        cpu.Run();
        Check(cpu.Time().Cycles() == 0, "zero ceiling never executes");
    }
    {
        BlackFinCpu cpu(true);
        bool rejected = false;
        try {
            cpu.SetCycleCeiling(100);
        } catch (const std::logic_error &) {
            rejected = true;
        }
        Check(rejected, "unlimited compiled code cannot claim a strict ceiling");
    }
    const auto prefix = (dir / "trace").string();
    {
        BoundedCpu cpu;
        StartupTrace trace(cpu, prefix, 2);
        for (unsigned i = 0; i < 10; ++i) {
            cpu.SetPC(0x1000 + 2 * i);
            trace.BeforeRun();
            cpu.GetEmulator().MemoryWrite16(0xffc03244, i);
        }
        trace.Finish("test");
        Check(cpu.GetEmulator().MemoryRead16(0xffc03244) == 9,
              "observation cap preserves raw register behavior");
    }
    nlohmann::json state;
    std::ifstream(prefix + ".state.json") >> state;
    Check(state["category_events"]["pc_first"] == 2 && state["category_omissions"]["pc_first"] == 8,
          "explicit per-source dropped counts");
    Check(state["portg_hysteresis_history"]["recorded"] == 2 &&
              state["portg_hysteresis_history"]["omitted"].get<int>() > 0,
          "dedicated history has its own hard cap");
    Check(!state["portg_hysteresis_history"]["complete"].get<bool>(), "incomplete observation explicit");
    std::filesystem::current_path(cwd);
    std::filesystem::remove_all(dir);
    std::cout << "exact packet/IDLE ceilings and source/history caps passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}

// Synthetic storage/dispatch tests. Byte/32-bit/padding accesses below describe
// emulator mechanics; only aligned 16-bit accesses qualify the BF52x MMR form.
#include "cpu/cpu.h"
#include "cpu/nand.h"
#include "cpu/observed_memory.h"
#include "cpu/startup_trace.h"
#include "loader/load_dxe.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using Json = nlohmann::json;
constexpr u32 kRegister = 0xffc03244;
static void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

static std::vector<Json> Lines(const std::string& path) {
    std::ifstream file(path);
    Check(bool(file), "open history");
    std::vector<Json> lines;
    for (std::string line; std::getline(file, line);) lines.push_back(Json::parse(line));
    return lines;
}

static void Mechanics() {
    Emulator emu;
    ObservedMemoryDevice memory("PORT_MUX", 0xffc03200, 0x100);
    emu.BindDevice(&memory);
    std::vector<ObservedMemoryDevice::Access> seen;
    std::vector<Emulator::AccessOrigin> origins;
    memory.ObserveWord(0x44, [&](const auto& access) { seen.push_back(access); origins.push_back(emu.Origin()); });
    Check(memory.ObservedWord() == 0 && !emu.DirectPages()[kRegister >> PAGE_BITS], "zero construction state, no direct MMR page");
    emu.MemoryWrite16(kRegister, 8);
    Check(seen.back().before == 0 && seen.back().after == 8 && seen.back().bytes == 2, "16-bit write preserves requested value");
    Check(emu.MemoryRead16(kRegister) == 8 && seen.back().data == std::vector<u8>({8,0}), "16-bit read, no fabricated mask");
    emu.MemoryWrite8(kRegister + 1, 0x12);
    emu.MemoryWrite16(kRegister + 2, 0xabcd);
    Check(emu.MemoryRead32(kRegister) == 0xabcd1208, "byte/high-half lanes preserve other bytes");
    Check(emu.MemoryRead16(kRegister) == 0x1208, "upper padding does not alias the 16-bit register");
    emu.MemoryWrite32(kRegister, 0x76543210);
    Check(seen.back().before == 0xabcd1208 && seen.back().after == 0x76543210, "32-bit storage mechanics");
    const u8 bytes[] = {0xaa,1,2,3,4,0xbb};
    emu.MemoryWrite(kRegister - 1, bytes, sizeof(bytes));
    Check(seen.back().bytes == 6 && seen.back().overlapAddress == kRegister &&
          seen.back().data == std::vector<u8>({1,2,3,4}) && memory.ObservedWord() == 0x04030201,
          "bulk write crossing the watched word reports only overlapping data");
    const auto count = seen.size();
    emu.MemoryWrite16(kRegister - 4, 0xffff);
    Check(seen.size() == count, "neighboring pin-control register is independent");
    bool work, stable;
    Check(emu.ReadAttended<u16>(kRegister, work, stable) == 0x0201 && origins.back() == Emulator::AccessOrigin::Guest,
          "guest read dispatch is identified once");
    emu.WriteAttended<u16>(kRegister, 8, work);
    Check(seen.back().after == 0x04030008 && origins.back() == Emulator::AccessOrigin::Guest,
          "guest low-half write has the same lane behavior");
    {
        Emulator::OriginScope loader(emu, Emulator::AccessOrigin::Loader);
        emu.MemoryWrite16(kRegister, 0);
        Check(origins.back() == Emulator::AccessOrigin::Loader, "loader scope reaches bulk memory path");
    }
    emu.MemoryRead16(kRegister);
    Check(origins.back() == Emulator::AccessOrigin::Host, "origin scope restores host");
    memory.Map(0);
    Check(seen.back().mapping && memory.MappingExposures() == 1, "raw pointer exposure is reported conservatively");
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

class GuestCpu : public BlackFinCpu {
public:
    GuestCpu() : BlackFinCpu(true) {
        cpuThread_.store(std::this_thread::get_id());
        AttachNandFlash(std::make_shared<NoFlash>());
        SetChaining(false);
    }
};

static void GuestHistory() {
    GuestCpu cpu;
    {
        StartupTrace trace(cpu, "guest");
        trace.AddMemory(0x2000, 4);
        trace.AddCheckpoint(0xffa00100);
        DXE dxe;
        Block program{};
        program.header.block_code = BFLAG_FIRST;
        program.header.target_address = 0xffa00100;
        // W[P1](X) -> R0; BITSET(R0,3); W[P1] = R0.L; RTS.
        // Standard Blackfin instructions, synthetic program independent of firmware.
        program.data = {0x48,0x95,0x18,0x4a,0x09,0x8a,0x10,0x00};
        dxe.blocks.push_back(program);
        Block initialize{};
        initialize.header.target_address = kRegister;
        initialize.data = {0,0};
        dxe.blocks.push_back(initialize);
        LoadDXE(cpu, dxe);
        cpu.SetRegister(RegIndex::P1, kRegister);
        for (unsigned n = 0; cpu.PC() != kLdrReturnAddress && !cpu.Failed() && n < 30; ++n) {
            trace.BeforeRun(); cpu.Run();
        }
        Check(!cpu.Failed() && cpu.PC() == kLdrReturnAddress, "synthetic guest executes through normal loader/CPU");
        Check(cpu.GetEmulator().MemoryRead16(kRegister) == 8, "actual guest read/bitset/low-half-store yields 8");
        trace.Finish("test_return");
    }
    auto history = Lines("guest.portg-hysteresis.jsonl");
    unsigned guestReads = 0, guestWrites = 0, loaderWrites = 0;
    bool begin = false, end = false, checkpoint = false;
    for (const auto& e : history) {
        if (e["kind"] == "read" && e["origin"] == "guest") ++guestReads;
        if (e["kind"] == "write" && e["origin"] == "guest") {
            ++guestWrites;
            Check(e["bytes"] == 2 && e["before_word"] == 0 && e["after_word"] == 8, "guest history before/after");
        }
        if (e["kind"] == "write" && e["origin"] == "loader") ++loaderWrites;
        if (e.value("label", "") == "dxe_load_begin") begin = e["word"] == 0;
        if (e.value("label", "") == "dxe_load_end") end = e["word"] == 0;
        if (e.value("label", "") == "requested_pc") checkpoint = e["pc"] == 0xffa00100 && e["memory"].size() == 1;
    }
    Check(guestReads == 1 && guestWrites == 1 && loaderWrites == 1 && begin && end && checkpoint, "complete origin and boundary history");
    Check(history.back()["history"]["complete"] == true && history.back()["history"]["omitted"] == 0,
          "no silent truncation or raw pointer gap");
    Check(std::filesystem::file_size("guest.checkpoint-ffa00100.memory-2000.bin") == 4, "checkpoint RAM snapshot");
    {
        StartupTrace trace(cpu, "limited");
        for (unsigned n = 0; n < 10005; ++n) cpu.GetEmulator().MemoryRead16(kRegister);
        cpu.GetEmulator().MemoryMap(kRegister);
        trace.Finish("test_limit");
    }
    history = Lines("limited.portg-hysteresis.jsonl");
    unsigned truncations = 0;
    for (const auto& e : history) if (e["kind"] == "truncated") ++truncations;
    const auto& summary = history.back()["history"];
    Check(truncations == 1 && summary["recorded"] == 10000 && summary["omitted"] > 0 &&
          summary["raw_mapping_exposures"] == 1 && summary["complete"] == false, "dedicated budget and mapping gaps explicitly reported");
}

int main() {
    Mechanics();
    char directory[] = "/tmp/op1-memory-observation-XXXXXX";
    Check(mkdtemp(directory), "temporary working directory");
    const auto cwd = std::filesystem::current_path();
    std::filesystem::current_path(directory);
    GuestHistory();
    std::filesystem::current_path(cwd);
    std::filesystem::remove_all(directory);
}

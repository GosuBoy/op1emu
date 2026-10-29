#include "cpu/cpu.h"
#include "cpu/debug_commands.h"
#include "cpu/debugger.h"
#include "cpu/nand.h"
#include "loader/load_dxe.h"
#include "peripheral/sport_capture.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
struct NoFlash final : NandFlash {
    void SendCommand(u8) override {}
    void SendAddress(u8) override {}
    u8 ReadData() override { return 0xff; }
    void WriteData(u8) override {}
    bool IsDataReady() const override { return true; }
    bool IsBusy() const override { return false; }
    void StartPageRead() override {}
    void StartPageWrite() override {}
    u32 PageRead(u8 *, u32) override { return 0; }
    u32 PageWrite(const u8 *, u32) override { return 0; }
    void SetReadCallback(ReadCallback) override {}
};
int main(int argc, char **argv) try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "op1-debug --ldr IMAGE --commands FILE --log FILE [--codec-mclk HZ --capture PREFIX --max-cycles "
                     "N --max-runs N --host-timeout-ms N]\n"
                  << "Deterministic, single RAM DXE; starts paused. Append JSONL "
                     "pause/continue/break/remove/status/registers/ram/audio/wait-stop/quit commands to FILE.\n";
        return 0;
    }
    std::string image, commands, log, capture;
    uint32_t mclk = 0;
    uint64_t maxCycles = 6'000'000'000, maxRuns = 200'000'000, timeout = 30000;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            throw std::invalid_argument("option requires a value");
        std::string k = argv[i], v = argv[i + 1];
        auto number = [&] {
            size_t end = 0;
            if (v.empty() || v[0] == '-')
                throw std::invalid_argument("unsigned option value required");
            auto n = std::stoull(v, &end);
            if (end != v.size())
                throw std::invalid_argument("invalid numeric option");
            return n;
        };
        if (k == "--ldr")
            image = v;
        else if (k == "--commands")
            commands = v;
        else if (k == "--log")
            log = v;
        else if (k == "--capture")
            capture = v;
        else if (k == "--codec-mclk") {
            auto n = number();
            if (n != 11289600 && n != 12288000)
                throw std::invalid_argument("unsupported test MCLK");
            mclk = uint32_t(n);
        } else if (k == "--max-cycles")
            maxCycles = number();
        else if (k == "--max-runs")
            maxRuns = number();
        else if (k == "--host-timeout-ms")
            timeout = number();
        else
            throw std::invalid_argument("unsupported option (deterministic RAM LDR mode only)");
    }
    if (image.empty() || commands.empty() || log.empty() || !maxCycles || !maxRuns || !timeout || timeout > 3600000)
        throw std::invalid_argument("usage: op1-debug --ldr IMAGE --commands FILE --log FILE [--codec-mclk HZ "
                                    "--capture PREFIX --max-cycles N --max-runs N --host-timeout-ms N]");
    auto canonical = [](const std::string &p) { return std::filesystem::weakly_canonical(p); };
    std::vector<std::string> outputs{log};
    if (!capture.empty()) {
        outputs.push_back(capture + ".pcm");
        outputs.push_back(capture + ".csv");
    }
    for (const auto &output : outputs)
        if (canonical(output) == canonical(image) || canonical(output) == canonical(commands))
            throw std::invalid_argument("debug outputs must be separate from image and commands");
    if (!capture.empty() &&
        (canonical(log) == canonical(capture + ".pcm") || canonical(log) == canonical(capture + ".csv")))
        throw std::invalid_argument("debug log must be separate from capture streams");
    BlackFinCpu cpu(true);
    cpu.AttachNandFlash(std::make_shared<NoFlash>());
    cpu.SetTwiSpeedup(1);
    if (mclk)
        cpu.ConfigureCs42l52Test(mclk);
    std::unique_ptr<SportCapture> pcm;
    if (!capture.empty()) {
        pcm = std::make_unique<SportCapture>(capture);
        cpu.SetSportTxWordCallback([&](int n, const auto &w) {
            if (n == 0)
                pcm->Write(w);
        });
    }
    LDRParser loader;
    if (!loader.loadFile(image) || loader.getDXEs().size() != 1)
        throw std::invalid_argument("requires one direct DXE");
    LoadDXE(cpu, loader.getDXEs().front());
    CpuDebugger debugger(cpu);
    DebugCommands control(debugger, commands, log);
    debugger.RequestPause();
    cpu.Run();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    uint64_t runs = 0;
    auto nextPoll = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline && runs < maxRuns && cpu.Time().Cycles() < maxCycles) {
        const auto now = std::chrono::steady_clock::now();
        if (debugger.Stopped() || now >= nextPoll) {
            if (control.Poll())
                return 0;
            nextPoll = now + std::chrono::milliseconds(1);
        }
        if (debugger.Stopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        cpu.Run();
        ++runs;
        if (cpu.Failed()) {
            control.Finish("cpu-failed");
            return 1;
        }
        if (cpu.PC() == kLdrReturnAddress) {
            control.Finish("ldr-return");
            return 0;
        }
    }
    control.Finish(std::chrono::steady_clock::now() >= deadline ? "host-timeout"
                   : runs >= maxRuns                            ? "run-limit"
                                                                : "cycle-limit");
    return 124;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 2;
}

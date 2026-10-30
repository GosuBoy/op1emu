#include "cpu/cpu.h"
#include "cpu/startup_trace.h"
#include "cpu_state.h"
#include "loader/load_dxe.h"
#include "peripheral/MT29F4G08.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>

using Json = nlohmann::ordered_json;
namespace {
struct Flash final : MT29F4G08 {
    using MT29F4G08::MT29F4G08;
    uint64_t commands = 0;
    void SendCommand(u8 command) override {
        ++commands;
        MT29F4G08::SendCommand(command);
    }
};
Json Snapshot(BlackFinCpu &cpu) {
    const auto &c = cpu.DiagnosticState();
    auto &m = cpu.GetEmulator();
    Json registers = Json::object();
    for (auto [name, addr] : std::map<std::string, uint32_t>{{"pll_ctl", 0xffc00000},
                                                             {"pll_div", 0xffc00004},
                                                             {"pll_stat", 0xffc0000c},
                                                             {"syscr", 0xffc00104},
                                                             {"sic_imask0", 0xffc0010c},
                                                             {"sic_iwr0", 0xffc00124},
                                                             {"sic_iwr1", 0xffc00164},
                                                             {"sdgctl", 0xffc00a10},
                                                             {"sdbctl", 0xffc00a14},
                                                             {"sdrrc", 0xffc00a18},
                                                             {"sdstat", 0xffc00a1c},
                                                             {"evt1", 0xffe02004},
                                                             {"imask", 0xffe02104},
                                                             {"ipend", 0xffe02108},
                                                             {"ilat", 0xffe0210c},
                                                             {"dmem_control", 0xffe00004},
                                                             {"imem_control", 0xffe01004}})
        registers[name] = m.MemoryRead32(addr);
    std::vector<uint32_t> mailbox(16), context(4);
    for (unsigned i = 0; i < mailbox.size(); ++i)
        mailbox[i] = m.MemoryRead32(0xff800000 + 4 * i);
    for (unsigned i = 0; i < context.size(); ++i)
        context[i] = m.MemoryRead32(0xff800800 + 4 * i);
    Json result = {{"pc", cpu.PC()},
                   {"cycles", cpu.Time().Cycles()},
                   {"packets", cpu.Time().Packets()},
                   {"idle_cycles", cpu.Time().IdleCycles()},
                   {"dpregs", c.dpregs},
                   {"usp", c.usp},
                   {"ksp", c.ksp},
                   {"rets", c.rets},
                   {"reti", c.reti},
                   {"retx", c.retx},
                   {"seqstat", c.seqstat},
                   {"syscfg", c.syscfg},
                   {"mmr", registers},
                   {"mailbox", mailbox},
                   {"context", context},
                   {"sdram_sentinel", m.MemoryRead32(0x00100800)}};
    // Never dereference a malformed guest boot-data pointer for observation.
    const auto p = c.dpregs[0];
    if (!(p & 3) && p >= 0xffb00000 && p <= 0xffb00fa0) {
        Json boot = Json::object();
        for (auto [name, offset] : std::map<std::string, uint32_t>{
                 {"source", 0}, {"flags", 0x18}, {"user_long", 0x28}, {"callback", 0x38}})
            boot[name] = m.MemoryRead32(p + offset);
        result["r0_boot_data"] = boot;
    }
    return result;
}
} // namespace
int main(int argc, char **argv) try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout
            << "Supervised synthetic INIT worker. Use tools/run_init_handoff.py for the hard wall limit.\n"
               "--image LDR --nand BACKING --output PREFIX [--path rom|direct|direct-stream]\n"
               "[--max-cycles N --max-runs N --wall-ms N --trace-cap N]\n";
        return 0;
    }
    std::string image, nand, output, path = "rom";
    uint64_t cycles = 1'000'000'000, runs = 5'000'000, wall = 120000, cap = 4096;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc)
            throw std::invalid_argument("option requires value");
        std::string k = argv[i], v = argv[i + 1];
        auto number = [&] {
            size_t end = 0;
            if (v.empty() || v[0] == '-')
                throw std::invalid_argument("unsigned limit required");
            auto n = std::stoull(v, &end);
            if (end != v.size())
                throw std::invalid_argument("invalid limit");
            return n;
        };
        if (k == "--image")
            image = v;
        else if (k == "--nand")
            nand = v;
        else if (k == "--output")
            output = v;
        else if (k == "--path")
            path = v;
        else if (k == "--max-cycles")
            cycles = number();
        else if (k == "--max-runs")
            runs = number();
        else if (k == "--wall-ms")
            wall = number();
        else if (k == "--trace-cap")
            cap = number();
        else
            throw std::invalid_argument("unsupported option");
    }
    if (image.empty() || nand.empty() || output.empty() || !cycles || cycles > 1'000'000'000 || !runs ||
        runs > 5'000'000 || !wall || wall > 120000 || !cap || cap > 4096)
        throw std::invalid_argument("required image/nand/output; ceilings: cycles 1e9, Run calls 5e6, "
                                    "wall-ms 120000, trace-cap 4096");
    if (path != "rom" && path != "direct" && path != "direct-stream")
        throw std::invalid_argument(
            "unsupported_path: only synthetic memory-boot/direct comparison; NAND boot is unqualified");
    auto canonical = [](const std::string &p) { return std::filesystem::weakly_canonical(p); };
    for (const auto &suffix : {".result.json", ".events.jsonl", ".state.json", ".portg-hysteresis.jsonl"})
        if (canonical(output + suffix) == canonical(image) || canonical(output + suffix) == canonical(nand))
            throw std::invalid_argument("output overlaps an input");
    if (!std::filesystem::is_regular_file(nand))
        throw std::invalid_argument("synthetic NAND backing must exist");
    LDRParser parser;
    if (!parser.loadFile(image) || parser.getDXEs().empty() ||
        (path != "direct-stream" && parser.getDXEs().size() != 1))
        throw std::invalid_argument("unsupported LDR container");
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(wall);
    BlackFinCpu cpu(true, true);
    cpu.SetCycleCeiling(cycles);
    cpu.SetBootMode(0x0d);           // caller's explicit synthetic context, not measured straps
    cpu.ConfigureSportTxTransport(); // no external serial clock
    auto flash = std::make_shared<Flash>(cpu, nand, true);
    cpu.AttachNandFlash(flash);
    StartupTrace trace(cpu, output, cap);
    Json phases = Json::array();
    uint64_t phaseDropped = 0;
    auto phase = [&](const std::string &name) {
        if (phases.size() == cap) {
            ++phaseDropped;
            return;
        }
        phases.push_back({{"name", name}, {"state", Snapshot(cpu)}});
    };
    phase("constructed");
    LoadDXE(cpu, parser.getDXEs().front());
    phase("dxe_loaded");
    std::map<uint32_t, std::string> targets;
    const auto manifestPath = std::filesystem::path(image).parent_path() / "manifest.json";
    std::ifstream mf(manifestPath);
    Json manifest;
    mf >> manifest;
    auto name = std::filesystem::path(image).stem().string();
    if (name == "stream")
        name = "normal";
    for (const auto &label : {"boot_call", "init_entry", "init_valid", "init_return", "callback_entry",
                              "callback_valid", "callback_return", "main_entry", "poison_entry"})
        targets[manifest.at("images").at(name).at("symbols").at(label).get<uint32_t>()] = label;
    uint64_t calls = 0, romCalls = 0;
    bool sawInit = false, sawCallback = false, sawMain = false;
    std::string status;
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            status = "host_timeout";
            break;
        }
        if (cpu.Time().Cycles() >= cycles) {
            status = "cycle_limit";
            break;
        }
        if (calls >= runs) {
            status = "run_limit";
            break;
        }
        if (auto it = targets.find(cpu.PC()); it != targets.end()) {
            sawInit |= it->second == "init_entry";
            sawCallback |= it->second == "callback_entry";
            sawMain |= it->second == "main_entry";
            phase(it->second);
        }
        if (cpu.PC() >= 0xef000000 && cpu.PC() < 0xef008000)
            ++romCalls;
        trace.BeforeRun();
        ++calls;
        cpu.Run();
        if (cpu.Failed()) {
            status = "guest_failure";
            break;
        }
        const auto code = cpu.GetEmulator().MemoryRead32(0xff800000);
        if (code == 0x48414e44) {
            status = "completed";
            break;
        }
        if ((code & 0xffff0000) == 0 && (code & 0xff00) == 0xe000) {
            status = "guest_rejected";
            break;
        }
        if (cpu.PC() == kLdrReturnAddress) {
            status = "unexpected_return";
            break;
        }
    }
    const bool contract = path == "rom" ? romCalls && sawInit && sawCallback && sawMain
                                        : path == "direct" && !romCalls && sawMain;
    if (status == "completed" && !contract)
        status = "contract_failure";
    phase("stopped");
    trace.Finish(status);
    std::ifstream sf(output + ".state.json");
    Json traceState;
    sf >> traceState;
    uint64_t omitted = phaseDropped + traceState.at("access_counter_omissions").get<uint64_t>() +
                       traceState.at("portg_hysteresis_history").at("omitted").get<uint64_t>();
    for (const auto &value : traceState.at("category_omissions"))
        omitted += value.get<uint64_t>();
    const bool complete = !omitted;
    if (status == "completed" && !complete)
        status = "incomplete_observation";
    Json result = {
        {"schema", 1},
        {"status", status},
        {"path", path},
        {"trace_complete", complete},
        {"contract_satisfied", contract},
        {"observation_complete", complete && status == "completed"},
        {"limits", {{"cycles", cycles}, {"run_calls", runs}, {"wall_ms", wall}, {"events_per_source", cap}}},
        {"run_calls", calls},
        {"rom_run_calls", romCalls},
        {"nand_commands", flash->commands},
        {"cycles", cpu.Time().Cycles()},
        {"phases", phases},
        {"handoff_events_dropped", phaseDropped},
        {"category_omissions", traceState.at("category_omissions")},
        {"history_omissions", traceState.at("portg_hysteresis_history").at("omitted")},
        {"final", Snapshot(cpu)},
        {"time_model_hz", TimeSource::kHz},
        {"qualification",
         "synthetic memory-boot ABI only; attached NAND unused; no physical clock/SDRAM/board claim"}};
    std::ofstream receipt(output + ".result.json");
    receipt << result.dump(2) << '\n';
    receipt.close();
    std::cout << Json{{"status", status},
                      {"cycles", cpu.Time().Cycles()},
                      {"run_calls", calls},
                      {"rom_run_calls", romCalls}}
                     .dump()
              << '\n';
    return status == "completed"                                                          ? 0
           : status == "guest_rejected"                                                   ? 3
           : status == "cycle_limit" || status == "run_limit" || status == "host_timeout" ? 124
                                                                                          : 1;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 2;
}

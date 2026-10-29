#include "startup_trace.h"
#include "cpu.h"
#include "bcore_memory.h"
#include "observed_memory.h"
#include "sport.h"
#include "twi.h"
#include "peripheral/oled.h"
#include "cpu_state.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

using Json = nlohmann::ordered_json;

static const char* OriginName(Emulator::AccessOrigin origin) {
    switch (origin) {
        case Emulator::AccessOrigin::Guest: return "guest";
        case Emulator::AccessOrigin::Loader: return "loader";
        default: return "host";
    }
}

static bool Interesting(uint32_t a) {
    return (a >= 0xffc00000 && a < 0xffc00e00) ||
           (a >= 0xffc01400 && a < 0xffc01800) ||
           (a >= 0xffc03200 && a < 0xffc03300) ||
           (a >= 0xffe02000 && a < 0xffe02200) || (a >= 0xffe03000 && a < 0xffe03010);
}

StartupTrace::StartupTrace(BlackFinCpu& cpu, std::string prefix) : cpu_(cpu), prefix_(std::move(prefix)) {
    events_.open(prefix_ + ".events.jsonl");
    if (!events_) throw std::runtime_error("cannot open startup events");
    hysteresis_.open(prefix_ + ".portg-hysteresis.jsonl");
    if (!hysteresis_) throw std::runtime_error("cannot open Port G history");
    for (auto* dev : cpu_.emulator.Devices())
        if (dev->BaseAddress() == 0xffc03200) portMux_ = dynamic_cast<ObservedMemoryDevice*>(dev);
    if (!portMux_) throw std::runtime_error("Port G observation device unavailable");
    portMux_->ObserveWord(0x44, [this](const ObservedMemoryDevice::Access& a) {
        if (!a.mapping) (a.write ? historyWrites_ : historyReads_)++;
        if (a.before != lastWord_) ++unattributedChanges_;
        lastWord_ = a.after;
        History(Json{{"kind", a.mapping ? "raw_mapping_exposed" : a.write ? "write" : "read"},
                     {"origin", OriginName(cpu_.emulator.Origin())}, {"ns", cpu_.Time().Nanoseconds()},
                     {"block_pc", cpu_.PC()}, {"address", a.address}, {"bytes", a.bytes},
                     {"overlap_address", a.overlapAddress}, {"data", a.data},
                     {"before_word", a.before}, {"after_word", a.after}, {"after_low16", a.after & 0xffff}}.dump());
    });
    lastWord_ = portMux_->ObservedWord();
    hysteresis_ << Json{{"kind", "contract"}, {"schema", 1}, {"address", 0xffc03244u},
                       {"storage_bytes", 4}, {"hardware_register_bits", 16}, {"max_records", 10000},
                       {"pc_precision", "basic-block entry; resolve actual instruction from disassembly"},
                       {"origin", "dispatch scope; nested callbacks inherit scope"},
                       {"model", "unmodified generic byte storage; not reserved-encoding hardware semantics"}}.dump() << '\n';
    cpu_.emulator.SetBoundaryObserver([this](const char* label) { Checkpoint(label); });
    Checkpoint("observer_attached");
    cpu_.bcoreMemory_->SetObserver([this](uint32_t a, unsigned width, bool write, uint32_t value) {
        if (!Interesting(a)) return;
        ++accesses_[{a, width, write}];
        if (!write) {
            auto key = std::make_pair(a, width);
            const auto previous = reads_.find(key);
            if (previous != reads_.end() && previous->second == value) { ++omitted_; return; }
            reads_[key] = value;
        } else reads_.erase({a, width});
        // Separate budgets keep display traffic from hiding later codec/SPORT
        // events. Access totals continue after detailed observations are capped.
        const auto page = a & 0xffffff00u;
        const char* group = "other_mmio";
        uint64_t budget = 10000;
        if (page == 0xffc00700 || page == 0xffc01500 || page == 0xffc01700) { group = "gpio_mmio"; budget = 5000; }
        else if (page == 0xffc00600 || page == 0xffe03000) { group = "timer_mmio"; budget = 5000; }
        else if (page == 0xffc01400) { group = "twi_mmio"; budget = 5000; }
        else if (page == 0xffc00800 || page == 0xffc00900 || page == 0xffc00d00) { group = "transport_mmio"; budget = 15000; }
        else if (page == 0xffc00100 || page == 0xffe02000 || page == 0xffe02100) group = "interrupt_mmio";
        if (!Reserve(group, budget)) return;
        Emit(Json{{"kind", write ? "mmio_write" : "mmio_read_change"}, {"ns", cpu_.Time().Nanoseconds()},
                  {"block_pc", cpu_.PC()}, {"address", a}, {"bytes", width}, {"value", value}}.dump());
    });
    cpu_.serviced_.twi->SetObserver([this](u8 address, bool read, const std::vector<u8>& bytes, bool ack, bool stop) {
        if (!Reserve("i2c", 30000)) return;
        Emit(Json{{"kind", stop ? "i2c_stop" : "i2c_bytes"}, {"ns", cpu_.Time().Nanoseconds()},
                  {"block_pc", cpu_.PC()}, {"address", address}, {"read", read}, {"data", bytes}, {"ack", ack}}.dump());
    });
    cpu_.oled->SetObserver([this](bool read, u32 command, bool supported, u32 value) {
        if (!Reserve("panel", 1000)) return;
        Emit(Json{{"kind", read ? "panel_read" : "panel_command"}, {"ns", cpu_.Time().Nanoseconds()},
                  {"block_pc", cpu_.PC()}, {"command", command}, {"register_mapped", supported},
                  {"synthetic_profile", cpu_.oled->SyntheticDdb().has_value()}, {"bus_value", value}}.dump());
    });
    Emit(Json{{"kind", "contract"}, {"version", 1}, {"max_events", 200000},
              {"pc_precision", "basic-block entry; not the exact instruction"},
              {"mmio_reads", "first/change only; host snapshot reads are excluded"},
              {"i2c_time", "service observation, not electrical edge time"},
              {"budgets", {{"pc_first", 100000}, {"gpio_mmio", 5000}, {"timer_mmio", 5000}, {"twi_mmio", 5000},
                           {"transport_mmio", 15000}, {"interrupt_mmio", 10000}, {"other_mmio", 10000},
                           {"i2c", 30000}, {"panel", 1000}, {"progress", 10000}}}}.dump());
}

StartupTrace::~StartupTrace() {
    cpu_.emulator.SetBoundaryObserver({});
    portMux_->ObserveWord(0x44, {});
    cpu_.bcoreMemory_->SetObserver({});
    cpu_.serviced_.twi->SetObserver({});
    cpu_.oled->SetObserver({});
}

void StartupTrace::History(const std::string& json) {
    ++historyTotal_;
    if (historyWritten_ == 10000) {
        if (!historyOmitted_) hysteresis_ << "{\"kind\":\"truncated\",\"max_records\":10000}\n";
        ++historyOmitted_;
        return;
    }
    hysteresis_ << json << '\n';
    ++historyWritten_;
}

void StartupTrace::AddCheckpoint(uint32_t pc) {
    if (checkpoints_.size() == 64) throw std::runtime_error("at most 64 diagnostic checkpoints");
    checkpoints_.insert(pc);
}

void StartupTrace::Checkpoint(const char* label, bool memory) {
    const auto word = portMux_->ObservedWord();
    if (word != lastWord_) ++unattributedChanges_;
    lastWord_ = word;
    Json event = {{"kind", "checkpoint"}, {"label", label}, {"origin", OriginName(cpu_.emulator.Origin())},
                  {"pc", cpu_.PC()}, {"ns", cpu_.Time().Nanoseconds()}, {"word", word},
                  {"low16", word & 0xffff}, {"dpregs", cpu_.cpuState_->dpregs}, {"memory", Json::array()}};
    if (memory) {
        for (auto [address, bytes] : memory_) {
            std::ostringstream suffix;
            suffix << ".checkpoint-" << std::hex << cpu_.PC() << ".memory-" << address << ".bin";
            std::vector<char> data(bytes);
            cpu_.emulator.MemoryRead(address, data.data(), bytes);
            std::ofstream file(prefix_ + suffix.str(), std::ios::binary);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file.write(data.data(), data.size());
            event["memory"].push_back({{"address", address}, {"bytes", bytes}, {"suffix", suffix.str()}});
        }
    }
    History(event.dump());
}

void StartupTrace::Emit(const std::string& json) {
    if (eventsWritten_ == 200000) { ++omitted_; return; }
    events_ << json << '\n';
    ++eventsWritten_;
}

bool StartupTrace::Reserve(const char* category, uint64_t limit) {
    auto& count = categoryEvents_[category];
    if (count >= limit) { ++categoryOmissions_[category]; ++omitted_; return false; }
    ++count;
    return true;
}

void StartupTrace::BeforeRun() {
    const auto pc = cpu_.PC();
    if (!checkpoints_.empty() && checkpoints_.erase(pc)) Checkpoint("requested_pc", true);
    // Limit unique-PC storage as well as the output file.
    if (pcs_.size() < 100000 || pcs_.count(pc)) {
        if (++pcs_[pc] == 1)
            Emit(Json{{"kind", "pc_first"}, {"ns", cpu_.Time().Nanoseconds()}, {"pc", pc}}.dump());
    } else ++omitted_;
    if (cpu_.Time().Cycles() >= nextProgress_ && Reserve("progress", 10000)) {
        Emit(Json{{"kind", "progress"}, {"cycles", cpu_.Time().Cycles()}, {"packets", cpu_.Time().Packets()},
                  {"pc", pc}, {"ipend", cpu_.emulator.MemoryRead32(0xffe02108)},
                  {"ilat", cpu_.emulator.MemoryRead32(0xffe0210c)}}.dump());
        nextProgress_ = cpu_.Time().Cycles() + 40'000'000;
    }
}

void StartupTrace::AddMemory(uint32_t address, uint32_t bytes) {
    if (!bytes || bytes > 1024 * 1024 || uint64_t(address) + bytes > 0xffc00000 || !cpu_.emulator.Maps(address, bytes))
        throw std::runtime_error("diagnostic memory range must be mapped RAM, at most 1 MiB");
    for (uint64_t a = address; a < uint64_t(address) + bytes; a = (a & ~uint64_t(4095)) + 4096)
        if (!cpu_.emulator.Quiet(a)) throw std::runtime_error("diagnostic memory range must be plain RAM");
    memory_.emplace_back(address, bytes);
}

void StartupTrace::Finish(const std::string& reason) {
    auto& c = *cpu_.cpuState_;
    auto& emu = cpu_.emulator;
    Json registers = Json::object();
    const std::pair<uint32_t, uint32_t> ranges[] = {
        {0xffc00000, 0x10}, {0xffc00100, 0x6c}, {0xffc00700, 0x44},
        {0xffc00800, 0x40}, {0xffc00900, 0x40}, {0xffc00d00, 0x40},
        {0xffc01400, 0x30}, {0xffc01500, 0x44}, {0xffc01700, 0x44}, {0xffc03200, 0x100},
        {0xffc03700, 0x50}, {0xffe02000, 0x40}, {0xffe02100, 0x14}, {0xffe03000, 0x10}
    };
    // Final observation only, after the CPU thread is joined. These reads
    // may settle a device to the stopped guest time; never poll to progress.
    for (auto [base, length] : ranges) {
        for (uint32_t a = base; a < base + length; a += 4) {
            // Never consume FIFO/data registers while taking a diagnostic.
            if (a == 0xffc00810 || a == 0xffc00818 || a == 0xffc00910 || a == 0xffc00918 ||
                a == 0xffc0372c || a == 0xffc0374c) continue;
            std::ostringstream name;
            name << std::hex << std::setfill('0') << std::setw(8) << a;
            registers[name.str()] = emu.MemoryRead32(a);
        }
    }
    Json ports = Json::array();
    for (const auto& sport : {cpu_.sport0, cpu_.sport1}) {
        const auto s = sport->TxTransport().Inspect();
        const auto clocks = sport->TxTransport().InspectExternalInputs();
        ports.push_back({{"accepted", s.accepted}, {"transmitted", s.transmitted}, {"fifo", s.fifo},
                         {"holding", s.holding}, {"shifting", s.shifting}, {"supported", s.supported},
                         {"status", s.status}, {"underflows", s.underflows}, {"overflows", s.overflows},
                         {"external_bit_hz", clocks.bitHz}, {"external_frame_clocks", clocks.frameClocks},
                         {"external_clock_present", clocks.clock}, {"external_sync_present", clocks.sync}});
    }
    std::vector<std::pair<uint32_t, uint64_t>> top(pcs_.begin(), pcs_.end());
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    if (top.size() > 64) top.resize(64);
    Json histogram = Json::array();
    for (auto [pc, count] : top) histogram.push_back({{"pc", pc}, {"runs", count}});
    Json accesses = Json::array();
    for (const auto& item : accesses_) {
        auto [address, bytes, write] = item.first;
        accesses.push_back({{"address", address}, {"bytes", bytes}, {"write", write}, {"count", item.second}});
    }
    Checkpoint("stopped");
    Json history = {{"address", 0xffc03244u}, {"total", historyTotal_}, {"recorded", historyWritten_},
                    {"omitted", historyOmitted_}, {"reads", historyReads_}, {"writes", historyWrites_},
                    {"raw_mapping_exposures", portMux_->MappingExposures()}, {"unattributed_changes", unattributedChanges_},
                    {"complete", !historyOmitted_ && !portMux_->MappingExposures() && !unattributedChanges_},
                    {"pending_checkpoints", checkpoints_}};
    hysteresis_ << Json{{"kind", "summary"}, {"history", history}}.dump() << '\n';
    Json report = {{"schema", 1}, {"stop_reason", reason}, {"pc", cpu_.PC()}, {"cpu_failed", cpu_.Failed()},
                   {"time_model_hz", TimeSource::kHz},
                   {"panel_profile", cpu_.oled->SyntheticDdb() ? Json(*cpu_.oled->SyntheticDdb()) : Json(nullptr)},
                   {"cycles", cpu_.Time().Cycles()}, {"packets", cpu_.Time().Packets()}, {"idle_cycles", cpu_.Time().IdleCycles()},
                   {"seqstat", c.seqstat}, {"retx", c.retx}, {"reti", c.reti}, {"rets", c.rets},
                   {"dpregs", c.dpregs}, {"mmr", registers}, {"sport", ports}, {"pc_run_histogram", histogram}, {"access_counts", accesses},
                   {"events_written", eventsWritten_}, {"events_omitted_or_coalesced", omitted_},
                   {"category_events", categoryEvents_}, {"category_omissions", categoryOmissions_},
                   {"portg_hysteresis_history", history}, {"memory", Json::array()}};
    for (auto [address, bytes] : memory_) {
        std::ostringstream suffix;
        suffix << ".memory-" << std::hex << address << ".bin";
        std::vector<char> data(bytes);
        emu.MemoryRead(address, data.data(), bytes);
        std::ofstream file(prefix_ + suffix.str(), std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.write(data.data(), data.size());
        report["memory"].push_back({{"address", address}, {"bytes", bytes}, {"suffix", suffix.str()}});
    }
    std::ofstream file(prefix_ + ".state.json");
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << report.dump(2) << '\n';
    events_.flush();
    if (!events_) throw std::runtime_error("cannot write startup events");
    hysteresis_.flush();
    if (!hysteresis_) throw std::runtime_error("cannot write Port G history");
    events_.close();
    hysteresis_.close();
}

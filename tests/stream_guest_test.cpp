// Host supplies only declared external clocks/reset/wiring and observes.
// No guest MMIO writes, refills, manual service/time advance or IRQ injection.
#include "cpu/cpu.h"
#include "cpu/dma.h"
#include "cpu/nand.h"
#include "cpu/sport.h"
#include "cpu/twi.h"
#include "peripheral/cs42l52.h"
#include "peripheral/sport_capture.h"
#include "loader/load_dxe.h"
#include "cpu_state.h"
#include "../ext/bcore/src/cec.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>
using Json = nlohmann::ordered_json;
struct NoFlash : NandFlash {
    void SendCommand(u8) override {} void SendAddress(u8) override {}
    u8 ReadData() override { return 0xff; } void WriteData(u8) override {}
    bool IsDataReady() const override { return true; } bool IsBusy() const override { return false; }
    void StartPageRead() override {} void StartPageWrite() override {}
    u32 PageRead(u8*, u32) override { return 0; } u32 PageWrite(const u8*, u32) override { return 0; }
    void SetReadCallback(ReadCallback) override {}
};
class GuestCpu : public BlackFinCpu {
public:
    GuestCpu(bool chain) : BlackFinCpu(true) {
        cpuThread_.store(std::this_thread::get_id()); SetChaining(chain);
        AttachNandFlash(std::make_shared<NoFlash>());
    }
    auto PeekSport() const { return sport0->TxTransport().Peek(); }
    auto PeekDma() const { return serviced_.dma->InspectTx(4); }
    void ObserveDma(DMA::TxObserver o) { serviced_.dma->SetTxObserver(std::move(o)); }
    void ObserveTwi(TWI::Observer o) { serviced_.twi->SetObserver(std::move(o)); }
    u32 Cec(unsigned off) { return bcore::cec_mmr_read(cpuState_.get(), 0xffe02100 + off); }
    u32 Seqstat() const { return cpuState_->seqstat; }
};
static Json DmaJson(const DMA::TxSnapshot& s) {
    return {{"modeled", s.modeled}, {"running", s.running}, {"done", s.done}, {"sync", s.sync},
            {"address", s.address}, {"remaining", s.remaining}, {"buffered", s.buffered},
            {"work", s.work}, {"read", s.read}, {"accepted", s.accepted}};
}
static Json SportJson(const SportTxTransport::Snapshot& s) {
    return {{"accepted", s.accepted}, {"transmitted", s.transmitted}, {"fifo", s.fifo},
            {"holding", s.holding}, {"shifting", s.shifting}, {"underflows", s.underflows},
            {"overflows", s.overflows}, {"status", s.status}, {"supported", s.supported}};
}
static uint32_t HashWord(uint32_t hash, u32 v) {
    for (unsigned b = 0; b < 4; ++b) hash = (hash ^ ((v >> (b * 8)) & 255)) * 16777619u;
    return hash;
}
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string mode = argv[3];
    if (mode != "normal" && mode != "delay" && mode != "premature" && mode != "pause" && mode != "clock-timeout" && mode != "stop-delay") return 2;
    bool chain = false;
    uint64_t maxCycles = 200'000'000;
    unsigned rate = 48000, blockCount = 256, mailboxAddress = 0x2000, ledgerAddress = 0x10000;
    unsigned ledgerCapacity = 2048, runLimit = 3'000'000;
    u32 stackBegin = 0, stackBytes = 0;
    std::vector<std::pair<u32, unsigned>> memoryRanges;
    std::vector<std::pair<u32, uint64_t>> pcWatches;
    for (int i = 4; i < argc; ++i) {
        if (std::string(argv[i]) == "--chain") chain = true;
        else if (std::string(argv[i]) == "--max-cycles" && i + 1 < argc) maxCycles = std::stoull(argv[++i]);
        else if (std::string(argv[i]) == "--rate" && i + 1 < argc) rate = std::stoul(argv[++i]);
        else if (std::string(argv[i]) == "--blocks" && i + 1 < argc) blockCount = std::stoul(argv[++i]);
        else if (std::string(argv[i]) == "--mailbox" && i + 1 < argc) mailboxAddress = std::stoul(argv[++i], nullptr, 0);
        else if (std::string(argv[i]) == "--ledger" && i + 1 < argc) ledgerAddress = std::stoul(argv[++i], nullptr, 0);
        else if (std::string(argv[i]) == "--ledger-capacity" && i + 1 < argc) ledgerCapacity = std::stoul(argv[++i]);
        else if (std::string(argv[i]) == "--max-runs" && i + 1 < argc) runLimit = std::stoul(argv[++i]);
        else if (std::string(argv[i]) == "--watch-pc" && i + 1 < argc) pcWatches.emplace_back(std::stoul(argv[++i], nullptr, 0), 0);
        else if (std::string(argv[i]) == "--stack" && i + 2 < argc) {
            stackBegin = std::stoul(argv[++i], nullptr, 0);
            stackBytes = std::stoul(argv[++i]);
            if ((stackBegin & 3) || (stackBytes & 3) || stackBytes > 1024 * 1024) return 2;
        } else if (std::string(argv[i]) == "--observe-memory" && i + 2 < argc) {
            const auto address = std::stoul(argv[++i], nullptr, 0);
            const auto count = std::stoul(argv[++i]);
            if (count > 4096) return 2;
            memoryRanges.emplace_back(address, count);
        } else return 2;
    }
    if ((rate != 48000 && rate != 44100) || !blockCount || blockCount > 16384 ||
        ledgerCapacity > 131072 || !ledgerCapacity) return 2;
    const auto mclk = rate == 44100 ? 11'289'600u : 12'288'000u;
    GuestCpu cpu(chain);
    cpu.SetTwiSpeedup(1); cpu.ConfigureCs42l52Test(mclk);
    SportCapture capture(argv[2]);
    uint64_t captured = 0, repeated = 0, invalid = 0;
    uint64_t observedUnderflows = 0;
    unsigned maxDmaBuffered = 0, maxSportBuffered = 0, maxCombinedBuffered = 0;
    Json blocks = Json::array(), prefixReads = Json::array(), clocks = Json::array();
    Json sportErrors = Json::array();
    Json codecEvents = Json::array(), twiEvents = Json::array();
    unsigned dropped = 0;
    auto observeBuffers = [&] {
        const auto d = cpu.PeekDma();
        const auto s = cpu.PeekSport(); // Passive, including inside a serial callback.
        const unsigned held = unsigned(s.fifo) + s.holding + s.shifting;
        maxDmaBuffered = std::max(maxDmaBuffered, d.buffered);
        maxSportBuffered = std::max(maxSportBuffered, held);
        maxCombinedBuffered = std::max(maxCombinedBuffered, d.buffered + held);
        if (s.underflows != observedUnderflows) {
            observedUnderflows = s.underflows;
            if (sportErrors.size() < 1024) sportErrors.push_back({{"observed_ns", cpu.Time().Nanoseconds()},
                {"underflows", s.underflows}, {"overflows", s.overflows}, {"status", s.status}});
            else ++dropped;
        }
    };
    auto snapshot = [&](const char* label) {
        if (clocks.size() == 16) { ++dropped; return; }
        clocks.push_back({{"label", label}, {"ns", cpu.Time().Nanoseconds()},
                          {"dma", DmaJson(cpu.PeekDma())}, {"sport", SportJson(cpu.PeekSport())},
                          {"mclk_hz", cpu.TestCodec()->Inspect().mclkHz}});
    };
    cpu.SetSportTxWordCallback([&](int port, const auto& word) {
        if (port) throw std::runtime_error("unexpected SPORT1 output");
        ++captured; repeated += word.repeated; invalid += !word.valid; capture.Write(word);
        observeBuffers();
    });
    cpu.TestCodec()->SetObserver([&](const CS42L52::Event& e) {
        if (codecEvents.size() >= 2048) { ++dropped; return; }
        codecEvents.push_back({{"ns", e.ns}, {"operation", e.operation}, {"reg", e.reg},
            {"requested", e.requested}, {"after", e.after}, {"ack", e.ack},
            {"bit_hz", e.state.bitHz}, {"frame_hz", e.state.frameHz}, {"reason", e.state.clockReason},
            {"master_mute", e.state.masterMute}, {"output_mute", e.state.outputMute}});
    });
    cpu.ObserveTwi([&](u8 addr, bool read, const auto& data, bool ack, bool stop) {
        if (twiEvents.size() >= 2048) { ++dropped; return; }
        twiEvents.push_back({{"ns", cpu.Time().Nanoseconds()}, {"address", addr}, {"read", read},
                            {"data", data}, {"ack", ack}, {"stop", stop}});
    });
    cpu.ObserveDma([&](const DMA::TxEvent& e) {
        observeBuffers();
        if (e.channel != 4) throw std::runtime_error("unexpected buffered DMA channel");
        const std::string op = e.operation;
        if (op == "start") {
            if (blocks.size() >= blockCount) { ++dropped; return; }
            blocks.push_back({{"work", e.work}, {"start_ns", e.ns}, {"read_words", 0}, {"accepted_words", 0},
                              {"read_hash", 2166136261u}, {"accept_hash", 2166136261u}});
            // Declared external input changes, scheduled by CPU guest time.
            if ((mode == "pause" || mode == "clock-timeout") && e.work == 33) {
                cpu.QueueEvent([&] { cpu.TestCodec()->SetMclk(0); snapshot("drop"); }, std::chrono::microseconds(500));
                cpu.QueueEvent([&] { snapshot("stalled-a"); }, std::chrono::microseconds(600));
                cpu.QueueEvent([&] { snapshot("stalled-b"); }, std::chrono::microseconds(900));
                if (mode == "pause") cpu.QueueEvent([&] { snapshot("before-resume"); cpu.TestCodec()->SetMclk(mclk); },
                                                     std::chrono::microseconds(1000));
            }
        }
        if (!e.work || e.work > blocks.size()) { ++dropped; return; }
        auto& b = blocks[e.work - 1];
        if (op == "read" || op == "accept") {
            const auto countKey = op == "read" ? "read_words" : "accepted_words";
            const auto hashKey = op == "read" ? "read_hash" : "accept_hash";
            b[countKey] = b[countKey].get<unsigned>() + 1;
            b[hashKey] = HashWord(b[hashKey].get<u32>(), e.value);
            if (op == "read" && e.work == 1 && prefixReads.size() < 16)
                prefixReads.push_back({{"ns", e.ns}, {"address", e.address}, {"value", e.value}, {"state", DmaJson(e.state)}});
        } else if (op == "complete") {
            b["complete_ns"] = e.ns; b["completion"] = DmaJson(e.state);
            b["serialized_at_completion"] = captured;
        } else if (op == "drained") b["drained_ns"] = e.ns;
    });
    LDRParser loader;
    if (!loader.loadFile(argv[1]) || loader.getDXEs().size() != 1) return 2;
    LoadDXE(cpu, loader.getDXEs().front());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    unsigned runs = 0;
    while (cpu.PC() != kLdrReturnAddress && !cpu.Failed() && cpu.Time().Cycles() < maxCycles &&
           runs < runLimit && std::chrono::steady_clock::now() < deadline) {
        for (auto& watch : pcWatches) if (cpu.PC() == watch.first) ++watch.second;
        cpu.Run(); ++runs;
    }
    observeBuffers();
    capture.Finish();
    auto& emu = cpu.GetEmulator();
    Json box = Json::array();
    for (unsigned i = 0; i < 64; ++i) box.push_back(emu.MemoryRead32(mailboxAddress + i * 4));
    Json guestEvents = Json::array();
    for (unsigned i = 0; i < std::min(box[50].get<unsigned>(), ledgerCapacity); ++i) {
        Json row = Json::array();
        for (unsigned j = 0; j < 16; ++j) row.push_back(emu.MemoryRead32(ledgerAddress + i * 64 + j * 4));
        guestEvents.push_back(row);
    }
    Json errors = Json::array();
    auto check = [&](bool ok, const char* why) { if (!ok) errors.push_back(why); };
    check(cpu.PC() == kLdrReturnAddress && !cpu.Failed(), "guest execution bound or CPU failure");
    check(cpu.Seqstat() == 0 && box[26] == 0, "guest exception");
    check(dropped == 0 && box[51] == 0 && box[50].get<unsigned>() <= ledgerCapacity, "trace overflow");
    check(maxDmaBuffered <= 4 && maxSportBuffered <= 6 && maxCombinedBuffered <= 10, "unbounded TX buffering");
    check(box[25] == 0x76543210 && box[32] == 0xe3 && box[35] == 0x494e4954, "guest context/codec initialization");
    check(invalid == 0, "invalid serial output");
    if (mode == "clock-timeout") {
        check(box[0] == 0x57444f47 && box[55] == 1 && box[2] == 32, "guest clock watchdog did not bound the stall");
    } else if (mode == "stop-delay") {
        check(box[0] == 0x554e4452 && box[55] == 0 && box[2] == 10 && box[42] == 10 && box[43] == 10,
              "late producer did not stop diagnostically before new submission");
        check(captured - repeated == 1280 && repeated > 0 && cpu.PeekSport().accepted == 1280,
              "late producer output prefix/replay accounting");
        check(!cpu.PeekDma().running && !cpu.PeekSport().shifting, "late producer stop left transmitter running");
        check(cpu.Cec(12) == 0 && !(cpu.Cec(8) & 0x200), "late producer interrupt not acknowledged");
    } else {
        check(box[0] == 0x50415353 && box[2] == blockCount && box[41] == blockCount && box[42] == blockCount && box[43] == blockCount,
              "guest did not produce/submit/complete requested blocks");
        check(box[44] == 0 && box[45] == 0 && box[48] == (blockCount + 1) / 2 && box[49] == blockCount / 2,
              "buffer ownership/reuse totals");
        check(captured - repeated == uint64_t(blockCount) * 128 && cpu.PeekSport().accepted == uint64_t(blockCount) * 128, "finite output length");
        check(cpu.PeekSport().overflows == 0 && cpu.PeekSport().fifo == 0 && !cpu.PeekSport().holding &&
              !cpu.PeekSport().shifting, "shutdown did not drain and stop");
        check(cpu.Cec(12) == 0 && !(cpu.Cec(8) & 0x200), "unacknowledged DMA interrupt");
        check(mode == "delay" ? repeated > 0 : repeated == 0, "unexpected steady-state replay");
    }
    Json report = {{"mode", mode}, {"pc", cpu.PC()}, {"cycles", cpu.Time().Cycles()},
        {"packets", cpu.Time().Packets()}, {"idle_cycles", cpu.Time().IdleCycles()}, {"seqstat", cpu.Seqstat()},
        {"cec", {{"imask", cpu.Cec(4)}, {"ipend", cpu.Cec(8)}, {"ilat", cpu.Cec(12)}}},
        {"mailbox", box}, {"dma", DmaJson(cpu.PeekDma())}, {"sport", SportJson(cpu.PeekSport())},
        {"blocks", blocks}, {"prefix_reads", prefixReads}, {"clock_events", clocks},
        {"sport_error_observations", sportErrors}, {"buffer_high_water", {{"dma", maxDmaBuffered},
             {"sport", maxSportBuffered}, {"combined", maxCombinedBuffered}}},
        {"codec_events", codecEvents}, {"twi_events", twiEvents}, {"guest_events", guestEvents},
        {"trace_dropped", dropped}, {"trace_limits", {{"blocks", blockCount}, {"guest", ledgerCapacity}, {"codec", 2048},
             {"twi", 2048}, {"clock", 16}, {"prefix_reads", 16}, {"sport_errors", 1024}}},
        {"capture_words", captured}, {"repeated_words", repeated}, {"invalid_words", invalid}, {"errors", errors}};
    if (stackBytes) {
        u32 firstWritten = stackBytes;
        for (u32 offset = 0; offset < stackBytes; offset += 4) {
            if (emu.MemoryRead32(stackBegin + offset) != 0xa5a5a5a5) { firstWritten = offset; break; }
        }
        report["stack_observation"] = {{"begin", stackBegin}, {"bytes", stackBytes},
                                       {"painted_high_water_bytes", stackBytes - firstWritten}};
    }
    if (!memoryRanges.empty()) {
        report["memory_observations"] = Json::array();
        for (auto [address, count] : memoryRanges) {
            Json values = Json::array();
            for (unsigned i = 0; i < count; ++i) values.push_back(emu.MemoryRead32(address + 4 * i));
            report["memory_observations"].push_back({{"address", address}, {"values", values}});
        }
    }
    if (!pcWatches.empty()) {
        report["pc_watches"] = Json::array();
        for (auto [address, hits] : pcWatches) report["pc_watches"].push_back({{"address", address}, {"hits", hits}});
    }
    std::ofstream(std::string(argv[2]) + ".json") << report.dump(2) << '\n';
    if (!errors.empty()) { std::cerr << errors.dump() << " PC=" << cpu.PC() << " mailbox=" << box.dump() << '\n'; return 1; }
    std::cout << mode << ": " << box[2] << " IRQ, " << box[48] << '/' << box[49] << " productions per slot, "
              << captured << " words, " << repeated << " repeated, " << cpu.Time().Cycles() << " cycles\n";
}

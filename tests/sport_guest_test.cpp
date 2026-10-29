// Integration driver: no host MMIO writes, manual peripheral service, time
// advancement or interrupt injection. The only stimulus is the declared clock.
#include "cpu/cpu.h"
#include "cpu/nand.h"
#include "cpu/sport.h"
#include "loader/load_dxe.h"
#include "peripheral/sport_capture.h"
#include "cpu_state.h"
#include "../ext/bcore/src/cec.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using Json = nlohmann::ordered_json;

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
    explicit GuestCpu(bool chain) : BlackFinCpu(true) {
        cpuThread_.store(std::this_thread::get_id());
        AttachNandFlash(std::make_shared<NoFlash>());
        SetChaining(chain);
    }
    auto Transport() { return sport0->TxTransport().Inspect(); }
    u32 Cec(unsigned offset) { return bcore::cec_mmr_read(cpuState_.get(), 0xffe02100 + offset); }
    u32 Seqstat() const { return cpuState_->seqstat; }
    u32 Retx() const { return cpuState_->retx; }
};

static const char* kFields[] = {
    "signature", "phase", "dma_entries", "timer_entries", "after_rti", "sample_words", "idle_entries",
    "start_cycles", "dma_entry_cycles", "dma_ack_cycles", "guard_start_cycles", "timer_entry_cycles", "end_cycles",
    "dma_count_at_isr", "dma_address_at_isr", "irq_before_ack", "irq_after_ack", "sport_at_isr", "ipend_at_isr",
    "dma_reti", "final_dma_count", "final_dma_address", "final_sport", "final_ilat", "final_ipend", "canary",
    "exception_seqstat", "exception_retx", "timer_reti", "isr_tag", "timer_phase", "reserved"
};

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: op1-sport-guest-test IMAGE.ldr PREFIX clock|no-clock [--chain] [--max-cycles N]\n";
        return 2;
    }
    const bool clock = std::string(argv[3]) == "clock";
    bool chain = false;
    uint64_t maxCycles = 2'000'000;
    for (int i = 4; i < argc; ++i) {
        if (std::string(argv[i]) == "--chain") chain = true;
        else if (std::string(argv[i]) == "--max-cycles" && i + 1 < argc) maxCycles = std::stoull(argv[++i]);
        else return 2;
    }
    if (!clock && std::string(argv[3]) != "no-clock") return 2;
    GuestCpu cpu(chain);
    SportCapture capture(argv[2]);
    std::vector<SportTxTransport::Word> words;
    cpu.ConfigureSportTxTransport(clock ? 1'000'000 : 0, 64);
    cpu.SetSportTxWordCallback([&](int port, const auto& word) {
        if (port != 0) throw std::runtime_error("unexpected SPORT1 output");
        words.push_back(word);
        capture.Write(word);
    });
    LDRParser loader;
    if (!loader.loadFile(argv[1]) || loader.getDXEs().size() != 1) return 2;
    LoadDXE(cpu, loader.getDXEs().front());
    const auto wallLimit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    unsigned runs = 0;
    // CPU::Run owns event deadlines and IDLE wakeup. Do not poll guest MMIO
    // here: observing DMA/SPORT between instructions must not drive progress.
    while (cpu.PC() != kLdrReturnAddress && !cpu.Failed() && cpu.Time().Cycles() < maxCycles &&
           runs < 1'000'000 && std::chrono::steady_clock::now() < wallLimit) {
        cpu.Run();
        ++runs;
    }
    const auto state = cpu.Transport();
    capture.Finish();
    auto& emu = cpu.GetEmulator();
    Json box = Json::object();
    for (unsigned i = 0; i < std::size(kFields); ++i)
        box[kFields[i]] = emu.MemoryRead32(0x2000 + i * 4);
    Json report = {
        {"synthetic_bit_hz", clock ? 1'000'000 : 0}, {"synthetic_frame_clocks", 64},
        {"pc", cpu.PC()}, {"cpu_failed", cpu.Failed()}, {"seqstat", cpu.Seqstat()}, {"retx", cpu.Retx()},
        {"cycles", cpu.Time().Cycles()}, {"packets", cpu.Time().Packets()}, {"idle_cycles", cpu.Time().IdleCycles()},
        {"cec", {{"imask", cpu.Cec(4)}, {"ipend", cpu.Cec(8)}, {"ilat", cpu.Cec(12)}}},
        {"sic", {{"imask0", emu.MemoryRead32(0xffc0010c)}, {"isr0", emu.MemoryRead32(0xffc00120)},
                 {"iar2", emu.MemoryRead32(0xffc00118)}, {"iwr0", emu.MemoryRead32(0xffc00124)}}},
        {"dma", {{"config", emu.MemoryRead16(0xffc00d08)}, {"irq", emu.MemoryRead16(0xffc00d28)},
                 {"address", emu.MemoryRead32(0xffc00d24)}, {"count", emu.MemoryRead16(0xffc00d30)},
                 {"x_count", emu.MemoryRead16(0xffc00d10)}, {"x_modify", emu.MemoryRead16(0xffc00d14)},
                 {"pmap", emu.MemoryRead16(0xffc00d2c)}}},
        {"sport", {{"tcr1", emu.MemoryRead16(0xffc00800)}, {"tcr2", emu.MemoryRead16(0xffc00804)},
                   {"status", state.status}, {"fifo", state.fifo}, {"holding", state.holding}, {"shifting", state.shifting},
                   {"supported", state.supported}, {"accepted", state.accepted}, {"transmitted", state.transmitted},
                   {"underflows", state.underflows}, {"overflows", state.overflows}}},
        {"mailbox", box}, {"samples", Json::array()}
    };
    for (unsigned i = 0; i < 16; ++i) report["samples"].push_back(emu.MemoryRead32(0x3000 + i * 4));
    std::vector<std::string> errors;
    auto check = [&](bool ok, const char* why) { if (!ok) errors.emplace_back(why); };
    auto m = [&](const char* field) { return box[field].get<u32>(); };
    check(cpu.PC() == kLdrReturnAddress && !cpu.Failed(), "guest did not return within execution limits");
    check(cpu.Seqstat() == 0 && m("exception_seqstat") == 0, "unexpected guest exception");
    check(m("sample_words") == 16 && m("canary") == 0x76543210, "guest initialization/context preservation failed");
    check((cpu.Cec(4) & 0x8240) == 0x8240 && report["sic"]["imask0"] == 0x20000 &&
          report["sic"]["iar2"] == 0x20 && report["sic"]["iwr0"] == 0x20000, "guest interrupt routing incorrect");
    check(state.supported && report["sport"]["tcr2"] == 0x21f && report["dma"]["config"] == 0x89 &&
          report["dma"]["x_count"] == 16 && report["dma"]["x_modify"] == 4 && report["dma"]["pmap"] == 0x4000,
          "guest SPORT0/DMA4 configuration incorrect");
    check(m("timer_entries") == 1 && m("idle_entries") > 0 && cpu.Time().IdleCycles() > cpu.Time().Packets() * 100,
          "guest timer interrupt/IDLE did not execute");
    check((cpu.Cec(12) & (1u << 9)) == 0 && (cpu.Cec(8) & (1u << 9)) == 0, "DMA interrupt remains pending after acknowledgment/RTI");
    for (unsigned i = 0; i < 16; ++i)
        check(report["samples"][i] == 0x12340001u + i * 0x101u, "guest did not populate exact sample buffer");
    if (clock) {
        check(m("signature") == 0x50415353 && m("after_rti") == 0x52544939 && m("isr_tag") == 0x444d4139,
              "guest did not execute DMA ISR, return and publish PASS");
        check(m("dma_entries") == 1 && m("dma_count_at_isr") == 0 && m("dma_address_at_isr") == 0x3040,
              "missed/repeated DMA completion interrupt or wrong DMA acceptance");
        check(m("irq_before_ack") == 1 && m("irq_after_ack") == 0 && (m("ipend_at_isr") & 0x200),
              "guest DMA W1C acknowledgment/IVG9 state incorrect");
        check(words.size() == 16 && state.accepted == 16 && state.transmitted == 16, "incomplete/extra serialized output");
        check(report["dma"]["irq"] == 0 && report["sport"]["tcr1"] == 0 && state.fifo == 0 &&
              !state.holding && !state.shifting && state.overflows == 0 && state.underflows == 1,
              "unexpected final DMA/SPORT state");
        for (unsigned i = 0; i < words.size(); ++i) {
            const auto& w = words[i];
            check(w.value == 0x12340001u + i * 0x101u && w.bits == 32 && w.slot == i % 2 &&
                  !w.lsbFirst && !w.repeated && w.valid && w.index == i, "native word value/order/format mismatch");
            if (i) check(w.ns - words[i - 1].ns == 32'000, "unexpected synthetic serial period");
        }
        unsigned atIsr = 0;
        for (const auto& w : words) if (w.ns <= uint64_t(m("dma_entry_cycles")) * 5 / 2) ++atIsr;
        report["serialized_at_dma_isr"] = atIsr;
        check(atIsr > 0 && atIsr < 16 && !words.empty() && words.back().ns > uint64_t(m("dma_ack_cycles")) * 5 / 2,
              "DMA completion was conflated with transport drain");
        check(m("timer_phase") == 3 && m("timer_entry_cycles") > m("guard_start_cycles") + 79'999,
              "post-DMA drain guard did not run");
    } else {
        check(m("signature") == 0x5354414c && m("dma_entries") == 0 && m("isr_tag") == 0 && m("after_rti") == 0,
              "clockless guest unexpectedly completed DMA");
        check(m("timer_phase") == 2 && m("final_dma_count") == 12 && m("final_dma_address") == 0x3010,
              "clockless DMA did not stall after finite FIFO acceptance");
        check(state.accepted == 4 && state.fifo == 4 && !state.holding && !state.shifting && words.empty() &&
              state.transmitted == 0 && state.status == 0x48 && state.overflows == 0 && state.underflows == 0,
              "clockless transport buffering/output incorrect");
    }
    report["errors"] = errors;
    std::ofstream(argv[2] + std::string(".json")) << report.dump(2) << '\n';
    if (!errors.empty()) { std::cerr << report.dump(2) << '\n'; return 1; }
    std::cout << (clock ? "clock" : "no-clock") << ": guest PASS, " << state.accepted << " accepted, "
              << m("dma_entries") << " DMA IRQ, " << state.transmitted << " serialized; " << runs << " CPU runs\n";
    return 0;
}

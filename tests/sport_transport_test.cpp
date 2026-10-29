// Register/capacity oracles: BF52x HRM ch.24 and ch.5. Event timestamps
// additionally qualify our documented word-level timing abstraction.
#include "cpu/sport.h"
#include "cpu/sic.h"
#include "cpu/time_source.h"
#include "peripheral/sport_capture.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#undef W
#undef R

static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
constexpr u32 kSport = 0xffc00800, kDMA = 0xffc00c00, kChannel = kDMA + 4 * 0x40;

struct Rig {
    TimeSource time{true};
    Emulator emu;
    MemoryDevice ram{"RAM", 0, 0x10000};
    DMA dma{kDMA, emu, &time};
    SIC sic{0xffc00100};
    std::shared_ptr<SPORT> sport = std::make_shared<SPORT>(kSport, 0, time);
    std::vector<SportTxTransport::Word> words;
    std::vector<SportTxTransport::Word> wire;
    std::vector<std::string> transitions;
    bool dmaLine = false, pioLine = false;
    std::function<void(const SportTxTransport::Word&)> sink;
    Rig() {
        emu.BindDevice(&ram); emu.BindDevice(&dma); emu.BindDevice(sport.get()); emu.BindDevice(&sic);
        sport->EnableTxTransport();
        dma.AttachDMABus(DMAPeripheralSPORT0Tx, sport);
        dma.BindInterrupt(4, 17, [this](int q, int v) { dmaLine = v; Line(q, dmaLine || pioLine); });
        sport->BindTransmitInterrupt(17, [this](int q, int v) { pioLine = v; Line(q, dmaLine || pioLine); });
        sport->BindErrorInterrupt(8, [this](int q, int v) { Line(q, v); });
        sport->TxTransport().SetWordCallback([this](const auto& word) {
            wire.push_back(word);
            if (word.valid && !word.repeated) words.push_back(word);
            if (sink) sink(word);
        });
    }
    void Line(int q, int level) {
        const bool before = sic.Read32(0x20) & (1u << q);
        sic.SetInterruptLevel(q, level);
        if (before != bool(level))
            transitions.push_back(std::to_string(time.Nanoseconds()) + ":" + std::to_string(q) + "=" + std::to_string(level));
    }
    void W(u32 addr, u32 value) { emu.MemoryWrite16(addr, u16(value)); }
    u32 R(u32 addr) { return emu.MemoryRead16(addr); }
    void Config(unsigned bits = 16, bool stereo = false) {
        W(kSport + 4, (bits - 1) | (stereo ? 0x200 : 0));
        sport->TxTransport().SetExternalClock(1'000'000, bits * (stereo ? 2 : 1), false, true);
        W(kSport, 0x4401); // External TSCLK/TFS, required FS, falling-edge TX.
    }
    void StartDMA(unsigned count, unsigned bytes = 2, int modify = 0) {
        for (unsigned i = 0; i < count; ++i)
            emu.MemoryWrite32(0x1000 + i * (modify ? modify : bytes), 0x12340000u + i * 0x101u + 1);
        emu.MemoryWrite32(kChannel + 4, 0x1000);
        W(kChannel + 0x10, count);
        W(kChannel + 0x14, modify ? modify : bytes);
        W(kChannel + 8, 0x81 | (bytes == 4 ? 8 : 4)); // stop, IRQ enabled
        Service();
    }
    void Service() {
        sport->ProcessWithInterrupt(-1);
        for (int pass = 0; pass < 4 && dma.NeedsService(time.Nanoseconds()); ++pass)
            dma.ProcessWithInterrupt(-1);
    }
    void To(uint64_t ns, const std::function<void()>& pio = {}) {
        Service(); if (pio) pio();
        const auto end = TimeSource::CyclesAt(ns);
        while (time.Cycles() < end) {
            auto next = TimeSource::CyclesAt(std::min(sport->TxDeadlineNs(), dma.IdleUntilNs()));
            next = std::min(end, std::max(next, time.Cycles() + 1));
            time.Idle(next - time.Cycles());
            Service(); if (pio) pio();
        }
    }
    auto State() { return sport->TxTransport().Inspect(); }
    std::vector<u32> Values() const { std::vector<u32> out; for (auto& w : words) out.push_back(w.value); return out; }
    std::string Fingerprint() {
        std::ostringstream s;
        for (auto& w : wire) s << w.ns << ':' << w.index << ':' << w.value << ':' << unsigned(w.bits) << ':' << unsigned(w.slot)
                               << ':' << w.repeated << ':' << w.valid << ';';
        for (auto& t : transitions) s << t << ';';
        auto state = State();
        s << state.accepted << ':' << state.transmitted << ':' << state.fifo << ':' << R(kChannel + 0x30);
        return s.str();
    }
};

static void NoClockAndRestart() {
    for (unsigned bits : {16, 32}) {
        Rig r; r.Config(bits); r.StartDMA(32, bits / 8);
        const unsigned capacity = bits == 16 ? 8 : 4;
        Check(r.State().accepted == capacity && r.State().transmitted == 0, "no clock: only documented FIFO capacity accepted");
        Check(r.R(kChannel + 0x30) == 32 - capacity - (bits == 32 ? 4 : 0), "32-bit DMA memory count includes controller prefetch");
        Check(r.R(kSport + 0x30) == 0x48, "clockless TXF set, TXHRE empty, no underflow");
        r.To(100'000);
        Check(r.State().accepted == capacity && r.words.empty(), "clockless interval has no output or DMA progress");
        r.sport->TxTransport().SetExternalSignals(true, true);
        r.To(180'000);
        Check(!r.words.empty() && r.R(kChannel + 0x30) < 32 - capacity, "clock start resumes FIFO drain and DMA");
        r.sport->TxTransport().SetExternalSignals(false, true);
        const auto count = r.State().transmitted, accepted = r.State().accepted;
        r.To(500'000);
        Check(r.State().transmitted == count && r.State().accepted == accepted, "stopped clock preserves buffered/partial data");
        r.sport->TxTransport().SetExternalSignals(true, true);
        r.To(2'000'000);
        Check(r.words.size() == 32 && r.R(kChannel + 0x30) == 0, "all accepted data eventually serialized");
        for (unsigned i = 0; i < 32; ++i) {
            const auto expected = (0x12340000u + i * 0x101u + 1) & (bits == 16 ? 0xffffu : ~0u);
            Check(r.words[i].value == expected, "restart preserves source order and word values");
            Check(!(r.words[i].ns > 180'000 && r.words[i].ns <= 500'000), "no native timestamps in clockless gap");
        }
    }
}

static void PioDMAAndStatus() {
    Rig pio, dma;
    pio.Config(); dma.Config();
    std::vector<u32> input;
    for (unsigned i = 0; i < 24; ++i) input.push_back(0x101 * i + 1);
    unsigned cursor = 0;
    auto feed = [&] { while (cursor < input.size() && !(pio.R(kSport + 0x30) & 8)) pio.W(kSport + 0x10, input[cursor++]); };
    feed(); dma.StartDMA(24);
    pio.sport->TxTransport().SetExternalSignals(true, true);
    dma.sport->TxTransport().SetExternalSignals(true, true);
    pio.To(1'000'000, feed); dma.To(1'000'000);
    Check(pio.Values() == input && dma.Values() == input, "PIO and DMA share the same serialized data path");
    for (size_t i = 0; i < input.size(); ++i) Check(pio.words[i].ns == dma.words[i].ns, "PIO/DMA serialization timing matches");
    Check(dma.R(kChannel + 0x28) == 1 && dma.dmaLine, "DMA completion IRQ precedes/does not imply TX drain");
    Check(!dma.pioLine, "DMAEN suppresses SPORT PIO service interrupt even after DONE");
    dma.W(kChannel + 0x28, 0); Check(dma.dmaLine, "writing zero cannot acknowledge DMA_DONE");
    dma.W(kChannel + 0x28, 1); Check(!dma.dmaLine, "DMA_DONE W1C deasserts IRQ");
    dma.W(kChannel + 8, 0); Check(dma.pioLine, "disabling DMA restores TX service level");
    Check(dma.emu.MemoryRead32(0xffc00120) & (1u << 17), "SIC receives SPORT TX service on source17");
    Check(dma.R(kSport + 0x30) & 16, "active FS with empty transmitter sets TUVF");
    Check(dma.wire.back().repeated && dma.wire.back().valid && dma.wire.back().value == input.back(), "underflow retransmits last data, marked separately from new DMA acceptance");
    Check(dma.emu.MemoryRead32(0xffc00120) & (1u << 8), "SPORT error interrupt on source8");
    dma.W(kSport + 0x30, 0); Check(dma.R(kSport + 0x30) & 16, "zero does not acknowledge TUVF");
    dma.W(kSport + 0x30, 16); Check(!(dma.R(kSport + 0x30) & 16), "TUVF W1C acknowledges error");
    Check(!(dma.emu.MemoryRead32(0xffc00120) & (1u << 8)), "error level clears on acknowledgment");

    Rig full; full.Config();
    for (int i = 0; i < 9; ++i) full.W(kSport + 0x10, i);
    Check(full.State().accepted == 8 && full.State().overflows == 1, "full FIFO rejects extra PIO write and sets TOVF");
    Check(full.R(kSport + 0x30) == 0x68, "TXF/TOVF/TXHRE status bits");
    full.W(kSport + 0x30, 32); Check(full.R(kSport + 0x30) == 0x48, "TOVF W1C leaves TXF unchanged");
    full.W(kSport, 0);
    Check(full.State().fifo == 0 && full.R(kSport + 0x30) == 0x40 && !full.pioLine, "disable aborts and clears TX state");
    Check(full.R(kSport + 4) == 15, "disable retains word configuration");
    full.sport->TxTransport().SetExternalSignals(true, true); full.To(1'000'000);
    Check(full.words.empty(), "disabled TX cannot transmit stale FIFO data");
}

static void FrameSyncAndTiming() {
    Rig r; r.Config(32, true); r.StartDMA(8, 4);
    Check(r.R(kSport + 4) == 0x21f, "TSFSE is bit9, TXSE is bit8");
    r.sport->TxTransport().SetExternalSignals(true, false);
    r.To(100'000);
    Check(r.words.empty() && r.State().holding && !r.State().shifting, "clock without FS only stages holding data");
    Check(!(r.R(kSport + 0x30) & 0x40), "TXHRE is zero when holding register contains data");
    r.sport->TxTransport().SetExternalSignals(true, true); r.To(140'000);
    Check(r.words.empty(), "no completed word before all32 serial clocks");
    r.To(161'000);
    Check(r.words.size() == 1 && r.words[0].ns == 161'000, "word completion is timed from frame start, not acceptance");
    r.sport->TxTransport().SetExternalSignals(true, false); r.To(500'000);
    Check(r.words.size() == 2, "removing FS lets current word finish but starts no further words");
    r.sport->TxTransport().SetExternalSignals(true, true); r.To(1'000'000);
    Check(r.words.size() == 8, "FS restart resumes remaining buffered words");

    Rig internal;
    internal.W(kSport + 4, 15);
    internal.W(kSport + 8, 2); // TSCLK = 100 MHz / 6 (not truncated Hz).
    internal.W(kSport + 0xc, 15);
    internal.W(kSport + 0x10, 0x55aa);
    internal.W(kSport, 0x603); // internal clock+FS, required FS
    internal.To(1'020);
    Check(internal.words.size() == 1 && internal.words[0].ns == 1'020, "internal divider retains rational clock timing");
    Check(internal.words[0].value == 0x55aa, "preloaded PIO value survives enable");
}

static std::string Repeat() {
    Rig r; r.Config(32, true); r.StartDMA(32, 4, 8);
    r.To(11'237); r.sport->TxTransport().SetExternalSignals(true, true);
    r.To(87'889); r.sport->TxTransport().SetExternalSignals(false, true);
    r.To(287'891); r.sport->TxTransport().SetExternalSignals(true, false);
    r.To(488'893); r.sport->TxTransport().SetExternalSignals(true, true);
    r.To(2'000'000);
    Check(r.words.size() == 32, "strided source completes");
    return r.Fingerprint();
}

static void AcceptanceIsNotDrainAndPacking() {
    Rig r; r.Config(); r.StartDMA(8);
    Check(r.R(kChannel + 0x28) == 1 && r.words.empty(), "DMA_DONE may precede every serialized word");
    r.sport->TxTransport().SetExternalSignals(true, true); r.To(200'000);
    Check(r.words.size() == 8, "SPORT drains after completed DMA stops being runnable");
    Rig packed; packed.Config(); packed.StartDMA(12, 4);
    packed.sport->TxTransport().SetExternalSignals(true, true); packed.To(1'000'000);
    Check(packed.words.size() == 24, "32-bit DMA elements accept pairs of16-bit peripheral words");
    for (unsigned i = 0; i < 12; ++i) {
        Check(packed.words[2*i].value == 0x101*i + 1 && packed.words[2*i+1].value == 0x1234, "DMA short writes never lose partial memory elements");
    }
    Rig unsupported; unsupported.Config(); unsupported.W(kSport + 4, 0x10f);
    unsupported.StartDMA(16); unsupported.sport->TxTransport().SetExternalSignals(true, true); unsupported.To(200'000);
    Check(!unsupported.State().supported && unsupported.State().accepted == 0 && unsupported.words.empty(), "unimplemented secondary channel fails closed");
}

static void NoRetroactiveOutput() {
    Rig r; r.Config(); r.sport->TxTransport().SetExternalSignals(true, true);
    r.time.Idle(TimeSource::CyclesAt(1'000'000)); // intentionally no peripheral service
    r.W(kSport + 0x10, 0x5678); // must settle the empty past before this write
    Check(r.words.empty(), "late PIO data cannot be serialized into past empty slots");
    r.To(1'040'000);
    Check(r.words.size() == 1 && r.words[0].ns > 1'000'000, "late data uses a future frame");
    r.W(kSport + 0x10, 0xaaaa); r.To(1'050'000);
    Check(r.State().shifting, "there is a partially transmitted word to abort");
    r.W(kSport, 0); const auto before = r.words.size();
    r.To(1'100'000);
    Check(r.words.size() == before && r.R(kSport + 0x30) == 0x40, "disable discards partial word without capture and resets flags");
    Rig cold;
    Check(cold.R(kSport) == 0 && cold.R(kSport + 4) == 0 && cold.R(kSport + 0x30) == 0x40 && cold.State().accepted == 0,
          "new device reset values and counters");
    Rig reconfigure; reconfigure.Config(); reconfigure.W(kSport + 8, 2);
    Check(!reconfigure.State().supported, "live reconfiguration fails closed in the qualified subset");
    reconfigure.W(kSport, 0); reconfigure.W(kSport, 0x4401);
    Check(reconfigure.State().supported, "disable/enable restores support after reconfiguration");
    Rig disabledDMA; disabledDMA.Config(); disabledDMA.StartDMA(32);
    disabledDMA.W(kChannel + 8, 0);
    disabledDMA.sport->TxTransport().SetExternalSignals(true, true); disabledDMA.To(200'000);
    Check(disabledDMA.words.size() == 8 && disabledDMA.State().accepted == 8, "DMA disable preserves and drains only accepted peripheral words");
}

static void Demo(const std::string& prefix, bool noClock) {
    Rig r; SportCapture capture(prefix);
    r.sink = [&](const auto& word) { capture.Write(word); };
    r.Config(32, true); r.StartDMA(16, 4);
    auto show = [&](const char* event) {
        auto s = r.State();
        std::printf("ns=%llu event=%s accepted=%llu transmitted=%llu dma_addr=%08x dma_count=%u fifo=%zu hold=%d shift=%d stat=%04x dma_irq=%x sic=%08x\n",
            (unsigned long long)r.time.Nanoseconds(), event, (unsigned long long)s.accepted,
            (unsigned long long)s.transmitted, r.emu.MemoryRead32(kChannel + 0x24), r.R(kChannel + 0x30), s.fifo,
            s.holding, s.shifting, s.status, r.R(kChannel + 0x28), r.emu.MemoryRead32(0xffc00120));
    };
    std::puts("fixture=synthetic-mmio sport=0 dma=4 pmap=0x4000 tcr1=0x4401 tcr2=0x021f external_bit_hz=1000000 frame_clocks=64 codec=unmodeled capture=native");
    show("no-clock"); r.To(100'000); show("backpressure");
    if (!noClock) {
        r.sport->TxTransport().SetExternalSignals(true, true); show("clock-start");
        r.To(200'000); show("running");
        r.sport->TxTransport().SetExternalSignals(false, true); r.To(400'000); show("clock-stopped");
        r.sport->TxTransport().SetExternalSignals(true, true);
        r.To(500'000); show("restarted"); r.To(813'000); show("drained");
        r.W(kSport, 0); show("disabled");
        Check(r.words.size() == 16, "demo emits all16 native words");
    } else Check(r.words.empty() && r.State().accepted == 4, "missing-clock demo stalls after4 FIFO words");
    for (const auto& transition : r.transitions) std::printf("irq_transition=%s\n", transition.c_str());
    capture.Finish();
    std::ifstream pcm(prefix + ".pcm", std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(pcm)), {});
    Check(bytes.size() == (noClock ? 0u : 64u), "native capture file contains exactly the serialized words");
    for (size_t i = 0; i < bytes.size(); ++i)
        Check(bytes[i] == u8((0x12340001u + (i / 4) * 0x101u) >> (8 * (i % 4))), "native file is serialized side little-endian PCM");
}

int main(int argc, char** argv) {
    try {
        if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--demo") {
            Check(argc != 4 || std::string(argv[3]) == "--no-clock", "unknown demo option");
            Demo(argv[2], argc == 4 && std::string(argv[3]) == "--no-clock"); return 0;
        }
        Check(argc == 1, "usage: op1-sport-transport-test [--demo PREFIX [--no-clock]]");
        NoClockAndRestart(); PioDMAAndStatus(); FrameSyncAndTiming(); AcceptanceIsNotDrainAndPacking(); NoRetroactiveOutput();
        Check(Repeat() == Repeat(), "identical schedule gives identical samples, timestamps, counters and IRQ transitions");
        std::puts("sport-transport-test: 6 groups passed (16/32-bit, stereo, strided/packed DMA, PIO, clocks/FS, status/IRQ, repeatability)");
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}

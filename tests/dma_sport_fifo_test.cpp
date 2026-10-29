// BF52x HRM rev1.0 6-6, figure6-2, 6-22/23: functional memory/peripheral
// ordering, independent of any assumed bus latency or SPORT serial timing.
#include "cpu/dma.h"
#include "cpu/time_source.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#undef R
#undef W
static void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Sink : DMABus {
    unsigned room = 4;
    std::vector<u32> values;
    bool BufferedTransmit() const override { return true; }
    u32 DMARead(int, int, void*, u32) override { return 0; }
    u32 DMAWriteTake(u32 n) const override { return std::min(n, room * 4); }
    u32 DMAWrite(int, int, const void* p, u32 n) override {
        n = DMAWriteTake(n);
        const auto* words = static_cast<const u32*>(p);
        values.insert(values.end(), words, words + n / 4); room -= n / 4; return n;
    }
    uint64_t DMAIdleUntil(bool, u32 n) const override { return DMAWriteTake(n) ? 0 : UINT64_MAX; }
    void Grant(unsigned n) { room += n; NotifyDMA(); }
};
struct Rig {
    TimeSource time{true};
    Emulator emu;
    MemoryDevice ram{"RAM", 0, 0x10000};
    DMA dma{0xffc00c00, emu, &time};
    std::shared_ptr<Sink> sink = std::make_shared<Sink>();
    std::vector<DMA::TxEvent> events;
    bool irq = false;
    Rig() {
        emu.BindDevice(&ram); emu.BindDevice(&dma);
        dma.AttachDMABus(DMAPeripheralSPORT0Tx, sink);
        dma.BindInterrupt(4, 17, [&](int, int v) { irq = v; });
        dma.SetTxObserver([&](auto& e) { events.push_back(e); });
    }
    void W(unsigned off, u32 v) { emu.MemoryWrite32(0xffc00d00 + off, v); }
    void Service() { dma.ProcessWithInterrupt(-1); }
    void Start(bool sync, unsigned count = 16, bool interrupt = true) {
        for (unsigned i = 0; i < count; ++i) emu.MemoryWrite32(0x1000 + i * 4, i + 1);
        W(4, 0x1000); W(0x10, count); W(0x14, 4);
        W(8, 9 | (sync ? 0x20 : 0) | (interrupt ? 0x80 : 0)); Service();
    }
};
int main() {
    for (bool sync : {false, true}) {
        Rig r; r.Start(sync);
        auto s = r.dma.InspectTx(4);
        Check(s.modeled && s.read == 8 && s.accepted == 4 && s.buffered == 4 && s.remaining == 8 &&
              s.address == 0x1020 && s.running && !s.done && !r.irq, "separate memory/peripheral FIFOs");
        // Premature reuse: prefetched words survive, unread words change.
        for (unsigned i = 0; i < 16; ++i) r.emu.MemoryWrite32(0x1000 + i * 4, 1000 + i);
        r.sink->Grant(4); r.Service();
        s = r.dma.InspectTx(4);
        Check(s.read == 12 && s.accepted == 8 && s.buffered == 4 && !s.done, "refill/drain ordering");
        r.sink->Grant(4); r.Service();
        s = r.dma.InspectTx(4);
        Check(s.read == 16 && s.accepted == 12 && s.buffered == 4 && s.running &&
              s.done == !sync && r.irq == !sync, "SYNC0 completion at final memory read, SYNC1 waits");
        r.W(0x28, 0); Check(r.irq == !sync, "zero must not acknowledge");
        if (!sync) {
            r.W(0x28, 1); Check(!r.irq, "W1C must acknowledge while FIFO still drains");
            // Final memory read has released the source: even the four words
            // still inside DMA must survive a new producer overwriting RAM.
            for (unsigned i = 0; i < 16; ++i) r.emu.MemoryWrite32(0x1000 + i * 4, 9000 + i);
        }
        r.sink->Grant(4); r.Service();
        s = r.dma.InspectTx(4);
        Check(!s.running && !s.buffered && s.accepted == 16 && r.irq == sync,
              "DMA_RUN must persist through DMA FIFO drain without repeated SYNC0 IRQ");
        for (unsigned i = 0; i < 16; ++i)
            Check(r.sink->values[i] == (i < 8 ? i + 1 : 1000 + i), "premature reuse prefetch boundary");
        unsigned complete = 0;
        for (auto& e : r.events) if (std::string(e.operation) == "complete") {
            ++complete; Check(e.state.buffered == (sync ? 0u : 4u) && e.state.running == !sync,
                              "completion event boundary");
        }
        Check(complete == 1, "one completion per work unit");
    }
    Rig blocked; blocked.sink->room = 0; blocked.Start(true);
    Check(blocked.dma.InspectTx(4).read == 4 && blocked.dma.InspectTx(4).buffered == 4 &&
          blocked.dma.IdleUntilNs() == UINT64_MAX, "cold memory prefetch is bounded without peripheral grants");
    blocked.W(8, 0); blocked.Service();
    Check(!blocked.dma.InspectTx(4).buffered && !blocked.dma.InspectTx(4).running,
          "disable discards only the controller FIFO");
    Rig noIrq; noIrq.Start(false, 8, false);
    Check(noIrq.dma.InspectTx(4).remaining == 0 && !noIrq.dma.InspectTx(4).done && !noIrq.irq,
          "DI_EN0 must not publish completion interrupt status");
    std::cout << "DMA/SPORT finite32 FIFO and SYNC ordering PASS\n";
}

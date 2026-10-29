// Firmware-246 display DMA programming, reconstructed from the original
// OP-1's selected display path and checked against the BF52x register map.
// This tests the requested memory-to-PPI transfer, not panel geometry,
// refresh rate, successful scanout, or hardware timing.
//
// DMA0: START_ADDR=B (forward) or B+0x27e00 (reverse);
// CONFIG=0x34, then DMAEN (0x35); PMAP=0; X_COUNT=156, X_MODIFY=2;
// Y_COUNT=320; Y_MODIFY=0x00ca (+202) or 0xfcca (-822).
// Both cases traverse rows 512 bytes apart, but transfer only 312 bytes
// from each row. No firmware, NAND image, or device is needed.
#include "cpu/dma.h"
#include "cpu/emu.h"
#include "cpu/ppi.h"
#include "cpu/time_source.h"
#include "peripheral/display.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kColumns = 156;
constexpr int kRows = 320;
constexpr int kPitch = 512;
constexpr int kRowBytes = kColumns * 2;
constexpr int kFrameBytes = kRows * kPitch;
constexpr u32 kBase = 0x1000;
constexpr u32 kRowCycles = kColumns * kSclkCyclesPerElement;

static_assert((kColumns - 1) * 2 + 202 == kPitch, "forward stride");
static_assert((kColumns - 1) * 2 - 822 == -kPitch, "reverse stride");
static_assert((kRows - 1) * kPitch == 0x27e00, "reverse start offset");

void Check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

class CaptureDisplay : public Display {
public:
    CaptureDisplay(const std::vector<u8>& source, bool reverse)
        : source_(source), reverse_(reverse) {}

    void Initialize(int columns, int rows) override {
        Check(columns == kColumns && rows == kRows, "PPI initialized fixture dimensions");
        ++initializations_;
    }

    void UpdateRowBuffer(int x, int y, const void* data, int length) override {
        const std::string where = std::string(reverse_ ? "reverse" : "forward") +
                                  " row " + std::to_string(received_);
        Check(initializations_ == 1, where + ": display initialized");
        Check(x == 0 && y == received_, where + ": DMA supplies rows in scan order");
        Check(length == kRowBytes, where + ": exactly 156 halfwords, no padding");
        Check(data != nullptr, where + ": row has data");
        const int sourceRow = reverse_ ? kRows - 1 - y : y;
        const u8* expected = source_.data() + sourceRow * kPitch;
        Check(std::memcmp(data, expected, kRowBytes) == 0,
              where + ": pixels must come from the correct pitched SDRAM row");
        ++received_;
    }

    int Received() const { return received_; }
    int Initializations() const { return initializations_; }

private:
    const std::vector<u8>& source_;
    bool reverse_;
    int received_ = 0;
    int initializations_ = 0;
};

void Run(bool reverse) {
    const std::string direction = reverse ? "reverse" : "forward";
    std::vector<u8> framebuffer(kFrameBytes, 0xD7);
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            // Distinct rows and columns; padding remains 0xD7 and must
            // never be passed to the display.
            const u16 pixel = static_cast<u16>((y * 257u) ^ (x * 73u) ^ 0xA57Cu);
            const int offset = y * kPitch + x * 2;
            framebuffer[offset] = static_cast<u8>(pixel);
            framebuffer[offset + 1] = static_cast<u8>(pixel >> 8);
        }
    }

    TimeSource time(true);
    Emulator emulator;
    MemoryDevice ram("test SDRAM", 0, 0x40000);
    emulator.BindDevice(&ram);
    emulator.MemoryWrite(kBase, framebuffer.data(), static_cast<int>(framebuffer.size()));

    auto display = std::make_shared<CaptureDisplay>(framebuffer, reverse);
    auto ppi = std::make_shared<PPI>(0xFFC01000);
    ppi->AttachDisplay(display);
    // These PPI dimension writes are fixture setup, not a claim about the
    // firmware's PPI_COUNT/PPI_FRAME instruction sequence.
    ppi->Write32(0x08, kColumns - 1);
    ppi->Write32(0x10, kRows);
    ppi->Write32(0x00, 1); // PPI_CONTROL.PORT_EN
    Check(display->Initializations() == 1, direction + ": PPI enabled");

    DMA dma(0xFFC00C00, emulator, &time);
    dma.AttachDMABus(DMAPeripheralPPI, ppi);
    const u32 firstRow = reverse ? kRows - 1 : 0;
    dma.Write32(0x04, kBase + firstRow * kPitch); // DMA0_START_ADDR
    dma.Write32(0x10, kColumns);                  // X_COUNT
    dma.Write32(0x14, 2);                         // X_MODIFY, byte units
    dma.Write32(0x18, kRows);                     // Y_COUNT
    dma.Write32(0x1C, reverse ? 0xFCCA : 0x00CA); // signed Y_MODIFY
    dma.Write32(0x2C, 0);                         // PMAP0: PPI
    dma.Write32(0x08, 0x34);                      // configure, disabled
    Check((dma.Read32(0x28) & 8) == 0, direction + ": DMA not yet running");
    dma.Write32(0x08, 0x35);                      // set DMAEN
    Check((dma.Read32(0x28) & 8) != 0, direction + ": DMA running");

    for (int y = 0; y < kRows; ++y) {
        time.Retire(kRowCycles - 1);
        dma.ProcessWithInterrupt(-1);
        Check(display->Received() == y, direction + ": row must not arrive early at " +
                                              std::to_string(y));

        time.Retire(1);
        dma.ProcessWithInterrupt(-1);
        Check(display->Received() == y + 1, direction + ": row due at " + std::to_string(y));

        const int remaining = kRows - y - 1;
        Check(dma.Read32(0x38) == static_cast<u32>(remaining),
              direction + ": remaining DMA rows at " + std::to_string(y));
        Check(dma.Read32(0x30) == static_cast<u32>(remaining ? kColumns : 0),
              direction + ": current X count at " + std::to_string(y));
        if (remaining) {
            const int nextSource = reverse ? kRows - y - 2 : y + 1;
            const u32 expected = kBase + nextSource * kPitch;
            Check(dma.Read32(0x24) == expected,
                  direction + ": next row address is exactly 512 bytes away");
        }
        const u32 status = dma.Read32(0x28);
        Check((status & 2) == 0, direction + ": no DMA_ERR");
        Check(((status & 8) != 0) == (remaining != 0),
              direction + ": DMA_RUN ends after row 320");
        Check(((status & 1) != 0) == (remaining == 0),
              direction + ": DMA_DONE only after final row");
    }

    time.Retire(kRowCycles);
    dma.ProcessWithInterrupt(-1);
    Check(display->Received() == kRows, direction + ": FLOW=stop, no extra row");
}
} // namespace

int main() {
    try {
        Run(false);
        Run(true);
        std::puts("display-dma-test: forward and reverse stock246 row transfers passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

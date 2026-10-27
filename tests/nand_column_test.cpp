// Exercise the actual NAND page DMA methods with valid and out-of-range
// 12-bit column addresses. No firmware, OTP data, or NAND image is needed.
#include "cpu/cpu.h"
#include "peripheral/MT29F4G08.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
static constexpr u32 kPageBytes = 2048 + 64;

static void Require(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        std::exit(1);
    }
}

struct ScratchImage {
    fs::path path;
    ScratchImage() {
        auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("op1-nand-column-" + std::to_string(unique) + ".img");
        std::ofstream file(path, std::ios::binary);
        Require(file.good(), "create empty snapshot image");
    }
    ~ScratchImage() {
        std::error_code ignored;
        fs::remove(path, ignored);
    }
};

class TestFlash : public MT29F4G08 {
public:
    using MT29F4G08::MT29F4G08;
    u32 Offset() const { return dataOffset; }
    std::vector<u8>& Page() { return pageBuffer; }
    const std::vector<u8>& Program() const { return programBuffer; }
};

static u32 ExpectedBytes(u32 column, u32 request) {
    return column >= kPageBytes ? 0 : std::min(request, kPageBytes - column);
}

// Random-read 05/E0 selects a column without loading a page from backing storage.
static void SelectReadColumn(TestFlash& flash, u32 column) {
    flash.SendCommand(0x05);
    flash.SendAddress(static_cast<u8>(column));
    flash.SendAddress(static_cast<u8>(column >> 8));
    flash.SendCommand(0xE0);
    Require(flash.Offset() == column, "random-read column decoded correctly");
}

// Page-program 80 + five address cycles selects the target column.
static void SelectWriteColumn(TestFlash& flash, u32 column) {
    flash.SendCommand(0x80);
    flash.SendAddress(static_cast<u8>(column));
    flash.SendAddress(static_cast<u8>(column >> 8));
    flash.SendAddress(0);
    flash.SendAddress(0);
    flash.SendAddress(0);
    Require(flash.Offset() == column, "page-program column decoded correctly");
}

static void CheckRead(TestFlash& flash, u32 column, u32 requested) {
    SelectReadColumn(flash, column);
    std::array<u8, 32> dest;
    dest.fill(0xA5);

    const u32 actual = flash.PageRead(dest.data(), requested);
    const u32 expected = ExpectedBytes(column, requested);
    const auto context = "read at column " + std::to_string(column) +
                         " for " + std::to_string(requested);
    Require(actual == expected, context + ": transferred byte count");
    Require(flash.Offset() == column + expected, context + ": offset advanced only by bytes read");
    for (u32 i = 0; i < dest.size(); ++i) {
        const u8 want = i < expected ? flash.Page()[column + i] : u8{0xA5};
        Require(dest[i] == want, context + ": data and destination guard");
    }
}

static void CheckWrite(TestFlash& flash, u32 column, u32 requested) {
    SelectWriteColumn(flash, column);
    std::array<u8, 32> source;
    for (u32 i = 0; i < source.size(); ++i) source[i] = static_cast<u8>(i + 1);

    const u32 actual = flash.PageWrite(source.data(), requested);
    const u32 expected = ExpectedBytes(column, requested);
    const auto context = "write at column " + std::to_string(column) +
                         " for " + std::to_string(requested);
    Require(actual == expected, context + ": transferred byte count");
    Require(flash.Offset() == column + expected, context + ": offset advanced only by bytes written");
    Require(flash.Program().size() == kPageBytes, context + ": program buffer size");
    for (u32 i = 0; i < kPageBytes; ++i) {
        const u8 want = (i >= column && i - column < expected)
                            ? source[i - column] : u8{0xFF};
        Require(flash.Program()[i] == want, context + ": program buffer contents");
    }
}

int main() {
    ScratchImage image;
    BlackFinCpu cpu(true);
    TestFlash flash(cpu, image.path.string(), true);
    Require(flash.Page().size() == kPageBytes, "page buffer size");
    for (u32 i = 0; i < kPageBytes; ++i) {
        flash.Page()[i] = static_cast<u8>(i ^ 0x5A);
    }

    // Includes the first spare byte, final spare byte, one-past-end and
    // maximum representable 12-bit NAND column. Also test requests crossing
    // the page boundary instead of only one-byte accesses.
    for (u32 column : {0u, 2047u, 2048u, 2110u, 2111u, 2112u, 2113u, 4095u}) {
        for (u32 requested : {1u, 8u, 32u}) {
            CheckRead(flash, column, requested);
            CheckWrite(flash, column, requested);
        }
    }

    std::puts("nand-column-test: ok");
}

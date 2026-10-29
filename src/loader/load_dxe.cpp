#include "load_dxe.h"
#include "cpu/cpu.h"

void LoadDXE(BlackFinCpu& cpu, const DXE& dxe) {
    auto& emulator = cpu.GetEmulator();
    Emulator::OriginScope origin(emulator, Emulator::AccessOrigin::Loader);
    emulator.ObserveBoundary("dxe_load_begin");
    for (const auto& block : dxe.blocks) {
        if (!block.data.empty())
            cpu.GetEmulator().MemoryWrite(block.header.target_address, block.data.data(), block.data.size());
        if (block.IsFirstBlock()) {
            cpu.SetRegister(RegIndex::RETS, kLdrReturnAddress);
            cpu.SetPC(block.header.target_address);
        }
    }
    emulator.ObserveBoundary("dxe_load_end");
}

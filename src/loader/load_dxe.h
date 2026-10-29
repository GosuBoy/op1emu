#pragma once
#include "ldr.h"

class BlackFinCpu;

constexpr uint32_t kLdrReturnAddress = 0x08000000;
// Normal direct LDR/DXE path used by the frontend and synthetic guests.
// This does not emulate NAND boot-ROM initialization.
void LoadDXE(BlackFinCpu& cpu, const DXE& dxe);

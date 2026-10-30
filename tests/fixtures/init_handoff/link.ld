OUTPUT_FORMAT("elf32-bfin")
OUTPUT_ARCH(bfin)
ENTRY(_start)
SECTIONS {
  .bootstrap 0xFFA00100 : { *(.bootstrap) }
  .init 0xFFA01000 : { *(.init) }
  .main 0x00100000 : { *(.main) }
  /DISCARD/ : { *(.comment) *(.note*) }
  ASSERT(SIZEOF(.bootstrap) <= 0x400, "bootstrap too large")
  ASSERT(SIZEOF(.init) <= 0x1000, "INIT too large")
  ASSERT(SIZEOF(.main) <= 0x800, "MAIN overlaps sentinel")
}

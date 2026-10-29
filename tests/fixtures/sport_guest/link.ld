OUTPUT_FORMAT("elf32-bfin")
OUTPUT_ARCH(bfin)
ENTRY(_start)
MEMORY { CODE (rx) : ORIGIN = 0xFFA00100, LENGTH = 0x1000 }
SECTIONS {
  .text : { *(.text) } > CODE
  /DISCARD/ : { *(.comment) *(.note*) }
  ASSERT(SIZEOF(.text) <= 0x1000, "diagnostic exceeds code reservation")
}

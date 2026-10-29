/* Synthetic BF524 CS42L52/TWI/SPORT0/DMA4 component diagnostic; no vendor firmware/runtime.
 * Normal LDR reset context -> IVG15 background -> IDLE -> IVG9 ISR -> RTI.
 * See README.md for the mailbox ABI, stimulus and timing assumptions. */
.section .text
.global _start, supervisor, dma_isr, timer_isr, exception_isr
.global idle_dma_resume, idle_drain_resume
.global codec_setup, codec_configured, twi_wait, codec_failed
.equ result_addr, 0x00002000
.equ sample_addr, 0x00003000
.equ sample_words, 16
.macro SET32 reg, value
    \reg\().L = ((\value) & 0xffff);
    \reg\().H = (((\value) >> 16) & 0xffff);
.endm
.macro ADDR reg, symbol
    \reg\().L = \symbol;
    \reg\().H = \symbol;
.endm

_start:
    CLI R0;
    SET32 SP, 0xffb01000
    SET32 P0, 0xffe0203c /* EVT15 */
    ADDR R0, supervisor
    [P0] = R0;
    SET32 P0, 0xffe02024 /* EVT9: DMA4 */
    ADDR R0, dma_isr
    [P0] = R0;
    SET32 P0, 0xffe02018 /* EVT6: core timer */
    ADDR R0, timer_isr
    [P0] = R0;
    SET32 P0, 0xffe0200c /* EVT3: exceptions */
    ADDR R0, exception_isr
    [P0] = R0;
    R0 = 0x8240 (Z); /* CEC IVG15, IVG9, IVG6 */
    STI R0;
    RAISE 15;
    ADDR R0, supervisor
    RETI = R0;
    RTI;
never:
    JUMP.S never;

supervisor:
    [--SP] = RETI; /* permit higher priority interrupts in IVG15 */
    SET32 P5, 0x2000
    P1 = P5;
    R0 = 0;
    R1 = 48;
clear_result:
    [P1++] = R0;
    R1 += -1;
    CC = R1 == 0;
    IF !CC JUMP clear_result;
    R0 = 1;
    [P5 + 4] = R0; /* phase: initialized */

    SET32 P1, 0x3000
    SET32 R0, 0x12340000
    R1 = sample_words;
    SET32 R2, 0x00010100
fill_samples:
    [P1++] = R0;
    R0 = R0 + R2;
    R1 += -1;
    CC = R1 == 0;
    IF !CC JUMP fill_samples;
    R0 = sample_words;
    [P5 + 20] = R0;
    SET32 R5, 0x76543210 /* main-context canary */

    /* Mask other board IRQ/wakeup sources; route SIC17 to IVG9. */
    SET32 P0, 0xffc0010c /* SIC_IMASK0 */
    SET32 R0, 0x00020000
    [P0] = R0;
    SET32 P0, 0xffc0014c /* SIC_IMASK1 */
    R0 = 0;
    [P0] = R0;
    SET32 P0, 0xffc00118 /* SIC_IAR2: source17 bits7:4 */
    R0 = 0x20;
    [P0] = R0;
    SET32 P0, 0xffc00124 /* SIC_IWR0 */
    SET32 R0, 0x00020000
    [P0] = R0;
    SET32 P0, 0xffc00164 /* SIC_IWR1 */
    R0 = 0;
    [P0] = R0;

    CALL codec_setup;

    /* One-shot 2 ms core-timer watchdog, 400 MHz CCLK, scale0. */
    SET32 P0, 0xffe03000
    R0 = 0;
    [P0] = R0;
    [P0 + 8] = R0;
    SET32 R0, 420000
    [P0 + 4] = R0;
    R0 = 3; /* TMPWR | TMREN, no autoreload */
    [P0] = R0;

    /* Codec master clocks wired to SPORT0: 24 MSB payload in 32-bit slots. */
    SET32 P0, 0xffc00800
    R0 = 0;
    W[P0] = R0;
    W[P0 + 0x38] = R0;
    W[P0 + 0x3c] = R0;
    R0 = 0x021f;
    W[P0 + 4] = R0;
    SET32 P0, 0xffc00d00
    R0 = 0;
    W[P0 + 8] = R0;
    R0 = 3;
    W[P0 + 0x28] = R0; /* clear stale DONE/ERR */
    SET32 R0, 0x3000
    [P0 + 4] = R0;
    R0 = sample_words;
    W[P0 + 0x10] = R0;
    R0 = 4;
    W[P0 + 0x14] = R0;
    R0 = 0x4000;
    W[P0 + 0x2c] = R0;
    R0 = 0x0089; /* DMAEN | WDSIZE32 | DI_EN, one finite row */
    W[P0 + 8] = R0;
    SET32 P0, 0xffc00800
    R0 = 0x4401; /* external required FS/clock, falling-edge TX, TSPEN */
    W[P0] = R0;
    SSYNC;
    JUMP tx_started; /* CYCLES is refreshed at a basic-block boundary. */
tx_started:
    R0 = CYCLES;
    [P5 + 28] = R0;
    R0 = 2;
    [P5 + 4] = R0;

wait_dma:
    R0 = [P5 + 8];
    CC = R0 == 0;
    IF !CC JUMP after_dma;
    R0 = [P5 + 12];
    CC = R0 == 0;
    IF !CC JUMP stalled;
    R0 = [P5 + 24];
    R0 += 1;
    [P5 + 24] = R0;
    IDLE;
idle_dma_resume:
    JUMP wait_dma;

after_dma:
    SET32 R0, 0x52544939 /* RTI9: executed after guest ISR return */
    [P5 + 16] = R0;
    R0 = 3;
    [P5 + 4] = R0;
    /* DMA accepted its final word, but four DMA plus six SPORT words may remain.
     * At the codec-derived 3.072 MHz/32-bit rate, wait105 us after RTI.
     * The final new word finishes before this; a tail replay cannot finish. */
    SET32 P0, 0xffe03000
    R0 = 0;
    [P0] = R0;
    SET32 R0, 42000
    [P0 + 4] = R0;
    R0 = CYCLES;
    [P5 + 40] = R0;
    R0 = 3;
    [P0] = R0;
wait_drain:
    R0 = [P5 + 12];
    CC = R0 == 0;
    IF !CC JUMP drained;
    R0 = [P5 + 24];
    R0 += 1;
    [P5 + 24] = R0;
    IDLE;
idle_drain_resume:
    JUMP wait_drain;

drained:
    CALL snapshot;
    SET32 P0, 0xffc00800
    R0 = 0;
    W[P0] = R0; /* stop before a new underrun replay completes */
    SSYNC;
    R0 = [P5 + 8];
    CC = R0 == 1;
    IF !CC JUMP failed;
    [P5 + 100] = R5;
    SET32 R0, 0x50415353 /* PASS */
    [P5] = R0;
    JUMP finish;

stalled:
    CALL snapshot;
    [P5 + 100] = R5;
    SET32 R0, 0x5354414c /* STAL: watchdog, not DMA completion */
    [P5] = R0;
    JUMP finish;
failed:
    SET32 R0, 0x4641494c /* FAIL */
    [P5] = R0;
finish:
    R0 = CYCLES;
    [P5 + 48] = R0;
    SET32 P0, 0x08000000 /* normal LDR return sentinel */
    JUMP (P0);

snapshot:
    SET32 P0, 0xffc00d00
    R0 = W[P0 + 0x30] (Z);
    [P5 + 80] = R0;
    R0 = [P0 + 0x24];
    [P5 + 84] = R0;
    SET32 P0, 0xffc00830
    R0 = W[P0] (Z);
    [P5 + 88] = R0;
    SET32 P0, 0xffe0210c
    R0 = [P0];
    [P5 + 92] = R0;
    SET32 P0, 0xffe02108
    R0 = [P0];
    [P5 + 96] = R0;
    RTS;

.align 4
dma_isr:
    [--SP] = ASTAT;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = P0;
    [--SP] = P1;
    SET32 P1, 0x2000
    R0 = [P1 + 8];
    R0 += 1;
    [P1 + 8] = R0;
    R0 = CYCLES;
    [P1 + 32] = R0;
    R0 = RETI;
    [P1 + 76] = R0;
    SET32 P0, 0xffe02108
    R0 = [P0];
    [P1 + 72] = R0;
    SET32 P0, 0xffc00830
    R0 = W[P0] (Z);
    [P1 + 68] = R0;
    SET32 P0, 0xffc00d00
    R0 = W[P0 + 0x30] (Z);
    [P1 + 52] = R0;
    R0 = [P0 + 0x24];
    [P1 + 56] = R0;
    R0 = W[P0 + 0x28] (Z);
    [P1 + 60] = R0;
    R0 = 1;
    W[P0 + 0x28] = R0;
    SSYNC;
    R0 = W[P0 + 0x28] (Z);
    [P1 + 64] = R0;
    JUMP dma_acknowledged;
dma_acknowledged:
    R0 = CYCLES;
    [P1 + 36] = R0;
    SET32 R0, 0x444d4139 /* DMA9: written only by this guest ISR */
    [P1 + 116] = R0;
    P1 = [SP++];
    P0 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    ASTAT = [SP++];
    RTI;

.align 4
timer_isr:
    [--SP] = ASTAT;
    [--SP] = R0;
    [--SP] = P0;
    [--SP] = P1;
    SET32 P1, 0x2000
    R0 = [P1 + 12];
    R0 += 1;
    [P1 + 12] = R0;
    R0 = CYCLES;
    [P1 + 44] = R0;
    R0 = RETI;
    [P1 + 112] = R0;
    R0 = [P1 + 4];
    [P1 + 120] = R0;
    SET32 P0, 0xffe03000
    R0 = 0;
    [P0] = R0;
    SSYNC;
    P1 = [SP++];
    P0 = [SP++];
    R0 = [SP++];
    ASTAT = [SP++];
    RTI;

.align 4
exception_isr:
    SET32 P0, 0x2000
    R0 = SEQSTAT;
    [P0 + 104] = R0;
    R0 = RETX;
    [P0 + 108] = R0;
    SET32 R0, 0x45584350 /* EXCP */
    [P0] = R0;
    SET32 P0, 0x08000000
    JUMP (P0);

/* All register configuration below is guest-issued TWI traffic at 400 kHz.
 * CW verifies every written byte using the datasheet's MAP/STOP/read sequence.
 * No host register injection, private firmware, or C runtime is involved. */
.macro CW reg, value
    R0 = \reg;
    R1 = \value;
    CALL codec_write;
    R0 = \reg;
    CALL codec_read;
    CC = R0 == R1;
    IF CC JUMP 1f;
    JUMP.L codec_failed;
1:
.endm
.macro COMMAND reg, value
    R0 = \reg;
    R1 = \value;
    CALL codec_write;
.endm
.macro CHECKREAD reg, value, boxpos
    R0 = \reg;
    CALL codec_read;
    [P5 + \boxpos\()] = R0;
    R2 = \value;
    CC = R0 == R2;
    IF CC JUMP 1f;
    JUMP.L codec_failed;
1:
.endm

codec_setup:
    [--SP] = RETS;
    SET32 P4, 0xffc01400
    R0 = 0x0c0d; /* CLKHI12 + CLKLOW13, 10 MHz reference -> 400 kHz. */
    W[P4] = R0;
    R0 = 0x008a; /* TWI_ENA, PRESCALE10 */
    W[P4 + 4] = R0;
    R0 = 0x4a;
    W[P4 + 0x1c] = R0;
    R0 = 0;
    W[P4 + 0x24] = R0; /* TWI polling, no TWI interrupt enabled. */

    /* Declared synthetic PG12 -> codec RESET. Hold low for >=1 ms. */
    SET32 P0, 0xffc01504 /* PORTGIO_CLEAR */
    R0 = 0x1000;
    W[P0] = R0;
    SET32 P0, 0xffc01530 /* PORTGIO_DIR */
    W[P0] = R0;
    R0 = 1;
    CALL codec_map; /* Must be unavailable while RESET is low. */
    [P5 + 148] = R3;
    CC = BITTST(R3, 5);
    IF CC JUMP 1f;
    JUMP.L codec_failed;
1:
    R4 = CYCLES;
reset_wait:
    R2 = CYCLES;
    R2 = R2 - R4;
    SET32 R3, 400000
    CC = R2 < R3 (IU);
    IF CC JUMP reset_wait;
    SET32 P0, 0xffc01508 /* PORTGIO_SET */
    R0 = 0x1000;
    W[P0] = R0;
    SSYNC;
    R0 = CYCLES;
    [P5 + 152] = R0;
    CHECKREAD 1, 0xe3, 128 /* Declared B1 test identity. */
    CHECKREAD 2, 1, 132
    CHECKREAD 5, 0xa0, 136

    /* DS680F2 4.11 required initialization, including preserved reg32 bits. */
    COMMAND 0, 0x99
    COMMAND 0x3e, 0xba
    COMMAND 0x47, 0x80
    R0 = 0x32;
    CALL codec_read;
    R1 = R0;
    BITSET(R1, 7);
    R0 = 0x32;
    CALL codec_write;
    R0 = 0x32;
    CALL codec_read;
    CC = BITTST(R0, 7);
    IF CC JUMP 1f;
    JUMP.L codec_failed;
1:
    R1 = R0;
    BITCLR(R1, 7);
    R0 = 0x32;
    CALL codec_write;
    CHECKREAD 0x32, 0x3b, 144
    COMMAND 0, 0
    SET32 R0, 0x494e4954 /* INIT */
    [P5 + 140] = R0;

    /* Configure master/LRCK only with PDN=1 (4.9). Muting is independent. */
    CW 0x0c, 3
    CW 0x0d, 0x63
    CW 0x0e, 0 /* no soft ramp/zero-cross transition */
    CW 0x0f, 0xf0
    CW 5, 0x20 /* 12.288 MHz /256 -> 48 kHz; SCLK/LRCK64. */
    CW 6, 0 /* slave: no external serial source in this test wiring */
    CW 6, 0x80
    CW 5, 0 /* unsupported double-speed selection, no fallback */
    CW 5, 0x20
    CW 6, 0x84 /* unsupported I2S in this bounded model */
    CW 6, 0
    CW 7, 0x10 /* master serial pins tri-stated until configured */
    CW 6, 0x80 /* master, left justified, normal polarity */
    CW 7, 0
    CW 2, 0 /* global power-up, still muted */
codec_configured:
    R0 = CYCLES;
    [P5 + 156] = R0;
    RETS = [SP++];
    RTS;

codec_write: /* R0=MAP, R1=value. R1 preserved for readback comparison. */
    [--SP] = RETS;
    R2 = 0xff;
    W[P4 + 0x20] = R2;
    R2 = 0x3e;
    W[P4 + 0x18] = R2;
    R2 = R1 << 8;
    R2 = R2 | R0;
    W[P4 + 0x84] = R2;
    R2 = 0x81; /* DCNT2, MEN, write */
    W[P4 + 0x14] = R2;
    CALL twi_wait;
    CC = BITTST(R3, 4);
    IF CC JUMP 1f;
    JUMP.L codec_failed;
1:
    RETS = [SP++];
    RTS;

codec_map: /* R0=MAP; leaves R3 status, including the deliberate reset NACK. */
    [--SP] = RETS;
    R2 = 0xff;
    W[P4 + 0x20] = R2;
    R2 = 0x3e;
    W[P4 + 0x18] = R2;
    W[P4 + 0x80] = R0;
    R2 = 0x41; /* DCNT1 write; completion sends STOP, retaining MAP. */
    W[P4 + 0x14] = R2;
    CALL twi_wait;
    RETS = [SP++];
    RTS;

codec_read:
    [--SP] = RETS;
    CALL codec_map;
    CC = BITTST(R3, 4);
    IF CC JUMP 1f;
    JUMP.L codec_failed;
1:
    R2 = 0xff;
    W[P4 + 0x20] = R2;
    R2 = 0x45; /* DCNT1, MDIR, MEN */
    W[P4 + 0x14] = R2;
    CALL twi_wait;
    CC = BITTST(R3, 4);
    IF CC JUMP 1f;
    JUMP.L codec_failed;
1:
    R0 = W[P4 + 0x88] (Z);
    RETS = [SP++];
    RTS;

twi_wait:
    SET32 R2, 200000 /* finite polling budget per transfer */
twi_poll:
    R3 = W[P4 + 0x20] (Z);
    CC = BITTST(R3, 5);
    IF CC JUMP twi_return;
    CC = BITTST(R3, 4);
    IF CC JUMP twi_return;
    R2 += -1;
    CC = R2 == 0;
    IF !CC JUMP twi_poll;
twi_return:
    [P5 + 124] = R3;
    RTS;

codec_failed:
    R0 = 0;
    W[P4 + 0x14] = R0;
    CALL snapshot;
    [P5 + 100] = R5;
    SET32 R0, 0x43464149 /* CFAI: codec unavailable/readback failed */
    [P5] = R0;
    JUMP finish;

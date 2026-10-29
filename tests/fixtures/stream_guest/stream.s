/* Public BF524 finite-restart streaming fixture. No firmware or Doom assets.
 * Two 64-frame stereo buffers; all production and ownership are guest code. */
.section .text
.global _start, supervisor, dma_isr, timer_isr, exception_isr
.global idle_dma_resume, delay_idle_resume, idle_drain_resume
.global codec_setup, codec_configured, codec_failed
.global produce, fill_words, start_dma, event
.ifndef STREAM_CASE
.set STREAM_CASE, 0
.endif
.macro SET32 reg, value
    \reg\().L = ((\value) & 0xffff);
    \reg\().H = (((\value) >> 16) & 0xffff);
.endm
.macro ADDR reg, symbol
    \reg\().L = \symbol;
    \reg\().H = \symbol;
.endm
.macro REQUIRE reg, value
    CC = \reg == \value;
    IF CC JUMP 1f;
    JUMP.L failed;
1:
.endm

.include "codec-macros.inc"

_start:
    CLI R0;
    SET32 SP, 0xffb01000
    SET32 P0, 0xffe0203c
    ADDR R0, supervisor
    [P0] = R0;
    SET32 P0, 0xffe02024
    ADDR R0, dma_isr
    [P0] = R0;
    SET32 P0, 0xffe02018
    ADDR R0, timer_isr
    [P0] = R0;
    SET32 P0, 0xffe0200c
    ADDR R0, exception_isr
    [P0] = R0;
    R0 = 0x8240 (Z);
    STI R0;
    RAISE 15;
    ADDR R0, supervisor
    RETI = R0;
    RTI;

supervisor:
    [--SP] = RETI;
    SET32 P5, 0x2000
    P1 = P5;
    R0 = 0;
    R1 = 64;
clear:
    [P1++] = R0;
    R1 += -1;
    CC = R1 == 0;
    IF !CC JUMP clear;
    R0 = STREAM_CASE;
    [P5 + 160] = R0;
    SET32 R5, 0x76543210
    SET32 P0, 0xffc0010c
    SET32 R0, 0x20000
    [P0] = R0;
    SET32 P0, 0xffc0014c
    R0 = 0;
    [P0] = R0;
    SET32 P0, 0xffc00118
    R0 = 0x20;
    [P0] = R0;
    SET32 P0, 0xffc00124
    SET32 R0, 0x20000
    [P0] = R0;
    SET32 P0, 0xffc00164
    R0 = 0;
    [P0] = R0;
    CALL codec_setup;
    R0 = 0;
    CALL produce;
    R0 = 1;
    CALL produce;
    SET32 P0, 0xffc00800
    R0 = 0;
    W[P0] = R0;
    W[P0 + 0x38] = R0;
    W[P0 + 0x3c] = R0;
    R0 = 0x021f;
    W[P0 + 4] = R0;
    SET32 P0, 0xffc00d2c
    R0 = 0x4000;
    W[P0] = R0;
    R0 = [P5 + 160];
    CC = R0 == 2;
    IF !CC JUMP clock_prepared;
    CW 7, 0x10 /* Premature-reuse test: freeze the source prefetch boundary. */
clock_prepared:
    R1 = 0;
    CALL start_dma;
    SET32 P0, 0xffc00800
    R0 = 0x4401;
    W[P0] = R0;
    SSYNC;
    JUMP tx_started;
tx_started:
    R0 = CYCLES;
    [P5 + 28] = R0;
    R0 = [P5 + 160];
    CC = R0 == 2;
    IF !CC JUMP wait_dma;
    SET32 P0, 0xffc00d30
    R1 = W[P0] (Z);
    R0 = 128;
    R0 = R0 - R1;
    [P5 + 216] = R0;
    R1 = 8;
    CC = R0 == R1;
    IF CC JUMP 1f;
    JUMP.L failed;
1:
    SET32 P1, 0x3000
    R0 = 0x5000; /* Deliberate wrong-generation tag, while owner is IN_DMA. */
    CALL fill_words;
    SSYNC;
    R0 = 8;
    R1 = 0;
    CALL event;
    CW 7, 0

wait_dma:
    R0 = [P5 + 220];
    CC = R0 == 0;
    IF CC JUMP 1f;
    JUMP.L watchdog;
1:
    R0 = [P5 + 164];
    R1 = 256;
    CC = R0 == R1;
    IF !CC JUMP 1f;
    JUMP.L shutdown;
1:
    R1 = [P5 + 8];
    CC = R0 < R1 (IU);
    IF CC JUMP completed;
    R0 = [P5 + 24];
    R0 += 1;
    [P5 + 24] = R0;
    IDLE;
idle_dma_resume:
    JUMP wait_dma;

completed:
    [P5 + 248] = R0; /* completed block, preserved across helper calls */
    R1 = R0;
    CALL owner_pointer;
    R1 = [P2];
    REQUIRE R1, 3
    R1 = 0;
    [P2] = R1; /* Release only after the guest completion ISR. */
    R1 = [P5 + 248];
    R0 = 5;
    CALL event;
    R0 = [P5 + 164];
    R0 += 1;
    [P5 + 164] = R0;
    R1 = 256;
    CC = R0 == R1;
    IF CC JUMP refill;
    R1 = R0;
    CALL start_dma; /* Publish consumer's next ready slot before producing. */
refill:
    R0 = [P5 + 248];
    R0 += 2;
    R1 = 256;
    CC = R0 < R1 (IU);
    IF !CC JUMP wait_dma;
    R1 = [P5 + 160];
    CC = R1 == 1;
    IF !CC JUMP produce_next;
    R1 = [P5 + 248];
    R2 = 8;
    CC = R1 == R2;
    IF !CC JUMP produce_next;
    CALL producer_delay; /* DMA9 finishes during this 2 ms guest IDLE wait. */
produce_next:
    R0 = [P5 + 248];
    R0 += 2;
    CALL produce;
    JUMP wait_dma;

/* R1=block -> P2=owner. Two slots, FREE/FILLING/READY/IN_DMA=0/1/2/3. */
owner_pointer:
    R2 = 1;
    R2 = R1 & R2;
    R2 <<= 2;
    P2 = R2;
    SET32 P0, 0x20b0
    P2 = P2 + P0;
    RTS;

/* Producer interface: R0=block generation, fills its released slot, then
 * publishes READY. A future mixer supplies 64 interleaved signed16 frames
 * before this consumer-visible publication, using the same packing. */
produce:
    [--SP] = RETS;
    [--SP] = R5;
    R5 = R0;
    R1 = R0;
    CALL owner_pointer;
    R1 = [P2];
    REQUIRE R1, 0
    R0 = 1;
    [P2] = R0;
    R1 = R5;
    CALL event;
    R0 = R5;
    R1 = 1;
    R0 = R0 & R1;
    R0 <<= 9;
    SET32 R1, 0x3000
    R0 = R0 + R1;
    P1 = R0;
    R0 = R5;
    R0 += 1;
    R0 <<= 7;
    CALL fill_words;
    SSYNC;
    R0 = 2;
    [P2] = R0;
    [P2 + 8] = R5;
    R0 = [P5 + 172];
    R0 += 1;
    [P5 + 172] = R0;
    R0 = [P2 + 16];
    R0 += 1;
    [P2 + 16] = R0; /* total productions per slot, including initial fill */
    R1 = R5;
    R0 = 2;
    CALL event;
    R5 = [SP++];
    RETS = [SP++];
    RTS;

fill_words: /* R0=signed16 bit tag (frame0), P1=destination. */
    R3 = 64;
fill_frame:
    R1 = R0 << 16; /* left: tag|frame */
    [P1++] = R1;
    R2 = 0x40;
    R1 = R0 | R2;
    R1 <<= 16; /* right: tag|0x40|frame */
    [P1++] = R1;
    R0 += 1;
    R3 += -1;
    CC = R3 == 0;
    IF !CC JUMP fill_frame;
    RTS;

start_dma: /* R1=block to submit, requires READY and generation match. */
    [--SP] = RETS;
    [--SP] = R5;
    R5 = R1;
    CALL owner_pointer;
    R0 = [P2];
    REQUIRE R0, 2
    R0 = [P2 + 8];
    CC = R0 == R5;
    IF CC JUMP 1f;
    JUMP.L failed;
1:
    R0 = 3;
    [P2] = R0;
    SET32 P0, 0xffc00d00
    /* FLOW=STOP permits restart by CONFIG with DMAEN still1, once RUN0.
     * Keeping DMA ownership suppresses the shared SPORT PIO service IRQ
     * even if the producer was late and the peripheral FIFO is empty. */
    R0 = W[P0 + 0x28] (Z);
    REQUIRE R0, 0
    R0 = R5;
    R2 = 1;
    R0 = R0 & R2;
    R0 <<= 9;
    SET32 R2, 0x3000
    R0 = R0 + R2;
    [P0 + 4] = R0;
    R0 = 128;
    W[P0 + 0x10] = R0;
    R0 = 4;
    W[P0 + 0x14] = R0;
    R0 = 0x00a9; /* 32-bit, SYNC1, DI_EN, FLOW=STOP. */
    W[P0 + 8] = R0;
    SSYNC;
    R0 = [P5 + 168];
    R0 += 1;
    [P5 + 168] = R0;
    R0 = 3;
    R1 = R5;
    CALL event;
    R0 = 1; /* timer purpose: work-unit watchdog */
    SET32 R1, 1600000 /* 4 ms, comfortably beyond 64 frames at 48 kHz */
    CALL arm_timer;
    R5 = [SP++];
    RETS = [SP++];
    RTS;

arm_timer: /* purpose R0, duration R1; no calls */
    [P5 + 224] = R0;
    SET32 P0, 0xffe03000
    R0 = 0;
    [P0] = R0;
    [P0 + 8] = R0;
    [P5 + 228] = R0;
    [P0 + 4] = R1;
    R0 = 3;
    [P0] = R0;
    RTS;

producer_delay:
    [--SP] = RETS;
    R0 = 6;
    R1 = 10;
    CALL event;
    R0 = 2;
    SET32 R1, 800000
    CALL arm_timer;
delay_wait:
    R0 = [P5 + 228];
    CC = R0 == 0;
    IF !CC JUMP delayed;
    IDLE;
delay_idle_resume:
    JUMP delay_wait;
delayed:
    R0 = 7;
    R1 = 10;
    CALL event;
    R0 = 1;
    [P5 + 212] = R0;
    RETS = [SP++];
    RTS;

shutdown:
    R0 = 10;
    R1 = 256;
    CALL event;
    R0 = CYCLES;
    [P5 + 40] = R0;
    R0 = 3;
    SET32 R1, 26000 /* 6 outstanding SPORT words *32/3.072MHz=62.5us; 65us guard */
    CALL arm_timer;
drain_wait:
    R0 = [P5 + 228];
    CC = R0 == 0;
    IF !CC JUMP drained;
    IDLE;
idle_drain_resume:
    JUMP drain_wait;
drained:
    CALL snapshot;
    SET32 P0, 0xffc00800
    R0 = 0;
    W[P0] = R0;
    SET32 P0, 0xffc00d08
    W[P0] = R0;
    SSYNC;
    R0 = 11;
    R1 = 256;
    CALL event;
    R0 = [P5 + 8];
    R1 = 256;
    CC = R0 == R1;
    IF !CC JUMP failed;
    R0 = [P5 + 204];
    REQUIRE R0, 0
    SET32 R0, 0x50415353
    JUMP finish;
watchdog:
    CALL snapshot;
    SET32 R0, 0x57444f47
    JUMP finish;
failed:
    CALL snapshot;
    SET32 R0, 0x4641494c
    JUMP finish;
codec_failed:
    SET32 R0, 0x43464149
finish:
    [P5] = R0;
    [P5 + 100] = R5;
    R0 = CYCLES;
    [P5 + 48] = R0;
    SET32 P0, 0x08000000
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
    RTS;

/* Bounded guest event ledger at10000: 2048 records of16 u32s. */
event:
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = R2;
    [--SP] = R3;
    [--SP] = P0;
    [--SP] = P1;
    R2 = [P5 + 200];
    R3 = 2048;
    CC = R2 < R3 (IU);
    IF !CC JUMP event_drop;
    R3 = R2 << 6;
    P1 = R3;
    SET32 P0, 0x10000
    P1 = P1 + P0;
    [P1] = R0;
    [P1 + 4] = R1;
    R3 = CYCLES;
    [P1 + 8] = R3;
    SET32 P0, 0xffc00d00
    R3 = [P0 + 0x24];
    [P1 + 12] = R3;
    R3 = W[P0 + 0x30] (Z);
    [P1 + 16] = R3;
    R3 = W[P0 + 0x28] (Z);
    [P1 + 20] = R3;
    SET32 P0, 0xffc00830
    R3 = W[P0] (Z);
    [P1 + 24] = R3;
    R0 = [P5 + 180];
    R0 <<= 8;
    R3 = [P5 + 176];
    R3 = R3 | R0;
    [P1 + 28] = R3;
    R3 = [P5 + 76];
    [P1 + 32] = R3;
    SET32 P0, 0xffe02108
    R3 = [P0];
    [P1 + 36] = R3;
    R3 = [P5 + 168];
    [P1 + 40] = R3;
    R3 = [P5 + 172];
    [P1 + 44] = R3;
    R3 = [P5 + 184];
    [P1 + 48] = R3;
    R3 = [P5 + 188];
    [P1 + 52] = R3;
    R3 = [P5 + 64];
    [P1 + 56] = R3;
    R3 = [P5 + 60];
    [P1 + 60] = R3;
    R2 += 1;
    [P5 + 200] = R2;
    JUMP event_return;
event_drop:
    R2 = [P5 + 204];
    R2 += 1;
    [P5 + 204] = R2;
event_return:
    P1 = [SP++];
    P0 = [SP++];
    R3 = [SP++];
    R2 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    RTS;

.align 4
dma_isr:
    [--SP] = ASTAT;
    [--SP] = RETS;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = R2;
    [--SP] = P0;
    [--SP] = P1;
    R0 = CYCLES;
    [P5 + 32] = R0;
    R0 = RETI;
    [P5 + 76] = R0;
    R1 = [P5 + 8];
    R0 = R1;
    R0 += 1;
    [P5 + 8] = R0;
    SET32 P0, 0xffc00d28
    R0 = W[P0] (Z);
    [P5 + 60] = R0;
    R0 = 1;
    W[P0] = R0;
    SSYNC;
    R0 = W[P0] (Z);
    [P5 + 64] = R0;
    R0 = CYCLES;
    [P5 + 36] = R0;
    R0 = 4;
    CALL event;
    P1 = [SP++];
    P0 = [SP++];
    R2 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    RETS = [SP++];
    ASTAT = [SP++];
    RTI;

.align 4
timer_isr:
    [--SP] = ASTAT;
    [--SP] = RETS;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = P0;
    SET32 P0, 0xffe03000
    R0 = 0;
    [P0] = R0;
    R0 = [P5 + 12];
    R0 += 1;
    [P5 + 12] = R0;
    R0 = 1;
    [P5 + 228] = R0;
    R0 = [P5 + 224];
    CC = R0 == 1;
    IF !CC JUMP 1f;
    [P5 + 220] = R0;
1:
    R0 = 9;
    R1 = [P5 + 164];
    CALL event;
    P0 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    RETS = [SP++];
    ASTAT = [SP++];
    RTI;

exception_isr:
    R0 = SEQSTAT;
    [P5 + 104] = R0;
    R0 = RETX;
    [P5 + 108] = R0;
    SET32 R0, 0x45584350
    JUMP finish;

.include "codec.inc"

/* Public synthetic ROM memory-boot caller, INIT and callback. No vendor source.
 * CASE: 0 normal, 1 callback context corruption, 2 missing SDRAM configuration,
 * 3 ignored source redirection. BOOT_DATA: 0 ROM, 1 null, 2 misaligned. */
.equ mailbox, 0xff800000
.equ context, 0xff800800
.equ stream, 0xff900000
.equ sentinel, 0x00100800
.macro SET reg, value
    \reg\().L = ((\value) & 0xffff);
    \reg\().H = (((\value) >> 16) & 0xffff);
.endm
.macro ADDR reg, symbol
    \reg\().L = \symbol;
    \reg\().H = \symbol;
.endm

.section .bootstrap,"ax"
.global _start, boot_call, boot_returned, exception_stop
_start:
    SET SP, 0xffb00ffc
    FP = SP;
    USP = SP;
    CLI R0;
    SET P0, 0xffe0200c
    ADDR R0, exception_stop
    [P0] = R0;
.if BOOT_DATA == 0
    ADDR R0, stream
    R1 = 0;
    R2 = 0;
    R3 = 0;
    SET P0, 0xef000008 /* documented BFROM_MEMBOOT public entry */
.else
.if BOOT_DATA == 1
    R0 = 0;
.else
 .if BOOT_DATA == 2
    SET R0, 0xffb00001
 .else
    SET R0, 0xffb00100 /* aligned but zero-filled malformed structure */
 .endif
.endif
    ADDR P0, init_entry
.endif
boot_call:
    CALL (P0);
boot_returned:
    ADDR P0, mailbox
    SET R0, 0xe005 /* unexpected normal return */
    [P0] = R0;
    JUMP.S boot_returned;
exception_stop:
    ADDR P0, mailbox
    SET R0, 0xe006
    [P0] = R0;
exception_wait:
    IDLE;
    JUMP.S exception_wait;

.section .init,"ax"
.global init_entry, init_valid, init_return, callback_entry, callback_valid, callback_return, poison_entry
init_entry:
    LINK 0;
    [--SP] = (R7:4, P5:3);
    R4 = R0;
    ADDR P5, mailbox
    SET R1, 0xffb00000
    CC = R0 < R1 (IU);
    IF CC JUMP init_bad_pointer;
    SET R1, 0xffb00fa0
    CC = R0 <= R1 (IU);
    IF !CC JUMP init_bad_pointer;
    R1 = 3;
    R1 = R0 & R1;
    CC = R1 == 0;
    IF !CC JUMP init_bad_pointer;
    P4 = R4;
    R0 = [P4];
    ADDR R1, stream
    CC = R0 < R1 (IU);
    IF CC JUMP init_bad_pointer;
    SET R1, 0xff904000
    CC = R0 < R1 (IU);
    IF !CC JUMP init_bad_pointer;
init_valid:
    [P5 + 0x10] = R4;
    R0 = SP;
    [P5 + 0x14] = R0;
    R0 = SYSCFG;
    [P5 + 0x18] = R0;
    ADDR P3, context
    SET R0, 0x434f4e54
.if CASE == 1
    R0 = 0;
.endif
    [P3] = R0;
    [P3 + 4] = R4;
    R0 = 1;
    [P3 + 0xc] = R0;
    R0 = 0;
    [P3 + 8] = R0;
    R0 = P3;
    [P4 + 0x28] = R0; /* documented user-long field: our own context */
    ADDR R0, callback_entry
    [P4 + 0x38] = R0;
    R0 = [P4];
    [P5 + 0x1c] = R0;
.if CASE != 3
    R0 += 16; /* skip exactly one zero-payload poison header */
.endif
    [P4] = R0;
    [P5 + 0x20] = R0;
.if CASE != 2
    SET P0, 0xffc00a18
    R0 = 0x300;
    W[P0] = R0.L;
    SET P0, 0xffc00a14
    R0 = 1; /* enabled 16-MiB synthetic bank; no electrical timing claim */
    W[P0] = R0.L;
    SET P0, 0xffc00a10
    SET R0, 0xe0088849 /* existing public model/reset control value */
    [P0] = R0;
    SSYNC;
    ADDR P0, sentinel
    SET R0, 0x13579bdf
    [P0] = R0;
.endif
    SET P0, 0xffc00124
    R0 = 1;
    [P0] = R0;
    SET P0, 0xffc00164
    R0 = 0;
    [P0] = R0;
    R0 = 0x32;
    SYSCFG = R0;
init_return:
    (R7:4, P5:3) = [SP++];
    UNLINK;
    RTS;
init_bad_pointer:
    SET R0, 0xe001
    [P5] = R0;
    JUMP.S init_return;

callback_entry:
    LINK 0;
    [--SP] = (R7:4, P5:3);
    P4 = R0;
    P3 = R1;
    R4 = R2;
    ADDR P5, mailbox
    R1 = [P5 + 0x10];
    CC = R0 == R1;
    IF !CC JUMP callback_bad_context;
    R0 = [P4 + 0x28];
    ADDR R1, context
    CC = R0 == R1;
    IF !CC JUMP callback_bad_context;
    P4 = R0;
    R0 = [P4];
    SET R1, 0x434f4e54
    CC = R0 == R1;
    IF !CC JUMP callback_bad_context;
    R0 = P3;
    SET R1, 0xffb00000
    CC = R0 < R1 (IU);
    IF CC JUMP callback_bad_context;
    SET R1, 0xffb00ff0
    CC = R0 <= R1 (IU);
    IF !CC JUMP callback_bad_context;
    R1 = 3;
    R1 = R0 & R1;
    CC = R1 == 0;
    IF !CC JUMP callback_bad_context;
    R1 = 13;
    CC = R4 == R1; /* ADI callback: single-block destination buffer */
    IF !CC JUMP callback_bad_context;
    R0 = [P3];
    ADDR R1, main_entry
    CC = R0 == R1;
    IF !CC JUMP callback_bad_context;
    SET P0, 0xffc00a14
    R0 = W[P0] (Z);
    CC = R0 == 1;
    IF !CC JUMP callback_bad_init;
    ADDR P0, sentinel
    R0 = [P0];
    SET R1, 0x13579bdf
    CC = R0 == R1;
    IF !CC JUMP callback_bad_init;
callback_valid:
    [P5 + 0x24] = R4;
    R0 = [P4 + 8];
    R0 += 1;
    [P4 + 8] = R0;
    ADDR P0, main_word
    SET R0, 0x55aa11ee
    [P0] = R0;
callback_return:
    R0 = 0;
    (R7:4, P5:3) = [SP++];
    UNLINK;
    RTS;
callback_bad_context:
    SET R0, 0xe002
    [P5] = R0;
    JUMP.S callback_return;
callback_bad_init:
    SET R0, 0xe003
    [P5] = R0;
    JUMP.S callback_return;
poison_entry:
    ADDR P0, mailbox
    SET R0, 0xe004
    [P0] = R0;
poison_wait:
    JUMP.S poison_wait;
.align 4

.section .main,"ax"
.global main_entry, main_done, main_word
main_entry:
    ADDR P5, mailbox
    ADDR P0, main_word
    R0 = [P0];
    [P5 + 4] = R0;
    R0 = SYSCFG;
    [P5 + 8] = R0;
    R0 = SP;
    [P5 + 0xc] = R0;
    ADDR R0, main_entry
    [P5 + 0x28] = R0;
    SET R0, 0x48414e44
    [P5] = R0;
main_done:
    IDLE;
    JUMP.S main_done;
.align 4
main_word:
    .long 0xa1b2c3d4

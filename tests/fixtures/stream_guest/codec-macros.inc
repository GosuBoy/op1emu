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

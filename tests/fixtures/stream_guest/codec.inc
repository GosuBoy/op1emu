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

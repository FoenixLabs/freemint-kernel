/* A2560Me bus and factory MAC. Copyright 2026 FoenixLabs contributors.
 * GPL-2.0-or-later; see FreeMiNT COPYING.
 * Call only in supervisor mode on an A2560Me.
 */
#ifndef LAN9221_BOARD_H
#define LAN9221_BOARD_H
#define LAN_BASE 0xfec00600UL
#define I2C_BASE 0xfec00e00UL
#define B8(o) (*(volatile unsigned char *)(I2C_BASE + (o)))
#define B16(o) (*(volatile unsigned short *)(I2C_BASE + (o)))
#define B32(o) (*(volatile unsigned long *)(I2C_BASE + (o)))
static lan_u32 board_swap(lan_u32 v)
{
    return ((v & 255) << 24) | ((v & 0xff00UL) << 8) |
           ((v >> 8) & 0xff00UL) | (v >> 24);
}
static lan_u32 board_read(unsigned int off)
{
    return board_swap(*(volatile unsigned long *)(LAN_BASE + off));
}
static void board_write(unsigned int off, lan_u32 v)
{
    *(volatile unsigned long *)(LAN_BASE + off) = board_swap(v);
}
static int board_present(void)
{
    const unsigned long *jar = *(const unsigned long *volatile *)0x5a0;
    unsigned int i;
    if (!jar) return 0;
    for (i = 0; i < 256 && jar[0]; i++, jar += 2)
        if (jar[0] == 0x4656524dUL && jar[1] == 0x3f800000UL) return 1;
    return 0;
}
/* Shared controller: leave clock, enable, and HDMI priority settings intact. */
static int board_factory_mac(struct lan_device *d)
{
    unsigned int attempt, n, error;
    if (B32(0x0c) != 0x49324d01UL) return -1;
    for (attempt = 0; attempt < 3; attempt++) {
        for (n = 0; n < 2000 && (B8(0x11) & 1); n++) d->io.delay(100);
        if (n == 2000) return -1;
        B8(0x11) = 2;
        B8(7) = 0x50;
        B16(0x14) = 0xfa;
        B16(0x12) = 6;
        B8(0x10) = 7;
        for (n = 0; n < 2000 && !(B8(0x11) & 2); n++) d->io.delay(100);
        if (n == 2000) return -1;
        error = B8(0x11) >> 4;
        if (error == 3) { d->io.delay(1000); continue; }
        if (error || B16(0x1c) != 6 || B16(0x1a) < 6) return -1;
        for (n = 0; n < 6; n++) d->mac[n] = B8(0x18);
        return lan_mac_valid(d->mac) ? 0 : -1;
    }
    return -1;
}
static int board_mac(struct lan_device *d)
{
    lan_u32 lo, hi;
    unsigned int i;
    if (!board_factory_mac(d)) return 0;
    if (lan_mac_read(d, L_ADDRL, &lo) || lan_mac_read(d, L_ADDRH, &hi)) return -1;
    for (i = 0; i < 4; i++) d->mac[i] = lo >> (i * 8);
    d->mac[4] = hi; d->mac[5] = hi >> 8;
    return lan_mac_valid(d->mac) ? 0 : -1;
}
#endif

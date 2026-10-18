/* A2560Me LAN9221 hardware core. Copyright 2026 FoenixLabs contributors.
 * GPL-2.0-or-later; see FreeMiNT COPYING.
 */
#include "hw.h"

static int wait_clear(struct lan_device *d, unsigned int reg, lan_u32 bits,
                      unsigned int attempts, unsigned long delay)
{
    while (attempts--) {
        if (!(d->io.read(reg) & bits)) return 0;
        d->io.delay(delay);
    }
    return -1;
}
int lan_mac_valid(const unsigned char *p)
{
    unsigned int i, any = 0, all = 255;
    for (i = 0; i < 6; i++) { any |= p[i]; all &= p[i]; }
    return any && all != 255 && !(p[0] & 1);
}
int lan_mac_read(struct lan_device *d, unsigned int reg, lan_u32 *v)
{
    if (wait_clear(d, L_MAC_CMD, L_BUSY, 1000, 1)) return -1;
    d->io.write(L_MAC_CMD, L_BUSY | L_MAC_READ | reg);
    if (wait_clear(d, L_MAC_CMD, L_BUSY, 1000, 1)) return -1;
    *v = d->io.read(L_MAC_DATA);
    return 0;
}
int lan_mac_write(struct lan_device *d, unsigned int reg, lan_u32 v)
{
    if (wait_clear(d, L_MAC_CMD, L_BUSY, 1000, 1)) return -1;
    d->io.write(L_MAC_DATA, v);
    d->io.write(L_MAC_CMD, L_BUSY | reg);
    return wait_clear(d, L_MAC_CMD, L_BUSY, 1000, 1);
}
static int phy_ready(struct lan_device *d)
{
    unsigned int n;
    lan_u32 value;
    for (n = 0; n < 1000; n++) {
        if (lan_mac_read(d, L_MII_ACC, &value)) return -1;
        if (!(value & 1)) return 0;
        d->io.delay(1);
    }
    return -1;
}
int lan_phy_read(struct lan_device *d, unsigned int reg, lan_u32 *value)
{
    if (phy_ready(d) || lan_mac_write(d, L_MII_ACC, 0x800UL | ((lan_u32)reg << 6) | 1) ||
        phy_ready(d)) return -1;
    return lan_mac_read(d, L_MII_DATA, value);
}
int lan_phy_write(struct lan_device *d, unsigned int reg, lan_u32 value)
{
    if (phy_ready(d) || lan_mac_write(d, L_MII_DATA, value) ||
        lan_mac_write(d, L_MII_ACC, 0x800UL | ((lan_u32)reg << 6) | 3)) return -1;
    return phy_ready(d);
}
int lan_probe(struct lan_device *d)
{
    return d->io.read(L_BYTE_TEST) == 0x87654321UL &&
           (d->io.read(L_ID_REV) >> 16) == 0x9221 ? 0 : -1;
}
int lan_update_link(struct lan_device *d)
{
    lan_u32 status, mode, cr;
    if (lan_phy_read(d, 1, &status) || lan_phy_read(d, 1, &status)) return -1;
    d->link = (status & 4) != 0;
    if (!d->link) return 0;
    if (lan_phy_read(d, 31, &mode)) return -1;
    d->full = (mode & 0x10) != 0;
    cr = d->mac_cr;
    if (d->full) cr |= L_MAC_FULL;
    else cr &= ~L_MAC_FULL;
    if (cr != d->mac_cr) {
        if (lan_mac_write(d, L_MAC_CR, cr)) return -1;
        d->mac_cr = cr;
    }
    return 0;
}
void lan_stop(struct lan_device *d)
{
    d->io.write(L_INT_EN, 0);
    d->io.write(L_IRQ_CFG, 0);
    lan_mac_write(d, L_MAC_CR, 0);
    d->io.write(L_TX_CFG, 0);
    d->link = 0;
}
int lan_start(struct lan_device *d)
{
    lan_u32 v;
    unsigned int n;
    if (lan_probe(d) || !lan_mac_valid(d->mac)) return -1;
    /* No EEPROM exists on the LAN9221 itself. Reset may clear its MAC. */
    d->io.write(L_HW_CFG, d->io.read(L_HW_CFG) | 1);
    if (wait_clear(d, L_HW_CFG, 1, 1000, 100)) return -1;
    if (d->io.read(L_HW_CFG) & 2) return -1;
    for (n = 0; n < 1000 && !(d->io.read(L_PMT_CTRL) & 1); n++) d->io.delay(100);
    if (n == 1000) return -1;
    d->io.write(L_INT_EN, 0);
    d->io.write(L_IRQ_CFG, 0);
    d->io.write(L_INT_STS, 0xffffffffUL);
    /* FIFO little-endian data ports, 4-byte RX end alignment, no data offset. */
    d->io.write(L_HW_CFG, 0x00150000UL);
    d->io.write(L_RX_CFG, 0);
    d->io.write(L_AFC_CFG, 0);
    d->io.write(L_GPIO_CFG, 0x70000000UL);
    if (lan_mac_write(d, L_ADDRL, (lan_u32)d->mac[0] | ((lan_u32)d->mac[1] << 8) |
                      ((lan_u32)d->mac[2] << 16) | ((lan_u32)d->mac[3] << 24)) ||
        lan_mac_write(d, L_ADDRH, (lan_u32)d->mac[4] | ((lan_u32)d->mac[5] << 8)) ||
        lan_mac_write(d, L_FLOW, 0)) goto fail;
    if (lan_phy_write(d, 0, 0x8000)) goto fail;
    for (n = 0; n < 500; n++) {
        if (lan_phy_read(d, 0, &v)) goto fail;
        if (!(v & 0x8000)) break;
        d->io.delay(1000);
    }
    if (n == 500 || lan_phy_write(d, 4, 0x01e1) || lan_phy_write(d, 0, 0x1200)) goto fail;
    d->mac_cr = L_MAC_TXRX | L_MAC_MULTICAST;
    if (lan_mac_write(d, L_MAC_CR, d->mac_cr)) goto fail;
    d->io.write(L_TX_CFG, 2);
    d->link = d->full = 0;
    d->tag = 0;
    return 0;
fail:
    lan_stop(d);
    return -1;
}
/* Returns 0 when queued in hardware, 1 when busy/link down, -1 if invalid. */
int lan_send(struct lan_device *d, const unsigned char *p, unsigned int n)
{
    unsigned int i, j;
    lan_u32 word;
    if (n < 14 || n > LAN_MAX_FRAME - 4) return -1;
    if (!d->link || (d->io.read(L_TX_INF) & 0xffff) < ((n + 3U) & ~3U) + 8U) return 1;
    d->io.write(L_TX_DATA, 0x3000UL | n);
    d->io.write(L_TX_DATA, ((lan_u32)d->tag++ << 16) | n);
    for (i = 0; i < n; i += 4) {
        word = 0;
        for (j = 0; j < 4 && i + j < n; j++) word |= (lan_u32)p[i + j] << (j * 8);
        d->io.write(L_TX_DATA, word);
    }
    return 0;
}
/* 1 packet, 0 empty, -1 discarded bad packet, -2 hardware failed. FCS stripped. */
int lan_receive(struct lan_device *d, unsigned char *p, unsigned int *length)
{
    lan_u32 status, word;
    unsigned int i, j, n;
    if (!(d->io.read(L_RX_INF) & 0x00ff0000UL)) return 0;
    status = d->io.read(L_RX_STATUS);
    n = (status >> 16) & 0x3fff;
    if (n < 18 || n > LAN_MAX_FRAME) {
        d->io.write(L_RX_DP, L_BUSY);
        return wait_clear(d, L_RX_DP, L_BUSY, 1000, 1) ? -2 : -1;
    }
    for (i = 0; i < n; i += 4) {
        word = d->io.read(L_RX_DATA);
        for (j = 0; j < 4 && i + j < n; j++) p[i + j] = word >> (8 * j);
    }
    if (status & 0x8000) return -1;
    *length = n - 4;
    return 1;
}

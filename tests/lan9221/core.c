/* Exercise the actual driver hardware core with emulated registers/FIFOs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hw.h"
static lan_u32 regs[64], mac[256], phy[32], tx[512], fifo[512];
static unsigned int nt, nr, readpos, waits;
static int stuck;
static lan_u32 rd(unsigned int off)
{
    if (off == L_MAC_CMD && stuck) return L_BUSY;
    if (off == L_RX_DATA) { assert(readpos < nr); return fifo[readpos++]; }
    return regs[off/4];
}
static void wr(unsigned int off, lan_u32 v)
{
    unsigned int index;
    regs[off/4] = v;
    if (off == L_TX_DATA) { assert(nt < 512); tx[nt++] = v; }
    if (off == L_HW_CFG) regs[off/4] &= ~1U;
    if (off == L_RX_DP && !stuck) regs[off/4] = 0;
    if (off == L_MAC_CMD) {
        index = v & 255;
        if (v & L_MAC_READ) regs[L_MAC_DATA/4] = mac[index];
        else {
            mac[index] = regs[L_MAC_DATA/4];
            if (index == L_MII_ACC) {
                unsigned int reg = (mac[index] >> 6) & 31;
                if (mac[index] & 2) phy[reg] = mac[L_MII_DATA] & ~0x8000U;
                else mac[L_MII_DATA] = phy[reg];
                mac[index] &= ~1U;
            }
        }
        regs[off/4] = 0;
    }
}
static void delay(unsigned long us) { (void)us; waits++; }
int main(void)
{
    struct lan_device d = {{rd,wr,delay},{2,1,2,3,4,5},0,0,0,0};
    unsigned char bytes[LAN_MAX_FRAME], input[65];
    unsigned int n, len;
    lan_u32 value;
    regs[L_BYTE_TEST/4] = 0x87654321U; regs[L_ID_REV/4] = 0x92210000U;
    regs[L_PMT_CTRL/4] = 1;
    assert(!lan_probe(&d));
    regs[L_BYTE_TEST/4] ^= 1; assert(lan_probe(&d)); regs[L_BYTE_TEST/4] ^= 1;
    assert(lan_mac_valid(d.mac)); d.mac[0] = 1; assert(!lan_mac_valid(d.mac)); d.mac[0] = 2;
    assert(!lan_start(&d));
    assert(mac[L_ADDRL] == 0x03020102U && mac[L_ADDRH] == 0x0504U);
    phy[1] = 4; phy[31] = 0x10; assert(!lan_update_link(&d));
    assert(d.link && d.full && (mac[L_MAC_CR] & L_MAC_FULL));
    regs[L_TX_INF/4] = 8192;
    for (n = 0; n < sizeof(input); n++) input[n] = n;
    assert(!lan_send(&d,input,sizeof(input))); assert(nt == 19);
    assert(tx[0] == (0x3000U | 65) && tx[1] == 65);
    assert(tx[2] == 0x03020100U && tx[18] == 64); /* no trailing overread */
    regs[L_TX_INF/4] = 4; assert(lan_send(&d,input,64) == 1);
    assert(lan_send(&d,input,13) == -1);
    regs[L_RX_INF/4] = 0x10000; regs[L_RX_STATUS/4] = 65U << 16;
    nr = 17; readpos = 0;
    for (n = 0; n < nr; n++) fifo[n] = 0x03020100U + 0x04040404U*n;
    assert(lan_receive(&d,bytes,&len) == 1 && len == 61 && readpos == 17);
    for (n = 0; n < 61; n++) assert(bytes[n] == n);
    regs[L_RX_STATUS/4] |= 0x8000; readpos = 0;
    assert(lan_receive(&d,bytes,&len) == -1 && readpos == nr);
    regs[L_RX_STATUS/4] = 1600U << 16; assert(lan_receive(&d,bytes,&len) == -1);
    stuck = 1; waits = 0;
    assert(lan_mac_read(&d,1,&value) == -1 && waits == 1000);
    waits = 0; assert(lan_receive(&d,bytes,&len) == -2 && waits == 1000);
    stuck = 0; phy[1] = 0; assert(!lan_update_link(&d) && !d.link);
    lan_stop(&d); assert(!mac[L_MAC_CR] && !regs[L_INT_EN/4]);
    puts("LAN9221 core: probe, reset, MAC/PHY, duplex, FIFO bytes/padding, FCS/errors, timeouts PASS");
    return 0;
}

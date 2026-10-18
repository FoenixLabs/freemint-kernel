/* A2560Me LAN9221 hardware core. Copyright 2026 FoenixLabs contributors.
 * GPL-2.0-or-later; see FreeMiNT COPYING.
 * Register facts: Microchip DS00002416A; board reference rev 1.8 pp.37-38.
 */
#ifndef LAN9221_HW_H
#define LAN9221_HW_H
#ifdef LAN9221_HOST_TEST
#include <stdint.h>
typedef uint32_t lan_u32;
#else
typedef unsigned long lan_u32;
#endif
#define L_RX_DATA 0x00
#define L_TX_DATA 0x20
#define L_RX_STATUS 0x40
#define L_TX_STATUS 0x48
#define L_ID_REV 0x50
#define L_IRQ_CFG 0x54
#define L_INT_STS 0x58
#define L_INT_EN 0x5c
#define L_BYTE_TEST 0x64
#define L_RX_CFG 0x6c
#define L_TX_CFG 0x70
#define L_HW_CFG 0x74
#define L_RX_DP 0x78
#define L_RX_INF 0x7c
#define L_TX_INF 0x80
#define L_PMT_CTRL 0x84
#define L_GPIO_CFG 0x88
#define L_RX_DROP 0xa0
#define L_MAC_CMD 0xa4
#define L_MAC_DATA 0xa8
#define L_AFC_CFG 0xac
#define L_BUSY 0x80000000UL
#define L_MAC_READ 0x40000000UL
#define L_MAC_CR 1
#define L_ADDRH 2
#define L_ADDRL 3
#define L_MII_ACC 6
#define L_MII_DATA 7
#define L_FLOW 8
#define L_MAC_TXRX 0x0cUL
#define L_MAC_FULL 0x00100000UL
#define L_MAC_MULTICAST 0x00080000UL
#define L_MAC_PROMISC 0x00040000UL
#define LAN_MAX_FRAME 1536

struct lan_io {
    lan_u32 (*read)(unsigned int);
    void (*write)(unsigned int, lan_u32);
    void (*delay)(unsigned long);
};
struct lan_device {
    struct lan_io io;
    unsigned char mac[6];
    unsigned char link, full;
    lan_u32 mac_cr;
    unsigned short tag;
};
int lan_mac_valid(const unsigned char *);
int lan_mac_read(struct lan_device *, unsigned int, lan_u32 *);
int lan_mac_write(struct lan_device *, unsigned int, lan_u32);
int lan_phy_read(struct lan_device *, unsigned int, lan_u32 *);
int lan_phy_write(struct lan_device *, unsigned int, lan_u32);
int lan_probe(struct lan_device *);
int lan_start(struct lan_device *);
void lan_stop(struct lan_device *);
int lan_update_link(struct lan_device *);
int lan_send(struct lan_device *, const unsigned char *, unsigned int);
int lan_receive(struct lan_device *, unsigned char *, unsigned int *);
#endif

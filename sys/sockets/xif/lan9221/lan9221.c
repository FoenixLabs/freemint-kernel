/* A2560Me MiNTNet LAN9221 driver. Copyright 2026 FoenixLabs contributors.
 * GPL-2.0-or-later; see FreeMiNT COPYING.
 * Bounded programmed I/O, serviced by a root timeout. No board IRQ changes.
 */
#include "global.h"
#include "buf.h"
#include "inet4/if.h"
#include "inet4/ifeth.h"
#include "netinfo.h"
#include "mint/sockio.h"
#include "mint/delay.h"
#include "hw.h"
#include "board.h"

static struct netif iface;
static struct lan_device device;
static TIMEOUT *timer;
static BUF *pending;
static unsigned char rx[LAN_MAX_FRAME];
static unsigned int ticks;
static short running;
static void poll_device(PROC *, long);

static void delay_us(unsigned long us)
{
    unsigned long rate = *KERNEL->loops_per_sec;
    unsigned long loops;
    /* udelay() emits 32x32->64 MULU, absent on the 68EC060. Our waits
     * are at most 1000 us, so splitting the calibrated rate keeps every
     * intermediate within 32 bits and uses implemented instructions. */
    loops = (rate / 1000000UL) * us + ((rate % 1000000UL) * us) / 1000000UL;
    __delay(loops);
}
static void fault(void)
{
    running = 0;
    lan_stop(&device);
    iface.flags &= ~IFF_RUNNING;
    c_conws("LAN9221: stopped after hardware/timeout failure; down/up to retry\r\n");
}
static void poll_device(PROC *p, long arg)
{
    unsigned int budget, length;
    int r;
    lan_u32 status;
    BUF *b;
    short type;
    UNUSED(p); UNUSED(arg);
    timer = 0;
    if (!running) return;
    if (!(ticks++ % 50) && lan_update_link(&device)) { fault(); return; }
    for (budget = 0; budget < 32 && (board_read(L_TX_INF) & 0x00ff0000UL); budget++) {
        status = board_read(L_TX_STATUS);
        if (status & 0x8000) iface.out_errors++;
        else iface.out_packets++;
    }
    for (budget = 0; budget < 16; budget++) {
        r = lan_receive(&device, rx, &length);
        if (!r) break;
        if (r < 0) {
            iface.in_errors++;
            if (r == -2) { fault(); return; }
            continue;
        }
        b = buf_alloc(length + 100UL, 50, BUF_NORMAL);
        if (!b) { iface.in_errors++; continue; }
        memcpy(b->dstart, rx, length);
        b->dend += length;
        if (iface.bpf) bpf_input(&iface, b);
        type = eth_remove_hdr(b);
        if (if_input(&iface, b, 0, type)) iface.in_errors++;
        else iface.in_packets++;
    }
    for (budget = 0; budget < 16 && device.link; budget++) {
        if (!pending) pending = if_dequeue(&iface.snd);
        if (!pending) break;
        r = lan_send(&device, (unsigned char *)pending->dstart,
                     pending->dend - pending->dstart);
        if (r == 1) break;
        if (r < 0) iface.out_errors++;
        else if (iface.bpf) bpf_input(&iface, pending);
        buf_deref(pending, BUF_NORMAL);
        pending = 0;
    }
    timer = addroottimeout(10, poll_device, 0);
    if (!timer) fault();
}
static long open_device(struct netif *nif)
{
    if (running) return 0;
    memcpy(device.mac, nif->hwlocal.adr.bytes, 6);
    if (lan_start(&device)) return EIO;
    ticks = 0;
    timer = addroottimeout(10, poll_device, 0);
    if (!timer) { lan_stop(&device); return ENOMEM; }
    running = 1;
    nif->flags |= IFF_RUNNING;
    return 0;
}
static long close_device(struct netif *nif)
{
    running = 0;
    if (timer) { cancelroottimeout(timer); timer = 0; }
    lan_stop(&device);
    if (pending) { buf_deref(pending, BUF_NORMAL); pending = 0; }
    if_flushq(&nif->snd);
    nif->flags &= ~IFF_RUNNING;
    return 0;
}
static long output(struct netif *nif, BUF *b, const char *addr, short len, short type)
{
    UNUSED(len);
    if (!running) { buf_deref(b, BUF_NORMAL); return ENETDOWN; }
    b = eth_build_hdr(b, nif, addr, type);
    if (!b) { nif->out_errors++; return ENOMEM; }
    /* if_enqueue owns the buffer even when the queue is full. */
    return if_enqueue(&nif->snd, b, b->info);
}
static long ioctl_device(struct netif *nif, short cmd, long arg)
{
    struct ifreq *ifr;
    struct ifopt *opt;
    switch (cmd) {
    case SIOCSIFFLAGS: case SIOCSIFADDR: case SIOCSIFNETMASK: return 0;
    case SIOCSIFMTU:
        if (nif->mtu > 1500) nif->mtu = 1500;
        if (nif->mtu < 46) nif->mtu = 46;
        return 0;
    case SIOCSIFOPT:
        ifr = (struct ifreq *)arg;
        opt = ifr->ifru.data;
        if (!strncmp(opt->option, "hwaddr", sizeof(opt->option))) {
            if (running) return EBUSY;
            if (opt->valtype != IFO_HWADDR ||
                !lan_mac_valid((unsigned char *)opt->ifou.v_string)) return EINVAL;
            memcpy(nif->hwlocal.adr.bytes, opt->ifou.v_string, 6);
            return 0;
        }
    }
    return ENOSYS;
}
long driver_init(void);
long driver_init(void)
{
    char message[128];
    if (!board_present()) return 1;
    device.io.read = board_read; device.io.write = board_write;
    device.io.delay = delay_us;
    if (lan_probe(&device)) { c_conws("LAN9221: device not detected\r\n"); return 1; }
    if (board_mac(&device)) {
        c_conws("LAN9221: no valid factory/programmed MAC; driver not loaded\r\n");
        return 1;
    }
    strcpy(iface.name, "en"); iface.unit = if_getfreeunit("en");
    iface.flags = IFF_BROADCAST; iface.mtu = 1500; iface.hwtype = HWTYPE_ETH;
    iface.hwlocal.len = iface.hwbrcst.len = 6;
    memcpy(iface.hwlocal.adr.bytes, device.mac, 6);
    memset(iface.hwbrcst.adr.bytes, 255, 6);
    iface.snd.maxqlen = iface.rcv.maxqlen = IF_MAXQ;
    iface.open = open_device; iface.close = close_device;
    iface.output = output; iface.ioctl = ioctl_device;
    iface.maxpackets = 16;
    if (if_register(&iface)) return 1;
    ksprintf(message, "LAN9221: en%d %02x:%02x:%02x:%02x:%02x:%02x (PIO/poll)\r\n",
        iface.unit, device.mac[0], device.mac[1], device.mac[2],
        device.mac[3], device.mac[4], device.mac[5]);
    c_conws(message);
    return 0;
}

/*
 * SNTPv3 client for the DSi-mode totp-nds build.
 *
 * Wire format (RFC 5905 §7.3):
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |LI | VN  |Mode |    Stratum    |     Poll      |   Precision   |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                         Root Delay                            |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                       Root Dispersion                         |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                     Reference Identifier                      |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                Reference Timestamp (64-bit)                   |   16..23
 *   |                                                               |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                Originate Timestamp (64-bit)                   |   24..31
 *   |                                                               |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                 Receive Timestamp  (64-bit)                   |   32..39
 *   |                                                               |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                 Transmit Timestamp (64-bit)                   |   40..47
 *   |                                                               |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * We send a 48-byte request with the LI/VN/Mode byte = 0x1B
 * (LI=0 unknown, VN=3, Mode=3 client) and everything else zero. The
 * server fills in the timestamps and replies. We use the 32-bit
 * Transmit-Timestamp seconds field at offset 40 — sub-second precision
 * is irrelevant for a 30-second TOTP window.
 *
 * NTP epoch is 1900-01-01 UTC; Unix epoch is 1970-01-01 UTC. Offset
 * is 70 years + 17 leap days = 2208988800 seconds.
 */
#include "ntp.h"

#ifdef DSI_BUILD

#include <dswifi9.h>
#include <wfc.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <nds.h>
#include <string.h>

#define NTP_SERVER       "pool.ntp.org"
#define NTP_PORT         123
#define NTP_PACKET_BYTES 48
#define NTP_EPOCH_OFFSET 2208988800UL   /* era 0: NTP 1900 -> Unix 1970  */
#define NTP_ERA1_OFFSET  2085978496UL   /* era 1: 2^32 - NTP_EPOCH_OFFSET */

/* Frames to wait for the radio to reach WlMgrState_Stopped. Purely a
 * courtesy wait so we don't leave it half-torn-down; we return either
 * way. */
#define WIFI_STOP_BUDGET (2 * 60)       /* 2s */

/* Frame budgets — each frame is 1/60 s. Association gets a generous
 * window because WPA2 + DHCP on a cold radio is genuinely slow; the
 * user can bail out of it at any frame, so a long ceiling costs
 * nothing. The NTP round-trip itself gets much less. */
#define ASSOC_FRAME_BUDGET  (20 * 60)   /* 20s */
#define RECV_FRAME_BUDGET   ( 5 * 60)   /*  5s */

/* Wifi_AssocStatus() reports DISCONNECTED both for "failed" and for
 * "hasn't started yet". We only trust it as a failure once the state
 * machine has been seen to move, or once this grace window has passed. */
#define ASSOC_GRACE_FRAMES  30          /* 0.5s */

/* Pump the cancel check on every frame so the user can bail with B. */
static int b_pressed(void) {
    scanKeys();
    return (keysDown() & KEY_B) ? 1 : 0;
}

/* Tear the radio down on the way out and pass the caller's result code
 * through, so every exit path below stays a one-liner.
 *
 * Two steps, because they do different things. Wifi_DisconnectAP()
 * disassociates, which drops the link but leaves the wireless interface
 * started and powered — dswifi has no deinit entry point of its own, and
 * its examples simply never shut down. wlmgrStop() is the layer that
 * actually stops the interface: calico owns the hardware (Mitsumi on DS,
 * Atheros on DSi) and wlmgr is the manager dswifi is built on, so this is
 * the documented way to reach WlMgrState_Stopped. Skipping it leaves the
 * radio powered for the rest of the session, which on a DS is a real
 * battery cost after a lookup that takes seconds.
 *
 * We only sync once, at boot, so tearing the stack down underneath dswifi
 * is safe here — nothing touches the network afterwards. A future re-sync
 * feature would need to call Wifi_InitDefault() again, not assume the
 * stack is still up.
 *
 * Only valid once Wifi_InitDefault() has succeeded. */
static int wifi_down(int rc) {
    int frame;

    Wifi_DisconnectAP();
    wlmgrStop();

    /* Courtesy wait so we don't return mid-teardown. Bounded: if the
     * interface never reports Stopped we return anyway rather than
     * hanging the boot on a power-saving nicety. */
    for (frame = 0; frame < WIFI_STOP_BUDGET; frame++) {
        if (wlmgrGetState() == WlMgrState_Stopped) break;
        swiWaitForVBlank();
    }
    return rc;
}

int ntp_sync(uint32_t *out_epoch) {
    uint8_t pkt[NTP_PACKET_BYTES];
    int sock;
    int status;
    int frame;
    int seen_progress = 0;

    if (out_epoch == 0) return NTP_BAD_REPLY;

    /* 1. Bring the WiFi stack up WITHOUT connecting.
     *
     *    INIT_ONLY leaves association to us. The alternative,
     *    Wifi_InitDefault(WFC_CONNECT), blocks inside dswifi until the
     *    connection resolves — which is exactly what we can't have here:
     *    the UI promises "B cancels" while ntp_sync() runs, and a
     *    blocking call makes that a lie for the longest phase of the
     *    operation. See dswifi's own autoconnect example, which reads
     *    the assigned IP on the line right after the call returns. */
    if (!Wifi_InitDefault(INIT_ONLY)) return NTP_WIFI_FAIL;

    /* 2. Pull the firmware's stored WFC profiles into dswifi.
     *
     *    On a DSi this reaches all six slots — the three legacy DS ones
     *    plus the three DSi-only slots, which are the WPA/WPA2-capable
     *    ones. That matters: the legacy WFC_CONNECT path is DS-era and
     *    open/WEP only, so before this an ordinary WPA2 home network
     *    was simply unreachable. */
    wfcClearConnSlots();   /* deterministic: never stack duplicate slots */
    wfcLoadFromNvram();
    if (wfcGetNumSlots() == 0u) return wifi_down(NTP_NO_PROFILE);

    /* 3. Start association. Returns immediately; progress shows up
     *    through Wifi_AssocStatus() below. */
    if (!wfcBeginAutoConnect()) return wifi_down(NTP_NOT_ASSOC);

    /* 4. Poll until associated, sampling B every frame so the sync is
     *    actually cancellable this time. */
    for (frame = 0; frame < ASSOC_FRAME_BUDGET; frame++) {
        swiWaitForVBlank();
        if (!pmMainLoop()) return wifi_down(NTP_CANCELLED);
        if (b_pressed())   return wifi_down(NTP_CANCELLED);

        status = Wifi_AssocStatus();
        if (status == ASSOCSTATUS_ASSOCIATED) break;
        if (status != ASSOCSTATUS_DISCONNECTED) {
            seen_progress = 1;
            continue;
        }
        /* Back at DISCONNECTED. Genuine failure if we ever saw the
         * state machine move, or if it never moved within the grace
         * window. */
        if (seen_progress || frame >= ASSOC_GRACE_FRAMES) {
            return wifi_down(NTP_NOT_ASSOC);
        }
    }
    if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) {
        return wifi_down(NTP_NOT_ASSOC);
    }

    /* 5. Resolve the NTP pool. dswifi's gethostbyname uses the DNS
     *    server learned via DHCP during association. */
    struct hostent *he = gethostbyname(NTP_SERVER);
    if (he == 0 || he->h_addr_list == 0 || he->h_addr_list[0] == 0) {
        return wifi_down(NTP_DNS_FAIL);
    }

    /* 6. UDP socket. */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return wifi_down(NTP_SOCK_FAIL);

    /* 7. Build + send the SNTP request. */
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x1B;  /* LI=0, VN=3, Mode=3 (client) */

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(NTP_PORT);
    memcpy(&srv.sin_addr, he->h_addr_list[0], sizeof(srv.sin_addr));

    if (sendto(sock, pkt, NTP_PACKET_BYTES, 0,
               (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        closesocket(sock);
        return wifi_down(NTP_SOCK_FAIL);
    }

    /* 8. Poll for the reply within RECV_FRAME_BUDGET frames. dswifi's
     *    socket layer exposes non-blocking via FIONBIO ioctl rather
     *    than SO_NONBLOCK setsockopt — we set it so the recvfrom loop
     *    below can check for "nothing yet" without freezing the boot. */
    int nonblock = 1;
    ioctl(sock, FIONBIO, &nonblock);

    int got = -1;
    for (frame = 0; frame < RECV_FRAME_BUDGET; frame++) {
        swiWaitForVBlank();
        if (!pmMainLoop() || b_pressed()) {
            closesocket(sock);
            return wifi_down(NTP_CANCELLED);
        }
        got = recvfrom(sock, pkt, NTP_PACKET_BYTES, 0, 0, 0);
        if (got == NTP_PACKET_BYTES) break;
    }
    closesocket(sock);
    if (got != NTP_PACKET_BYTES) return wifi_down(NTP_TIMEOUT);

    /* 9. Sanity-check the reply. Stratum 0 means kiss-of-death — the
     *    server is refusing service for this client. LI=3 means
     *    "alarm" (clock not sync'd). Mode must be 4 (server); anything
     *    else is not an answer to what we asked. */
    uint8_t li   = (pkt[0] >> 6) & 0x03u;
    uint8_t mode =  pkt[0]       & 0x07u;
    if (pkt[1] == 0 || li == 3u || mode != 4u) return wifi_down(NTP_BAD_REPLY);

    /* 10. Extract transmit timestamp seconds (offset 40, big-endian)
     *     and convert to a Unix epoch.
     *
     *     The NTP seconds field is 32 bits counting from 1900, so it
     *     wraps on 2036-02-07 06:28:16 UTC. RFC 4330 §3 resolves the
     *     ambiguity by the top bit: set means era 0 (1968-2036, counted
     *     from 1900), clear means era 1 (2036-2104, counted from the
     *     wrap point). Handling only era 0 would have made this app
     *     reject every reply from 2036 onward — the old
     *     "ntp_secs < NTP_EPOCH_OFFSET" guard would fire on every
     *     era-1 timestamp and return NTP_BAD_REPLY forever.
     *
     *     NTP_ERA1_OFFSET is 2^32 - NTP_EPOCH_OFFSET, so era 1 is just
     *     the era-0 arithmetic carried across the wrap. */
    uint32_t ntp_secs = ((uint32_t)pkt[40] << 24) |
                        ((uint32_t)pkt[41] << 16) |
                        ((uint32_t)pkt[42] <<  8) |
                         (uint32_t)pkt[43];

    if (ntp_secs & 0x80000000UL) {
        /* Era 0. Values below the 1970 mark are 1968-69 — the server is
         * talking nonsense, and accepting it would set the clock back
         * decades. */
        if (ntp_secs < NTP_EPOCH_OFFSET) return wifi_down(NTP_BAD_REPLY);
        *out_epoch = ntp_secs - NTP_EPOCH_OFFSET;
    } else {
        /* Era 1: 2036-02-07 onward. */
        *out_epoch = ntp_secs + NTP_ERA1_OFFSET;
    }
    return wifi_down(NTP_OK);
}

#else  /* !DSI_BUILD */

/* On the .nds build dswifi9 is still linkable, but WPA2 networks won't
 * work on original DS and we don't want to bloat the artifact with the
 * full WiFi stack. Stub returns "couldn't init WiFi" so main.c falls
 * through to the existing manual time-set path. */
int ntp_sync(uint32_t *out_epoch) {
    (void)out_epoch;
    return NTP_WIFI_FAIL;
}

#endif

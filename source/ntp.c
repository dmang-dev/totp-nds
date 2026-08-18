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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <nds.h>
#include <string.h>

#define NTP_SERVER       "pool.ntp.org"
#define NTP_PORT         123
#define NTP_PACKET_BYTES 48
#define NTP_EPOCH_OFFSET 2208988800UL

/* Association budget — pool.ntp.org should respond well inside 5s on a
 * working WiFi. Each frame is 1/60 s. */
#define ASSOC_FRAME_BUDGET   (10 * 60)   /* 10s */
#define RECV_FRAME_BUDGET    ( 5 * 60)   /* 5s */

/* Pump the cancel check on every frame so the user can bail with B. */
static int b_pressed(void) {
    scanKeys();
    return (keysDown() & KEY_B) ? 1 : 0;
}

int ntp_sync(uint32_t *out_epoch) {
    uint8_t pkt[NTP_PACKET_BYTES];
    int sock;
    int status;
    int frame;

    if (out_epoch == 0) return NTP_BAD_REPLY;

    /* 1. Init dswifi using firmware-saved profile. */
    if (!Wifi_InitDefault(WFC_CONNECT)) return NTP_WIFI_FAIL;

    /* 2. Wait for ASSOCIATED. Wifi_InitDefault returns immediately and
     *    leaves the connection in progress; we poll AssocStatus until
     *    it lands. */
    for (frame = 0; frame < ASSOC_FRAME_BUDGET; frame++) {
        swiWaitForVBlank();
        if (b_pressed()) return NTP_CANCELLED;
        status = Wifi_AssocStatus();
        if (status == ASSOCSTATUS_ASSOCIATED) break;
        if (status == ASSOCSTATUS_DISCONNECTED && frame > 60) {
            /* Connection genuinely failed (not just "not yet started"). */
            return NTP_NOT_ASSOC;
        }
    }
    if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) return NTP_NOT_ASSOC;

    /* 3. Resolve the NTP pool. dswifi's gethostbyname uses the DNS
     *    server learned via DHCP during association. */
    struct hostent *he = gethostbyname(NTP_SERVER);
    if (he == 0 || he->h_addr_list == 0 || he->h_addr_list[0] == 0) {
        return NTP_DNS_FAIL;
    }

    /* 4. UDP socket. */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return NTP_SOCK_FAIL;

    /* 5. Build + send the SNTP request. */
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
        return NTP_SOCK_FAIL;
    }

    /* 6. Poll for the reply within RECV_FRAME_BUDGET frames. dswifi's
     *    socket layer exposes non-blocking via FIONBIO ioctl rather
     *    than SO_NONBLOCK setsockopt — we set it so the recvfrom loop
     *    below can check for "nothing yet" without freezing the boot. */
    int nonblock = 1;
    ioctl(sock, FIONBIO, &nonblock);

    int got = -1;
    for (frame = 0; frame < RECV_FRAME_BUDGET; frame++) {
        swiWaitForVBlank();
        if (b_pressed()) { closesocket(sock); return NTP_CANCELLED; }
        got = recvfrom(sock, pkt, NTP_PACKET_BYTES, 0, 0, 0);
        if (got == NTP_PACKET_BYTES) break;
    }
    closesocket(sock);
    if (got != NTP_PACKET_BYTES) return NTP_TIMEOUT;

    /* 7. Sanity-check the reply. Stratum 0 means kiss-of-death — the
     *    server is refusing service for this client. LI=3 means
     *    "alarm" (clock not sync'd). */
    uint8_t li = (pkt[0] >> 6) & 0x03u;
    if (pkt[1] == 0 || li == 3u) return NTP_BAD_REPLY;

    /* 8. Extract transmit timestamp seconds (offset 40, big-endian) and
     *    convert from NTP epoch to Unix epoch. */
    uint32_t ntp_secs = ((uint32_t)pkt[40] << 24) |
                        ((uint32_t)pkt[41] << 16) |
                        ((uint32_t)pkt[42] <<  8) |
                         (uint32_t)pkt[43];
    if (ntp_secs < NTP_EPOCH_OFFSET) return NTP_BAD_REPLY;  /* pre-1970 */

    *out_epoch = ntp_secs - NTP_EPOCH_OFFSET;
    return NTP_OK;
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

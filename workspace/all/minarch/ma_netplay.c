#define _POSIX_C_SOURCE 200809L
#include "ma_netplay.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define NP_HELLO 160
#define NP_MAX_PACKET 65536
#define NP_QUEUE (1024 * 1024)
#define NP_TIMEOUT 10000
enum { W_DATA = 1, W_PAUSE, W_PING };
static struct {
    struct retro_netpacket_callback cb;
    enum NPState state;
    int listener, fd;
    bool supported, host, started, registered, used, local_pause, remote_pause, polling, relay;
    unsigned char preface[16];
    unsigned char hello[NP_HELLO], peer[NP_HELLO];
    size_t hello_read;
    unsigned char tx[NP_QUEUE], rx[NP_MAX_PACKET + 8];
    size_t tx_start, tx_end, rx_end;
    uint64_t last_rx, last_ping, began, sent, received;
    char status[160];
} np = { .listener = -1, .fd = -1 };

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void status(enum NPState state, const char *text) {
    np.state = state;
    snprintf(np.status, sizeof(np.status), "%s", text);
}
static void close_fd(int *fd) { if (*fd >= 0) close(*fd); *fd = -1; }
static void end_session(const char *reason, bool error) {
    bool started = np.started;
    np.started = false;
    close_fd(&np.fd);
    close_fd(&np.listener);
    np.local_pause = np.remote_pause = false;
    np.tx_start = np.tx_end = np.rx_end = np.hello_read = 0;
    if (started) {
        if (np.registered && np.cb.disconnected) np.cb.disconnected(1);
        if (np.cb.stop) np.cb.stop();
    }
    np.registered = false;
    status(error ? NP_ERROR : NP_IDLE, reason);
}
void Netplay_stop(const char *reason) { end_session(reason ? reason : "Disconnected", false); }
void Netplay_clear(void) {
    Netplay_stop("Offline");
    memset(&np.cb, 0, sizeof(np.cb));
    np.supported = np.used = false;
    memset(np.hello, 0, sizeof(np.hello));
}
bool Netplay_setInterface(const struct retro_netpacket_callback *cb) {
    if (!cb || !cb->start || !cb->receive || !cb->protocol_version ||
        strlen(cb->protocol_version) >= 64 || Netplay_busy()) return false;
    np.cb = *cb;
    np.supported = true;
    memset(np.hello, 0, sizeof(np.hello));
    memcpy(np.hello, "NEXTUILINK\0\2", 12);
    snprintf((char *)np.hello + 12, 64, "%s", cb->protocol_version);
    return true;
}
void Netplay_identity(const uint8_t sha[32], const uint8_t core_sha[32], const char *mode) {
    memcpy(np.hello + 76, sha, 32);
    memset(np.hello + 108, 0, 19);
    memcpy(np.hello + 127, core_sha, 32);
    snprintf((char *)np.hello + 108, 19, "%s", mode ? mode : "auto");
}
bool Netplay_supported(void) { return np.supported; }
bool Netplay_active(void) { return np.state == NP_ACTIVE; }
bool Netplay_busy(void) { return np.state >= NP_LISTENING && np.state <= NP_ACTIVE; }
bool Netplay_paused(void) { return np.local_pause || np.remote_pause; }
bool Netplay_localPaused(void) { return np.local_pause; }
bool Netplay_used(void) { return np.used; }
enum NPState Netplay_state(void) { return np.state; }
const char *Netplay_status(void) {
    if (Netplay_active() && np.local_pause) return "Link paused by you";
    if (Netplay_active() && np.remote_pause) return "Link paused by peer";
    return np.status[0] ? np.status : "Offline";
}
uint64_t Netplay_sent(void) { return np.sent; }
uint64_t Netplay_received(void) { return np.received; }

static bool configure_socket(int fd) {
    int one = 1;
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) return false;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    return true;
}
static bool queue_bytes(const void *buf, size_t len) {
    if (np.tx_start) {
        memmove(np.tx, np.tx + np.tx_start, np.tx_end - np.tx_start);
        np.tx_end -= np.tx_start;
        np.tx_start = 0;
    }
    if (len > NP_QUEUE - np.tx_end) {
        end_session("Link send queue full; reconnect", true);
        return false;
    }
    memcpy(np.tx + np.tx_end, buf, len);
    np.tx_end += len;
    return true;
}
static void flush(void) {
    while (np.fd >= 0 && np.tx_start < np.tx_end) {
        ssize_t n = send(np.fd, np.tx + np.tx_start, np.tx_end - np.tx_start, MSG_NOSIGNAL);
        if (n > 0) np.tx_start += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        else { end_session("Peer disconnected while sending", true); return; }
    }
    if (np.tx_start == np.tx_end) np.tx_start = np.tx_end = 0;
}
static void packet(unsigned type, const void *buf, size_t len) {
    if (!Netplay_active()) return;
    if (len > NP_MAX_PACKET) { end_session("Link packet too large", true); return; }
    uint32_t header[2] = { htonl(type), htonl((uint32_t)len) };
    if (queue_bytes(header, 8) && len) queue_bytes(buf, len);
    flush();
}
static void core_send(int flags, const void *buf, size_t len, uint16_t to) {
    (void)flags; /* All packets are reliable; every send flushes immediately. */
    if (!Netplay_active()) return;
    if ((to != RETRO_NETPACKET_BROADCAST && to != (np.host ? 1 : 0)) || (!buf && len)) {
        end_session("Invalid destination from core", true); return;
    }
    if (buf && len) { packet(W_DATA, buf, len); np.sent++; }
    else flush();
}
void Netplay_pause(bool paused) {
    if (np.local_pause == paused) return;
    np.local_pause = paused;
    unsigned char value = paused;
    packet(W_PAUSE, &value, 1);
}
static void handshake(void) {
    np.hello_read = np.rx_end = np.tx_start = np.tx_end = 0;
    np.last_rx = np.began = np.last_ping = now_ms();
    status(NP_HANDSHAKE, "Checking core, ROM and link mode...");
    if (np.relay) queue_bytes(np.preface, sizeof(np.preface));
    queue_bytes(np.hello, NP_HELLO);
    flush();
}
static bool prepare(bool host) {
    if (!np.supported) { status(NP_ERROR, "This core has no link interface"); return false; }
    Netplay_stop("Offline");
    np.host = host;
    np.relay = false;
    np.hello[159] = host ? 1 : 0;
    np.sent = np.received = 0;
    np.began = np.last_rx = now_ms();
    return true;
}
bool Netplay_host(uint16_t port) {
    if (!port || !prepare(true)) return false;
    np.listener = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(np.listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (np.listener < 0 || !configure_socket(np.listener) ||
        bind(np.listener, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(np.listener, 1) < 0) {
        end_session("Cannot host; check port or Wi-Fi", true); return false;
    }
    status(NP_LISTENING, "Hosting: waiting for a second Brick");
    return true;
}
static bool connect_peer(const char *ipv4, uint16_t port, const char *room, bool host) {
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
    if (!ipv4 || !port || inet_pton(AF_INET, ipv4, &addr.sin_addr) != 1) {
        end_session("Enter a valid IPv4 address and port", true); return false;
    }
    if (room) {
        if (strlen(room) != 6 || strspn(room, "0123456789") != 6) {
            end_session("Room code must be six digits", true); return false;
        }
    }
    if (!prepare(host)) return false;
    if (room) {
        np.relay = true;
        memcpy(np.preface, "NXRELAY1", 8);
        memcpy(np.preface + 8, room, 6);
        np.preface[14] = host ? 'H' : 'J';
        np.preface[15] = '\n';
    }
    np.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (np.fd < 0 || !configure_socket(np.fd)) { end_session("Cannot open link socket", true); return false; }
    int result = connect(np.fd, (struct sockaddr *)&addr, sizeof(addr));
    if (result == 0) handshake();
    else if (errno == EINPROGRESS) status(NP_CONNECTING, "Connecting to host...");
    else { end_session("Cannot reach host", true); return false; }
    return true;
}
bool Netplay_join(const char *ipv4, uint16_t port) { return connect_peer(ipv4, port, NULL, false); }
bool Netplay_relay(const char *ipv4, uint16_t port, const char *room, bool host) {
    return connect_peer(ipv4, port, room, host);
}
static void start_core(void) {
    if (memcmp(np.peer, np.hello, 12)) { end_session("NextUI link protocol mismatch", true); return; }
    if (memcmp(np.peer + 12, np.hello + 12, 64)) { end_session("Core link version mismatch", true); return; }
    if (memcmp(np.peer + 76, np.hello + 76, 32)) { end_session("ROM mismatch: use identical ROM files", true); return; }
    if (memcmp(np.peer + 108, np.hello + 108, 19)) { end_session("Link mode mismatch", true); return; }
    if (memcmp(np.peer + 127, np.hello + 127, 32)) { end_session("Core build mismatch: install the same update", true); return; }
    if (np.peer[159] != (np.host ? 0 : 1)) { end_session("Expected opposite host/join role", true); return; }
    status(NP_ACTIVE, "Link connected (2 players)");
    np.started = np.used = true;
    np.cb.start(np.host ? 0 : 1, core_send, Netplay_poll);
    if (!Netplay_active()) return;
    if (np.host && np.cb.connected) {
        if (!np.cb.connected(1)) { end_session("Core rejected second player", true); return; }
        np.registered = true;
    }
    unsigned char value = np.local_pause;
    packet(W_PAUSE, &value, 1);
}
/* The pinned gpSP RFU receiver trusts some inner lengths. Validate them
 * before dispatching to that core, including its 16-byte client data buffer.
 * RFU commands use a 12-byte prefix (usually padded to 16); data sends are
 * padded to 104 bytes. Do not mistake padding for the declared payload size. */
static bool valid_core_packet(const unsigned char *data, size_t len) {
    if (strcmp(np.cb.protocol_version, "gpSP v1.0") || len < 4 || memcmp(data, "RFU1", 4)) return true;
    if (len < 12) return false;
    uint32_t fields[2]; memcpy(fields, data + 4, 8);
    uint32_t type = ntohl(fields[0]), info = ntohl(fields[1]);
    if (type > 7) return false;
    if (type == 0) return len >= 36; /* broadcast: six 32-bit words */
    if (type == 2) return (info >> 16) < 4; /* assigned RFU slot */
    if (type == 5) return len >= 12 + (info & 0x7f); /* host payload <=127, storage=128 */
    if (type == 6) {
        size_t bytes = info >> 24;
        return bytes <= 16 && len >= 12 + bytes; /* client storage=16 */
    }
    return true;
}
static void consume(void) {
    while (Netplay_active() && np.rx_end >= 8) {
        uint32_t header[2]; memcpy(header, np.rx, 8);
        unsigned type = ntohl(header[0]);
        size_t len = ntohl(header[1]);
        if (len > NP_MAX_PACKET || type < W_DATA || type > W_PING ||
            (type == W_DATA && !len) || (type == W_PAUSE && len != 1) || (type == W_PING && len)) {
            end_session("Invalid link frame", true); return;
        }
        if (np.rx_end < len + 8) return;
        if (type == W_DATA) {
            if (!valid_core_packet(np.rx + 8, len)) { end_session("Invalid gpSP RFU payload", true); return; }
            np.received++;
            np.cb.receive(np.rx + 8, len, np.host ? 1 : 0);
        } else if (type == W_PAUSE) {
            if (np.rx[8] > 1) { end_session("Invalid pause frame", true); return; }
            np.remote_pause = np.rx[8] != 0;
        }
        /* Callback may have stopped the session. */
        if (!Netplay_active()) return;
        memmove(np.rx, np.rx + 8 + len, np.rx_end - 8 - len);
        np.rx_end -= 8 + len;
    }
}
void Netplay_poll(void) {
    if (np.polling || !Netplay_busy()) return;
    np.polling = true;
    uint64_t now = now_ms();
    if (np.state == NP_LISTENING) {
        int fd = accept(np.listener, NULL, NULL);
        if (fd >= 0) {
            np.fd = fd;
            close_fd(&np.listener);
            if (!configure_socket(fd)) end_session("Cannot configure peer socket", true);
            else handshake();
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            end_session("Cannot accept peer", true);
    }
    if (np.state == NP_CONNECTING) {
        struct pollfd p = { .fd = np.fd, .events = POLLOUT };
        if (poll(&p, 1, 0) > 0) {
            int error = 0; socklen_t size = sizeof(error);
            if (getsockopt(np.fd, SOL_SOCKET, SO_ERROR, &error, &size) || error)
                end_session("Cannot reach host", true);
            else handshake();
        }
    }
    flush();
    /* Bound per-frame work even when an untrusted peer floods the socket. */
    for (int i = 0; i < 128 && np.fd >= 0 && np.state != NP_CONNECTING; i++) {
        bool hello = np.state == NP_HANDSHAKE;
        unsigned char *dst = hello ? np.peer + np.hello_read : np.rx + np.rx_end;
        size_t cap = hello ? NP_HELLO - np.hello_read : sizeof(np.rx) - np.rx_end;
        if (!cap) { end_session("Link receive queue full", true); break; }
        ssize_t n = recv(np.fd, dst, cap, 0);
        if (n > 0) {
            np.last_rx = now;
            if (hello) { np.hello_read += (size_t)n; if (np.hello_read == NP_HELLO) start_core(); }
            else { np.rx_end += (size_t)n; consume(); }
        } else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        else { end_session("Peer disconnected", true); break; }
    }
    now = now_ms(); /* Handshake may have advanced began/last_ping during this poll. */
    if (Netplay_active()) {
        if (np.cb.poll) np.cb.poll();
        if (now - np.last_ping >= 1000) { packet(W_PING, NULL, 0); np.last_ping = now; }
        if (now - np.last_rx >= NP_TIMEOUT) end_session("Peer timed out", true);
    } else if ((np.state == NP_CONNECTING || np.state == NP_HANDSHAKE) && now - np.began >= (np.relay && np.state == NP_HANDSHAKE ? 120000 : NP_TIMEOUT))
        end_session("Connection timed out", true);
    np.polling = false;
}

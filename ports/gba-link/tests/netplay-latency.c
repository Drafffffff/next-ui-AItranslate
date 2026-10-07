/* Actual production transport: same-poll reply batching and pacing wakeups. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "ma_netplay.c"
static unsigned callbacks;
static bool replies;
static unsigned char host_data[16] = {'R','F','U','1',0,0,0,5,0,0,0,4,1,2,3,4};
static unsigned char uplink[16] = {'R','F','U','1',0,0,0,6,4,0,0,42,5,6,7,8};
static unsigned char ack[12] = {'R','F','U','1',0,0,0,7,0,0,0,42};
static void begin(uint16_t id, retro_netpacket_send_t send_fn, retro_netpacket_poll_receive_t poll_fn) {
    (void)id; (void)send_fn; (void)poll_fn;
}
static void receive_data(const void *buf, size_t len, uint16_t id) {
    (void)buf; (void)len; (void)id; callbacks++;
    if (replies) {
        core_send(RETRO_NETPACKET_RELIABLE, uplink, sizeof(uplink), 0);
        core_send(RETRO_NETPACKET_RELIABLE, ack, sizeof(ack), 0);
    }
}
static void setup(int sockets[2], bool host) {
    Netplay_clear();
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct retro_netpacket_callback cb = {.start=begin,.receive=receive_data,.protocol_version="latency fixture"};
    assert(Netplay_setInterface(&cb)); assert(prepare(host));
    np.fd=sockets[0]; assert(configure_socket(np.fd)); np.last_ping=now_ms();
    memcpy(np.peer,np.hello,sizeof(np.peer)); np.peer[159]=!host;
    start_core(); assert(Netplay_active()); callbacks=0;
    unsigned char initial_pause[9]; assert(recv(sockets[1],initial_pause,9,0)==9);
    np.send_calls=0;
}
static void incoming(int fd, const void *buf, size_t len) {
    unsigned char frame[128]; assert(len+8<=sizeof(frame));
    uint32_t header[2]={htonl(W_DATA),htonl(len)};
    memcpy(frame,header,8); memcpy(frame+8,buf,len);
    assert(write(fd,frame,len+8)==(ssize_t)(len+8));
}
static void check_replies(int fd) {
    unsigned char data[64]; ssize_t len=recv(fd,data,sizeof(data),0);
    assert(len==44); // uplink frame (24), ACK frame (20), unchanged ordering.
    assert(!memcmp(data+8,uplink,16)); assert(!memcmp(data+32,ack,12));
}
int main(void) {
    int sockets[2]; replies=true; setup(sockets,false);
    incoming(sockets[1],host_data,sizeof(host_data)); Netplay_poll();
    assert(callbacks==1 && np.send_calls==1 && np.tx_end==0); check_replies(sockets[1]);
    puts("PASS: uplink and ACK share one write and leave before receive poll returns");
    Netplay_stop("next test"); close(sockets[1]);

    setup(sockets,false);
    fflush(stdout); pid_t child=fork(); assert(child>=0);
    if (!child) {
        close(sockets[0]); struct timespec t={.tv_nsec=2000000}; nanosleep(&t,NULL);
        uint64_t start=now_us(); incoming(sockets[1],host_data,sizeof(host_data));
        struct pollfd p={.fd=sockets[1],.events=POLLIN};
        if (poll(&p,1,40)!=1) _exit(1);
        check_replies(sockets[1]);
        printf("pacing reply latency_us=%llu\n",(unsigned long long)(now_us()-start)); fflush(stdout);
        t.tv_nsec=30000000; nanosleep(&t,NULL); close(sockets[1]); _exit(0);
    }
    uint64_t start=now_us();
    bool sleep_only=getenv("NP_BASELINE_WAIT") != NULL;
    if (sleep_only) { struct timespec t={.tv_nsec=16000000}; nanosleep(&t,NULL); Netplay_poll(); }
    else Netplay_wait(16000);
    uint64_t elapsed=now_us()-start;
    assert(callbacks==1 && np.wait_packets==(sleep_only ? 0 : 1) && elapsed>=16000 && elapsed<60000);
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    puts(sleep_only ? "PASS: sleep-only control defers reply until pacing ends" :
         "PASS: arrival wakes pacing wait and replies promptly without shortening the frame");
    Netplay_stop("next test"); close(sockets[1]);

    replies=false; setup(sockets,true);
    core_send(0,host_data,sizeof(host_data),1); core_send(0,host_data,sizeof(host_data),1);
    incoming(sockets[1],ack,sizeof(ack)); incoming(sockets[1],ack,sizeof(ack)); Netplay_poll();
    assert(np.ack_samples==2 && np.outstanding_peak==2 && np.rfu_received[7]==2);
    assert(np.ack_bins[0]+np.ack_bins[1]+np.ack_bins[2]+np.ack_bins[3]+np.ack_bins[4]==2);
    puts("PASS: ordered ACK samples track real elapsed time and outstanding exchanges");
    /* Overwrite timing ring, not transport state. Old timestamps cannot label new ACKs. */
    np.rfu_sent[5]=300; np.rfu_received[7]=0;
    np.ack_started[0]=now_us(); incoming(sockets[1],ack,sizeof(ack)); Netplay_poll();
    assert(np.ack_samples==2 && np.ack_unmeasured==1);
    puts("PASS: timing ring overflow is reported and never fabricated as a valid RTT");
    Netplay_stop("next test"); close(sockets[1]);

    setup(sockets,false); close(sockets[1]); start=now_us(); Netplay_wait(16000);
    assert(Netplay_state()==NP_ERROR && now_us()-start>=16000);
    puts("PASS: peer close during pacing exits the session without busy looping");
    Netplay_clear(); return 0;
}

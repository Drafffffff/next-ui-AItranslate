#define _POSIX_C_SOURCE 200809L
#include "ma_netplay_bt.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef BIN_PATH
#include "defines.h"
#endif

static struct {
    pid_t child;
    int data, control;
    bool scanning, scan_failed, hotspot;
    unsigned count;
    struct NetplayBTRoom rooms[NETPLAY_BT_ROOMS];
    char scan_status[160];
    struct { pid_t pid; uint64_t began; bool drain, term_sent, hotspot; int control; } retired[8];
} bt = { .data=-1, .control=-1 };

static uint64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
static bool address_valid(const char *p) {
    if (!p || strlen(p)!=17) return false;
    for (int i=0;i<17;i++) {
        if (i%3==2) { if (p[i]!=':') return false; }
        else if (!((p[i]>='0'&&p[i]<='9')||(p[i]>='A'&&p[i]<='F')||(p[i]>='a'&&p[i]<='f'))) return false;
    }
    return true;
}
static void close_fd(int *fd) { if (*fd>=0) close(*fd); *fd=-1; }
void Netplay_btReap(void) {
    uint64_t now=now_ms();
    for (unsigned i=0;i<8;i++) if (bt.retired[i].pid) {
        pid_t result=waitpid(bt.retired[i].pid,NULL,WNOHANG);
        if (result>0 || (result<0&&errno==ECHILD)) { close_fd(&bt.retired[i].control);bt.retired[i].pid=0; }
        else if (bt.retired[i].drain&&!bt.retired[i].term_sent&&now-bt.retired[i].began>=1000) {
            kill(bt.retired[i].pid,SIGTERM);close_fd(&bt.retired[i].control);bt.retired[i].term_sent=true;
        }
        else if (now-bt.retired[i].began>=(bt.retired[i].hotspot?6000:2000)) kill(bt.retired[i].pid,SIGKILL);
    }
}
static void stop_worker(bool drain) {
    close_fd(&bt.data); if (!drain) close_fd(&bt.control);
    bt.scanning=false;
    if (bt.child>0) {
        if (!drain) kill(bt.child,SIGTERM);
        for (unsigned i=0;i<8;i++) if (!bt.retired[i].pid) {
            bt.retired[i].pid=bt.child;bt.retired[i].hotspot=bt.hotspot;bt.retired[i].began=now_ms();bt.retired[i].control=drain?bt.control:-1;if(drain)bt.control=-1;bt.retired[i].drain=drain;bt.retired[i].term_sent=!drain;bt.child=0;break;
        }
    }
    close_fd(&bt.control);
    Netplay_btReap();
}
void Netplay_btStop(void) { stop_worker(false); }
void Netplay_btDrainStop(void) { stop_worker(true); }
static bool launch(const char *role,const char *peer,bool scanning) {
    Netplay_btReap();
    if (bt.child || bt.control>=0) return false;
    /* Keep a slot for cancellation before starting another child. Rapid
     * retries cannot create unreapable workers or block the UI in waitpid. */
    bool slot=false;for (unsigned i=0;i<8;i++) if (!bt.retired[i].pid) slot=true;
    if (!slot) return false;
    const char *helper=BIN_PATH "/nextui-gba-bluetooth.elf";
#ifdef NEXTUI_LINK_BT_TEST
    if (getenv("NEXTUI_BT_HELPER")) helper=getenv("NEXTUI_BT_HELPER");
#endif
    if (access(helper,X_OK)) return false;
    int data[2],control[2];
    if (socketpair(AF_UNIX,SOCK_STREAM,0,data)) return false;
    if (socketpair(AF_UNIX,SOCK_DGRAM,0,control)) {close(data[0]);close(data[1]);return false;}
    for (int i=0;i<2;i++) {
        if (fcntl(data[i],F_SETFD,FD_CLOEXEC)||fcntl(control[i],F_SETFD,FD_CLOEXEC)||
            fcntl(data[i],F_SETFL,O_NONBLOCK)||fcntl(control[i],F_SETFL,O_NONBLOCK)) {
            close(data[0]);close(data[1]);close(control[0]);close(control[1]);return false;
        }
    }
    long maximum=sysconf(_SC_OPEN_MAX);if (maximum<0) maximum=1024;
    char *const arguments[]={(char*)helper,"-role",(char*)role,"-data-fd","3","-control-fd","4","-peer",(char*)(peer?peer:""),NULL};
    extern char **environ;
    pid_t child=fork();
    if (child==0) {
        close(data[0]);close(control[0]);
        /* Duplicating first avoids collisions with fd 3/4 in the parent. */
        int df=fcntl(data[1],F_DUPFD,5),cf=fcntl(control[1],F_DUPFD,5);
        if (df<0||cf<0||dup2(df,3)<0||dup2(cf,4)<0) _exit(126);
        close(df);close(cf);
        if (data[1]>4) close(data[1]);
        if (control[1]>4) close(control[1]);
        /* A forked helper must not inherit game files, frontend locks, audio
         * descriptors, old network sessions or any unrelated credentials. */
        for (int fd=5;fd<maximum;fd++) close(fd);
        execve(helper,arguments,environ);
        _exit(127);
    }
    close(data[1]);close(control[1]);
    if (child<0) {close(data[0]);close(control[0]);return false;}
    bt.child=child;bt.data=data[0];bt.control=control[0];bt.scanning=scanning;bt.hotspot=!strncmp(role,"hotspot-",8);bt.scan_failed=false;bt.count=0;
    snprintf(bt.scan_status,sizeof(bt.scan_status),"Searching nearby rooms...");
    return true;
}
bool Netplay_btStart(bool host,const char *peer) {
    if (!host&&!address_valid(peer)) return false;
    return launch(host?"serve-host":"serve-join",peer,false);
}
bool Netplay_btScan(void) {return launch("serve-scan",NULL,true);}
bool Netplay_hpStart(bool host,const char *peer) {
    if (!host&&!address_valid(peer)) return false;
    return launch(host?"hotspot-host":"hotspot-join",peer,false);
}
bool Netplay_hpScan(void) {return launch("hotspot-scan",NULL,true);}
bool Netplay_btScanning(void) {return bt.scanning;}
bool Netplay_btScanFailed(void) {return bt.scan_failed;}
unsigned Netplay_btRooms(struct NetplayBTRoom rooms[NETPLAY_BT_ROOMS]) {
    if (rooms) memcpy(rooms,bt.rooms,bt.count*sizeof(*rooms));
    return bt.count;
}
const char *Netplay_btScanStatus(void) {return bt.scan_status;}
bool Netplay_btEvent(struct NetplayBTEvent *event) {
    Netplay_btReap();
    if (!event || bt.control<0) return false;
    memset(event,0,sizeof(*event));event->fd=-1;
    unsigned char packet[2+83*NETPLAY_BT_ROOMS+1];
    ssize_t size=recv(bt.control,packet,sizeof(packet),0);
    if (size<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)) {
        if (bt.child>0&&waitpid(bt.child,NULL,WNOHANG)==bt.child) {
            bt.scan_failed=true;bt.child=0;event->kind='E';snprintf(event->text,sizeof(event->text),bt.hotspot?"Hotspot helper stopped. Try again.":"Bluetooth helper stopped. Try again.");
            snprintf(bt.scan_status,sizeof(bt.scan_status),"%s",event->text);
            return true;
        }
        return false;
    }
    if (size<1) goto invalid;
    event->kind=(char)packet[0];
    if (event->kind=='R'&&bt.scanning) {
        if (size<2||packet[1]>NETPLAY_BT_ROOMS||size!=2+83*packet[1]) goto invalid;
        struct NetplayBTRoom rooms[NETPLAY_BT_ROOMS]={0};
        for (unsigned i=0;i<packet[1];i++) {
            unsigned offset=2+i*83;
            if (packet[offset+17]||packet[offset+18+63]) goto invalid;
            memcpy(rooms[i].address,packet+offset,18);memcpy(rooms[i].label,packet+offset+18,64);
            if (!address_valid(rooms[i].address)) goto invalid;
            for (unsigned j=0;j<i;j++) if (!strcmp(rooms[i].address,rooms[j].address)) goto invalid;
            rooms[i].rssi=(signed char)packet[offset+82];
        }
        memcpy(bt.rooms,rooms,sizeof(rooms));bt.count=packet[1];return true;
    }
    if (size>160||!event->kind||!strchr("SWCHEKP",event->kind)) goto invalid;
    memcpy(event->text,packet+1,(size_t)size-1);
    if (event->kind=='H') {
        if (bt.scanning||bt.data<0) goto invalid;
        event->fd=bt.data;bt.data=-1;
    }
    if (event->kind=='E') bt.scan_failed=true;
    if (bt.scanning&&event->kind!='K') snprintf(bt.scan_status,sizeof(bt.scan_status),"%s",event->text);
    return true;
invalid:
    bt.scan_failed=true;
    snprintf(bt.scan_status,sizeof(bt.scan_status),"Bluetooth search ended. Try again.");
    event->kind='E';snprintf(event->text,sizeof(event->text),"Invalid Bluetooth helper response.");return true;
}

#define _POSIX_C_SOURCE 200809L
#include "ma_netplay_bt.h"
#include <assert.h>
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

static uint64_t milliseconds(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void tick(void) {struct timespec t={0,1000000};nanosleep(&t,NULL);}
/* Execute the same private FD contract as the production Go helper. This
 * fixture tests frontend process ownership; it does not claim radio coverage. */
static int helper(void) {
    int marker=atoi(getenv("BT_TEST_MARKER"));
    if (fcntl(marker,F_GETFD)!=-1 || errno!=EBADF) return 90;
    const char *mode=getenv("BT_TEST_CASE");
    if (!strcmp(mode,"exit")) return 1;
    if (!strcmp(mode,"hang")) {signal(SIGTERM,SIG_IGN);send(4,"Whanging",8,0);while(1)tick();}
    if (!strcmp(mode,"bad")) {unsigned char invalid[]={ 'R', 9 };send(4,invalid,sizeof(invalid),0);}
    else if (!strcmp(mode,"scan")) {
        unsigned char room[85]={ 'R',1 };
        memcpy(room+2,"22:22:64:D7:F4:74",17);
        memcpy(room+20,"GBA Link F474",13);room[84]=(unsigned char)-48;
        send(4,room,sizeof(room),0);
    } else {
        send(4,"Hready",6,0);write(3,"READY",5);
    }
    while(1)tick();
}
static struct NetplayBTEvent event(char expected) {
    struct NetplayBTEvent e;
    uint64_t deadline=milliseconds()+5000;
    while(milliseconds()<deadline) {
        if (Netplay_btEvent(&e)) {if (e.kind!=expected) fprintf(stderr,"expected %c got %c: %s\n",expected,e.kind,e.text);assert(e.kind==expected);return e;}
        tick();
    }
    assert(!"worker event timeout");return (struct NetplayBTEvent){0};
}
static void stop(void) {
    uint64_t began=milliseconds();Netplay_btStop();
    assert(milliseconds()-began<50);assert(!Netplay_btScanning());
}
int main(int argc,char **argv) {
    if(argc>1)return helper();
    assert(setenv("NEXTUI_BT_HELPER",argv[0],1)==0);
    int marker=open("/dev/null",O_RDONLY);assert(marker>=0);
    int reserved=fcntl(marker,F_DUPFD,50);assert(reserved>=50);close(marker);
    char fd[24];snprintf(fd,sizeof(fd),"%d",reserved);setenv("BT_TEST_MARKER",fd,1);
    assert(!Netplay_btStart(false,"not an address"));
    setenv("BT_TEST_CASE","scan",1);assert(Netplay_btScan());event('R');
    struct NetplayBTRoom rooms[NETPLAY_BT_ROOMS];assert(Netplay_btRooms(rooms)==1);
    assert(!strcmp(rooms[0].address,"22:22:64:D7:F4:74"));assert(!strcmp(rooms[0].label,"GBA Link F474"));assert(rooms[0].rssi==-48);
    assert(!Netplay_btScanFailed());stop();
    setenv("BT_TEST_CASE","bad",1);assert(Netplay_btScan());event('E');assert(Netplay_btScanFailed());stop();
    /* A cancelled, uncooperative old helper cannot attach to the new socket. */
    setenv("BT_TEST_CASE","hang",1);assert(Netplay_btStart(true,NULL));event('W');stop();
    setenv("BT_TEST_CASE","ready",1);assert(Netplay_btStart(false,"22:22:64:D7:F4:74"));
    struct NetplayBTEvent e=event('H');assert(e.fd>=0);
    char data[5];size_t offset=0;uint64_t deadline=milliseconds()+1000;
    while(offset<5&&milliseconds()<deadline) {ssize_t n=read(e.fd,data+offset,5-offset);if(n>0)offset+=(size_t)n;else tick();}
    assert(offset==5&&!memcmp(data,"READY",5));stop();close(e.fd);
    /* Normal exit detection surfaces an error; it never hangs the UI. */
    setenv("BT_TEST_CASE","exit",1);assert(Netplay_btScan());event('E');assert(Netplay_btScanFailed());stop();
    deadline=milliseconds()+3500;
    while(milliseconds()<deadline){Netplay_btReap();tick();}
    errno=0;assert(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD);
    /* Compatibility rejection drains asynchronously, with a bounded lifetime.
     * Its worker survives the immediate call, then receives targeted SIGTERM;
     * the ordinary cancellation checks above retain their immediate behavior. */
    setenv("BT_TEST_CASE","ready",1);assert(Netplay_btStart(true,NULL));
    e=event('H');close(e.fd);
    uint64_t began=milliseconds();Netplay_btDrainStop();assert(milliseconds()-began<50);
    deadline=began+250;
    while(milliseconds()<deadline){Netplay_btReap();tick();}
    assert(waitpid(-1,NULL,WNOHANG)==0);
    deadline=began+1500;
    while(milliseconds()<deadline){Netplay_btReap();tick();}
    errno=0;assert(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD);
    close(reserved);
    puts("PASS: private FDs, room decoding, invalid controls, nonblocking cancellation, retry isolation, exit detection and owned child cleanup");
    return 0;
}

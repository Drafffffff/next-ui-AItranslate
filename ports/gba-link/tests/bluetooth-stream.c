#define _POSIX_C_SOURCE 200809L
#include "ma_netplay_bt.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

/* Synthetic NextUI wire bytes through the actual private-FD production helper.
 * No emulator, ROM, save, or GUI is opened. One complete exchange is outstanding
 * at a time, so an ever-growing application send backlog cannot mask stalls.
 * The controller owns this process and its helper, never a user's game process. */
static volatile sig_atomic_t stopped;
static unsigned data_errors;
static int same_bytes(const void *expected,const void *actual,size_t length) {
    if(!memcmp(expected,actual,length))return 1;
    data_errors++;fprintf(stderr,"DATA_MISMATCH length=%zu\n",length);return 0;
}
static uint64_t clock_us(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static void stop_signal(int sig) {(void)sig;stopped=1;}
static int events(void) {
    struct NetplayBTEvent e;
    while(Netplay_btEvent(&e)) {
        if(e.kind=='E') {fprintf(stderr,"WORKER_ERROR %s\n",e.text);return -1;}
        if(e.kind=='H') return e.fd;
    }
    Netplay_btReap();return -2;
}
static int transfer(int fd,void *data,size_t length,int writing) {
    unsigned char *p=data;uint64_t deadline=clock_us()+5000000;
    while(length&&!stopped&&clock_us()<deadline) {
        ssize_t n=writing?write(fd,p,length):read(fd,p,length);
        if(n>0) {p+=n;length-=(size_t)n;continue;}
        if(n==0|| (errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))return 0;
        if(events()==-1)return 0;
        struct pollfd wait={fd,writing?POLLOUT:POLLIN,0};poll(&wait,1,1);
    }
    return !length;
}
static void put32(unsigned char *p,uint32_t value) {for(int i=3;i>=0;i--){p[i]=(unsigned char)value;value>>=8;}}
static uint32_t get32(const unsigned char *p) {return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void packet(unsigned char *p,unsigned length,uint32_t sequence,int padded) {
    put32(p,1);put32(p+4,length);
    for(unsigned i=0;i<length;i++)p[8+i]=(padded&&i>=24)?0:(unsigned char)((sequence*73+i*29)%255+1);
    put32(p+8,sequence);
}
static int compare(const void *a,const void *b) {uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);}
static int fake_transfer(unsigned char *data,size_t length,int writing) {
    while(length) {
        ssize_t n=writing?write(3,data,length):read(3,data,length);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return 0;
        data+=n;length-=(size_t)n;
    }
    return 1;
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"-role")&&getenv("NP_STREAM_FAKE")) {
        unsigned char buffer[65544];fcntl(3,F_SETFL,0);write(4,"H",1);
        if(!fake_transfer(buffer,160,0)||!fake_transfer(buffer,160,1))return 1;
        while(fake_transfer(buffer,8,0)) {
            unsigned length=get32(buffer+4);if(length>65536)return 1;
            if(!fake_transfer(buffer+8,length,0))return 1;
            if(getenv("NP_STREAM_FAKE_CORRUPT")&&length)buffer[8+length-1]^=1;
            struct timespec pace={0,100000};nanosleep(&pace,NULL);
            if(!fake_transfer(buffer,8+length,1))return 1;
        }
        return 0;
    }
    if(argc!=7) {fprintf(stderr,"usage: stream helper host|join peer seconds payload padded|dense\n");return 2;}
    int host=!strcmp(argv[2],"host"),fd=-1,success=0;
    unsigned duration=(unsigned)strtoul(argv[4],NULL,10),length=(unsigned)strtoul(argv[5],NULL,10);
    int padded=!strcmp(argv[6],"padded");
    if((!host&&strcmp(argv[2],"join"))||duration<1||duration>900||length<8||length>65536||(!padded&&strcmp(argv[6],"dense")))return 2;
    signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);signal(SIGPIPE,SIG_IGN);
    setenv("NEXTUI_BT_HELPER",argv[1],1);setbuf(stdout,NULL);
    uint64_t samples[100000];unsigned count=0;
    unsigned char *out=malloc(length+8),*in=malloc(length+8);if(!out||!in)return 2;
    uint64_t deadline;
    if(!host) {
        if(!Netplay_btScan())goto done;
        uint64_t began_scan=clock_us();deadline=began_scan+12000000;int found=0;
        while(!stopped&&clock_us()<deadline) {
            if(events()==-1)goto done;
            struct NetplayBTRoom rooms[NETPLAY_BT_ROOMS];unsigned count_rooms=Netplay_btRooms(rooms);
            for(unsigned i=0;i<count_rooms;i++)if(!strcmp(rooms[i].address,argv[3]))found=1;
            if(found&&clock_us()-began_scan>=2000000)break;
            struct timespec t={0,1000000};nanosleep(&t,NULL);
        }
        Netplay_btStop();if(!found)goto done;
        printf("DISCOVERED_ROOM %s\n",argv[3]);
    }
    if(!Netplay_btStart(host,host?NULL:argv[3]))goto done;
    deadline=clock_us()+60000000;
    while(fd<0&&!stopped&&clock_us()<deadline) {
        int event=events();if(event==-1)goto done;if(event>=0)fd=event;
        struct timespec t={0,1000000};nanosleep(&t,NULL);
    }
    if(fd<0)goto done;
    unsigned char hello[160]={0},peer[160];memcpy(hello,"SYNTHETIC-WIRE-PROBE-NO-GAME",28);
    if(!transfer(fd,hello,sizeof(hello),1)||!transfer(fd,peer,sizeof(peer),0)||!same_bytes(hello,peer,sizeof(peer)))goto done;
    printf("READY role=%s payload=%u pattern=%s\n",argv[2],length,argv[6]);
    uint64_t began=clock_us(),report=began,maximum=0;
    while(!stopped&&count<100000) {
        if(host) {
            if(count>=10&&clock_us()-began>=(uint64_t)duration*1000000) {
                put32(out,2);put32(out+4,0);
                if(!transfer(fd,out,8,1)||!transfer(fd,in,8,0)||!same_bytes(out,in,8))goto done;
                success=1;break;
            }
            packet(out,length,count,padded);uint64_t start=clock_us();
            if(!transfer(fd,out,length+8,1)||!transfer(fd,in,length+8,0)||!same_bytes(out,in,length+8))goto done;
            samples[count]=clock_us()-start;if(samples[count]>maximum)maximum=samples[count];
        } else {
            if(!transfer(fd,in,8,0))goto done;
            if(get32(in)==2&&get32(in+4)==0) {success=transfer(fd,in,8,1);break;}
            if(get32(in)!=1||get32(in+4)!=length||!transfer(fd,in+8,length,0))goto done;
            packet(out,length,count,padded);if(!same_bytes(out,in,length+8)||!transfer(fd,in,length+8,1))goto done;
        }
        count++;
        if(clock_us()-report>=5000000) {
            printf("PROGRESS count=%u elapsed_ms=%llu max_rtt_us=%llu outstanding_limit=1\n",count,(unsigned long long)((clock_us()-began)/1000),(unsigned long long)maximum);report=clock_us();
        }
    }
    uint64_t elapsed=clock_us()-began;
    uint64_t total=0;unsigned over33=0,over66=0,over100=0,over200=0;
    if(host)for(unsigned i=0;i<count;i++) {
        total+=samples[i];over33+=samples[i]>33334;over66+=samples[i]>66668;
        over100+=samples[i]>100000;over200+=samples[i]>200000;
    }
    if(host&&count)qsort(samples,count,sizeof(samples[0]),compare);
    printf("RESULT passed=%d role=%s exchanges=%u elapsed_ms=%llu p50_us=%llu p95_us=%llu p99_us=%llu max_us=%llu data_errors=%u final_outstanding=%d mean_us=%llu over_33_ms=%u over_66_ms=%u over_100_ms=%u over_200_ms=%u\n",success,argv[2],count,(unsigned long long)(elapsed/1000),host&&count?(unsigned long long)samples[count/2]:0,host&&count?(unsigned long long)samples[count*95/100]:0,host&&count?(unsigned long long)samples[count*99/100]:0,(unsigned long long)maximum,data_errors,success?0:1,host&&count?(unsigned long long)(total/count):0,over33,over66,over100,over200);
done:
    /* A successful write means accepted by the worker's Unix stream, not yet
     * delivered over the radio. Keep the endpoint alive for the terminal echo
     * before cancellation; the opposite result proves that echo arrived. */
    if(success) {
        deadline=clock_us()+500000;
        while(clock_us()<deadline){events();struct timespec t={0,1000000};nanosleep(&t,NULL);}
    }
    if(!success)fprintf(stderr,"FIXTURE_FAILED no complete verified measurement\n");
    if(fd>=0)close(fd);
    Netplay_btStop();
    deadline=clock_us()+3000000;
    while(clock_us()<deadline){Netplay_btReap();struct timespec t={0,1000000};nanosleep(&t,NULL);}
    free(out);free(in);return success?0:1;
}

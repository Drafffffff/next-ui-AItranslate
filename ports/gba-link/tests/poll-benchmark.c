/* Exercise the production core poll callback under an RFU-style empty wait.
 * Count actual recv calls and verify a newly arriving packet is still delivered. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static unsigned long socket_reads;
static ssize_t counted_recv(int fd,void *buf,size_t len,int flags){socket_reads++;return recv(fd,buf,len,flags);}
#define recv counted_recv
#ifndef NP_TRANSPORT_SOURCE
#define NP_TRANSPORT_SOURCE "ma_netplay.c"
#endif
#include NP_TRANSPORT_SOURCE
#undef recv
static retro_netpacket_poll_receive_t receive_poll;
static unsigned delivered;
static void begin(uint16_t id,retro_netpacket_send_t send_fn,retro_netpacket_poll_receive_t poll_fn){(void)id;(void)send_fn;receive_poll=poll_fn;}
static void receive_data(const void *buf,size_t len,uint16_t id){(void)id;if(len==1&&*(const char*)buf=='X')delivered++;}
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
int main(void){
 int sockets[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets))return 2;
 struct retro_netpacket_callback cb={.start=begin,.receive=receive_data,.protocol_version="poll benchmark"};
 if(!Netplay_setInterface(&cb))return 2;
 unsigned char sha[32]={0};Netplay_identity(sha,sha,"rfu");
 np.host=true;np.fd=sockets[0];configure_socket(np.fd);np.last_rx=np.last_ping=np.began=now_ms();
 memcpy(np.peer,np.hello,sizeof(np.peer));np.peer[159]=0;
 start_core();if(!Netplay_active()||!receive_poll)return 2;
 const unsigned loops=1000000;uint64_t start=ns();
 for(unsigned i=0;i<loops;i++)receive_poll();
 uint64_t elapsed=ns()-start;unsigned long empty_reads=socket_reads;
 uint32_t header[2]={htonl(W_DATA),htonl(1)};
 if(write(sockets[1],header,sizeof(header))!=(ssize_t)sizeof(header)||write(sockets[1],"X",1)!=1)return 2;
 uint64_t arrived=ns();
 while(!delivered&&ns()-arrived<5000000)receive_poll();
 uint64_t latency=ns()-arrived;
 printf("{\"poll_calls\":%u,\"socket_reads\":%lu,\"elapsed_ms\":%.3f,\"packet_delivery_us\":%.3f,\"delivered\":%u}\n",loops,empty_reads,elapsed/1e6,latency/1e3,delivered);
 Netplay_stop("Benchmark finished");close(sockets[1]);
 return delivered==1&&latency<5000000?0:1;
}

#define _POSIX_C_SOURCE 200809L
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static unsigned long long us(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000000+t.tv_nsec/1000;}
static void fail(const char *what){perror(what);exit(1);}
static void exact(int fd,void *buf,size_t len,int output){size_t pos=0;while(pos<len){struct pollfd p={fd,output?POLLOUT:POLLIN,0};if(poll(&p,1,10000)!=1){fprintf(stderr,"I/O timeout\n");exit(1);}ssize_t n=output?send(fd,(char*)buf+pos,len-pos,MSG_NOSIGNAL):recv(fd,(char*)buf+pos,len-pos,0);if(n<=0)fail("I/O");pos+=n;}}
int main(int argc,char **argv){
 if(argc<2)return 2;
 int host=!strcmp(argv[1],"host");
 int basic=getenv("NP_BT_BASIC")!=NULL;
 int tcp=getenv("NP_TCP")!=NULL;
 alarm(120);
 int fd=socket(tcp?AF_INET:AF_BLUETOOTH,basic?SOCK_SEQPACKET:SOCK_STREAM,tcp?0:BTPROTO_L2CAP);if(fd<0)fail("socket");
 struct l2cap_options options={0};socklen_t size=sizeof(options);
 if(!tcp){if(getsockopt(fd,SOL_L2CAP,L2CAP_OPTIONS,&options,&size))fail("options");options.mode=basic?L2CAP_MODE_BASIC:L2CAP_MODE_ERTM;options.imtu=1024;if(setsockopt(fd,SOL_L2CAP,L2CAP_OPTIONS,&options,sizeof(options)))fail("ERTM");
 struct bt_security sec={.level=BT_SECURITY_LOW};if(setsockopt(fd,SOL_BLUETOOTH,BT_SECURITY,&sec,sizeof(sec)))fail("security");
 }else{int one=1;if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one)))fail("TCP_NODELAY");if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one)))fail("SO_REUSEADDR");}
 struct sockaddr_l2 addr={.l2_family=AF_BLUETOOTH,.l2_psm=htobs(0x1001)};
 struct sockaddr_in ip={.sin_family=AF_INET,.sin_port=htons(55439)};
 if(host){if(bind(fd,tcp?(void*)&ip:(void*)&addr,tcp?sizeof(ip):sizeof(addr)))fail("bind");if(listen(fd,1))fail("listen");printf("LISTENING PSM 1001 mode=%s\n", tcp ? "tcp" : basic ? "basic" : "ertm");fflush(stdout);struct pollfd p={fd,POLLIN,0};if(poll(&p,1,30000)!=1)return 3;int peer=accept(fd,NULL,NULL);if(peer<0)fail("accept");close(fd);fd=peer;}
 else {if(argc!=3)return 2;if(tcp){if(inet_pton(AF_INET,argv[2],&ip.sin_addr)!=1)return 2;}else{unsigned b[6];char tail; if(sscanf(argv[2],"%2x:%2x:%2x:%2x:%2x:%2x%c",&b[5],&b[4],&b[3],&b[2],&b[1],&b[0],&tail)!=6)return 2;for(int i=0;i<6;i++)addr.l2_bdaddr.b[i]=b[i];}if(connect(fd,tcp?(void*)&ip:(void*)&addr,tcp?sizeof(ip):sizeof(addr)))fail("connect");}
 if(!tcp){size=sizeof(options);if(getsockopt(fd,SOL_L2CAP,L2CAP_OPTIONS,&options,&size))fail("negotiation");printf("CONNECTED mode=%u imtu=%u omtu=%u\n",options.mode,options.imtu,options.omtu);}else{int one=1;if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one)))fail("TCP_NODELAY");puts("CONNECTED TCP");}fflush(stdout);
 unsigned count=getenv("NP_BT_COUNT")?(unsigned)atoi(getenv("NP_BT_COUNT")):300; if(count<10||count>300)return 2;
 unsigned length=getenv("NP_BT_BYTES")?(unsigned)atoi(getenv("NP_BT_BYTES")):256;
 unsigned delay=getenv("NP_BT_DELAY_US")?(unsigned)atoi(getenv("NP_BT_DELAY_US")):10000;
 if(length<8||length>1024||delay>1000000)return 2;
 unsigned long long samples[300];unsigned char data[1024],back[1024];
 for(unsigned i=0;i<count;i++){for(unsigned j=0;j<length;j++)data[j]=(unsigned char)(i+j);unsigned long long start=us();if(host){exact(fd,data,length,1);exact(fd,back,length,0);if(memcmp(data,back,length)){fprintf(stderr,"payload mismatch\n");return 1;}samples[i]=us()-start;}else{exact(fd,back,length,0);if(memcmp(data,back,length))return 1;exact(fd,back,length,1);}struct timespec t={.tv_sec=delay/1000000,.tv_nsec=(delay%1000000)*1000};if(host&&delay)nanosleep(&t,NULL);}
 if(host){unsigned over33=0,over66=0,over200=0;for(unsigned i=0;i<count;i++){over33+=samples[i]>33000;over66+=samples[i]>66000;over200+=samples[i]>200000;}printf("STALLS over33ms=%u over66ms=%u over200ms=%u\n",over33,over66,over200);for(unsigned i=0;i<count;i++)for(unsigned j=i+1;j<count;j++)if(samples[j]<samples[i]){unsigned long long t=samples[i];samples[i]=samples[j];samples[j]=t;}printf("PASS exchanges=%u bytes_each_way=%u rtt_us_min=%llu p50=%llu p95=%llu p99=%llu max=%llu\n",count,count*length,samples[0],samples[count/2],samples[count*95/100],samples[count*99/100],samples[count-1]);}else printf("PASS verified %u bidirectional exchanges\n",count);close(fd);return 0;
}

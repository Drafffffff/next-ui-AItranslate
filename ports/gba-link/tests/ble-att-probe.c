#define _POSIX_C_SOURCE 200809L
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static void fail(const char *s){perror(s);_exit(1);}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 alarm(20);
 unsigned b[6];char tail;
 if(sscanf(argv[1],"%2x:%2x:%2x:%2x:%2x:%2x%c",&b[5],&b[4],&b[3],&b[2],&b[1],&b[0],&tail)!=6)return 2;
 int fd=socket(AF_BLUETOOTH,SOCK_SEQPACKET,BTPROTO_L2CAP);if(fd<0)fail("ATT socket");
 struct bt_security sec={.level=BT_SECURITY_LOW};if(setsockopt(fd,SOL_BLUETOOTH,BT_SECURITY,&sec,sizeof(sec)))fail("security");
 struct sockaddr_l2 local={.l2_family=AF_BLUETOOTH,.l2_cid=htobs(4),.l2_bdaddr_type=BDADDR_LE_PUBLIC};
 if(bind(fd,(void*)&local,sizeof(local)))fail("ATT bind");
 struct sockaddr_l2 remote=local;for(int i=0;i<6;i++)remote.l2_bdaddr.b[i]=b[i];
 if(connect(fd,(void*)&remote,sizeof(remote)))fail("ATT connect");
 puts("RAW_ATT_CONNECTED");fflush(stdout);
 unsigned char request[]={2,5,2},response[1024];
 if(send(fd,request,sizeof(request),MSG_NOSIGNAL)!=(int)sizeof(request))fail("MTU send");
 int n=0;
 for(int attempt=0;attempt<20;attempt++){
  struct pollfd p={fd,POLLIN,0};if(poll(&p,1,5000)!=1){fputs("MTU timeout\n",stderr);return 1;}
  n=recv(fd,response,sizeof(response),0);if(n<=0)fail("ATT receive");
  if(n==3&&response[0]==3)break;
  if(n==3&&response[0]==2){unsigned char mtu[]={3,5,2};if(send(fd,mtu,sizeof(mtu),MSG_NOSIGNAL)!=3)fail("peer MTU response");continue;}
  if(response[0]>=4&&response[0]<=0x20&&(response[0]&1)==0){unsigned char error[]={1,response[0],n>2?response[1]:0,n>2?response[2]:0,0x0a};if(send(fd,error,sizeof(error),MSG_NOSIGNAL)!=5)fail("empty server response");continue;}
  fprintf(stderr,"unexpected ATT opcode=%u len=%d\n",response[0],n);return 1;
 }
 if(n!=3||response[0]!=3){fputs("MTU response missing\n",stderr);return 1;}
 printf("PASS RAW_ATT_MTU=%u\n",response[1]+256*response[2]);close(fd);return 0;
}

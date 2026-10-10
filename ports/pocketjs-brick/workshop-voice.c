/* PocketJS IPC bridge; direct ASR helper owns capture/TLS. Based on Brick Mic's
   asynchronous snapshot/command pattern. No Bluetooth or Mac dependency. */
#define _GNU_SOURCE
#include "workshop-voice.h"
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
extern char **environ;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static int running,started,external;
static pid_t pid;
static char socket_path[104],runtime[80],snapshot[26000];
static char commands[8][12];
static unsigned head,tail;
static int rpc(const char *op,char *out,size_t size){
 int fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return 0;
 struct timeval timeout={0,200000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 struct sockaddr_un addr={0};addr.sun_family=AF_UNIX;snprintf(addr.sun_path,sizeof(addr.sun_path),"%s",socket_path);
 if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))){close(fd);return 0;}
 char line[32];int n=snprintf(line,sizeof(line),"%s\n",op);if(send(fd,line,(size_t)n,0)!=n){close(fd);return 0;}
 size_t used=0;int complete=0;
 while(used+1<size){ssize_t z=recv(fd,out+used,size-used-1,0);if(z<=0)break;used+=(size_t)z;if(memchr(out,'\n',used)){complete=1;break;}}
 out[used]=0;close(fd);return complete&&out[0]=='{';
}
static void publish(const char *s){pthread_mutex_lock(&lock);snprintf(snapshot,sizeof(snapshot),"%s",s);pthread_mutex_unlock(&lock);}
static int start_helper(void){
 if(external)return 1;
 const char *data=getenv("POCKETJS_DATA"),*shared=getenv("SHARED_USERDATA_PATH");if(!shared)shared="/mnt/SDCARD/.userdata/shared";
 char *args[]={"./workshop-voice","--socket",socket_path,"--data",(char*)data,"--settings",(char*)shared,NULL};
 posix_spawnattr_t attr;posix_spawnattr_init(&attr);posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attr,0);
 int rc=posix_spawn(&pid,args[0],NULL,&attr,args,environ);posix_spawnattr_destroy(&attr);return !rc;
}
static void stop_helper(void){
 if(external||pid<=0)return;
 char out[26000];rpc("cancel",out,sizeof(out));kill(-pid,SIGTERM);
 for(int i=0;i<100;i++){if(waitpid(pid,NULL,WNOHANG)==pid){pid=0;return;}struct timespec d={0,10000000};nanosleep(&d,NULL);}
 kill(-pid,SIGKILL);waitpid(pid,NULL,0);pid=0;
}
static void *worker(void *unused){
 (void)unused;int ok=start_helper(),misses=0;
 while(1){
  char op[12]="",out[26000];pthread_mutex_lock(&lock);int go=running;
  if(head!=tail){snprintf(op,sizeof(op),"%s",commands[head%8]);head++;}pthread_mutex_unlock(&lock);if(!go)break;
  if(!strcmp(op,"retry")){stop_helper();ok=start_helper();misses=0;op[0]=0;}
  if(ok&&*op)rpc(op,out,sizeof(out));
  if(ok&&rpc("status",out,sizeof(out))){publish(out);misses=0;}
  else if(!ok||++misses>20)publish("{\"state\":\"error\",\"error\":\"语音服务未就绪，按 A 重试\"}");
  struct timespec delay={0,80000000};nanosleep(&delay,NULL);
 }
 stop_helper();return NULL;
}
static JSValue get_state(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;(void)argc;(void)argv;char copy[26000];pthread_mutex_lock(&lock);snprintf(copy,sizeof(copy),"%s",snapshot);pthread_mutex_unlock(&lock);
 return JS_ParseJSON(ctx,copy,strlen(copy),"workshop-voice");
}
static JSValue command(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;if(!argc)return JS_FALSE;const char *op=JS_ToCString(ctx,argv[0]);if(!op)return JS_EXCEPTION;
 int valid=!strcmp(op,"start")||!strcmp(op,"stop")||!strcmp(op,"cancel")||!strcmp(op,"retry");
 pthread_mutex_lock(&lock);if(valid&&started&&tail-head<8){snprintf(commands[tail%8],12,"%s",op);tail++;}else valid=0;pthread_mutex_unlock(&lock);JS_FreeCString(ctx,op);return JS_NewBool(ctx,valid);
}
int workshop_voice_boot(JSContext *ctx,JSValue info){
 head=tail=0;pid=0;runtime[0]=0;snprintf(snapshot,sizeof(snapshot),"{\"state\":\"idle\"}");
 JSValue voice=JS_NewObject(ctx);JS_SetPropertyStr(ctx,voice,"snapshot",JS_NewCFunction(ctx,get_state,"snapshot",0));JS_SetPropertyStr(ctx,voice,"command",JS_NewCFunction(ctx,command,"command",1));
 if(JS_SetPropertyStr(ctx,info,"voice",voice)<0)return 0;
 const char *socket=getenv("POCKETJS_VOICE_SOCKET");external=socket&&*socket;
 if(external)snprintf(socket_path,sizeof(socket_path),"%s",socket);
 else{
  if(!getenv("POCKETJS_VOICE"))return 1;
  snprintf(runtime,sizeof(runtime),"/tmp/workshop-voice-%ld-XXXXXX",(long)getpid());if(!mkdtemp(runtime))return 0;
  snprintf(socket_path,sizeof(socket_path),"%s/asr.sock",runtime);
 }
 running=1;if(pthread_create(&thread,NULL,worker,NULL)){running=0;return 0;}started=1;return 1;
}
void workshop_voice_shutdown(void){
 pthread_mutex_lock(&lock);running=0;pthread_mutex_unlock(&lock);if(started)pthread_join(thread,NULL);started=0;
 if(*runtime){unlink(socket_path);rmdir(runtime);runtime[0]=0;}
}

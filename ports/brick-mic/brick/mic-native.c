//go:build ignore

#define _GNU_SOURCE
#include "mic-native.h"
#include "mic-power.h"
#include "brick-services.h"
#include "pocketjs_symbian_extension.h"
#include "pocket_ui_cabi.h"
#include "quickjs.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern char **environ;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int worker_started,stopping,presented,preview;
static int paused,pause_requested,restart_requested;
static int deep_requested,deep_paused,connection=-1;
static double connection_at;
static unsigned wake_generation;
static pid_t backend_pid;
static char socket_path[104],runtime_dir[64],font_path[1024];
static char snapshot[65536]="{\"state\":\"bluetooth\",\"connected\":false}";
static char command_error[256];
static char commands[32][96];
static unsigned head,tail;
static uint32_t palette[8]={0,0xffffffff,0x9b2257ff,0x1e2329ff,0xffffffff,0x000000ff,0xffffffff,0x000000ff};
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int read_connection(const char *value){
    /* Only accept the top-level boolean; nested task data must not affect sleep. */
    int depth=0;
    for(const char *p=value;*p;p++){
        if(*p=='{'||*p=='[')depth++;
        else if(*p=='}'||*p==']')depth--;
        else if(*p=='"'){
            const char *key=p++;while(*p&&*p!='"'){if(*p=='\\'&&p[1])p++;p++;}
            if(!*p)break;
            if(depth==1&&p-key==10&&!strncmp(key,"\"connected\"",11)){
                const char *v=p+1;while(*v==' '||*v=='\t'||*v=='\r'||*v=='\n')v++;
                if(*v++!=':')continue;
                while(*v==' '||*v=='\t'||*v=='\r'||*v=='\n')v++;
                if(!strncmp(v,"true",4)&&(v[4]==','||v[4]=='}'||v[4]==' '||v[4]=='\n'||v[4]=='\r'||v[4]=='\t'))return 1;
                if(!strncmp(v,"false",5)&&(v[5]==','||v[5]=='}'||v[5]==' '||v[5]=='\n'||v[5]=='\r'||v[5]=='\t'))return 0;
                return -1;
            }
        }
    }
    return -1;
}
static void publish(const char *value){pthread_mutex_lock(&mutex);snprintf(snapshot,sizeof(snapshot),"%s",value);connection=read_connection(value);connection_at=now();pthread_mutex_unlock(&mutex);}
static void theme(void){
    const char *settings=getenv("BRICK_MIC_SETTINGS"),*res=getenv("RES_PATH"),*fallback=getenv("BRICK_MIC_FONT");
    if(!settings)settings="/mnt/SDCARD/.userdata/shared/minuisettings.txt";
    if(!res)res="/mnt/SDCARD/.system/res";
    snprintf(font_path,sizeof(font_path),"%s",fallback?fallback:"/mnt/SDCARD/.system/res/font1.ttf");
    FILE *f=fopen(settings,"r");if(!f)return;char line[512];
    while(fgets(line,sizeof(line),f)){
        int index;unsigned color;
        if(sscanf(line,"color%d=0x%x",&index,&color)==2&&index>=1&&index<=7){
            const char *p=strchr(line,'=')+1;while(*p==' '||*p=='\t')p++;if(!strncmp(p,"0x",2)||!strncmp(p,"0X",2))p+=2;
            if(strspn(p,"0123456789abcdefABCDEF")<=6)color=(color<<8)|255;
            palette[index]=color;
        }
        if(!strncmp(line,"font=",5)){
            char *name=line+5;name[strcspn(name,"\r\n")]=0;
            if(!strcmp(name,"0"))name="font2.ttf";else if(!strcmp(name,"1"))name="font1.ttf";
            if(*name&&!strchr(name,'/')&&!strstr(name,"..")){
                char path[1024];snprintf(path,sizeof(path),"%s/%s",res,name);
                if(access(path,R_OK)==0)snprintf(font_path,sizeof(font_path),"%s",path);
            }
        }
    }fclose(f);
}
static int rpc(const char *command,char *out,size_t size){
    int fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return 0;
    struct timeval timeout={0,200000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    struct sockaddr_un addr={0};addr.sun_family=AF_UNIX;snprintf(addr.sun_path,sizeof(addr.sun_path),"%s",socket_path);
    if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0){close(fd);return 0;}
    char line[100];int length=snprintf(line,sizeof(line),"%s\n",command);
    if(send(fd,line,(size_t)length,0)!=length){close(fd);return 0;}
    size_t used=0;int complete=0;
    while(used+1<size){ssize_t n=recv(fd,out+used,size-used-1,0);if(n<=0)break;used+=(size_t)n;if(memchr(out,'\n',used)){complete=1;break;}}
    out[used]=0;close(fd);return complete;
}
static int backend_start(int resumed){
    if(preview)return 1;
    posix_spawnattr_t attr;posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attr,0);
    char *args[]={"/bin/sh","./start-services.sh",resumed?"--resume":NULL,NULL};
    int rc=posix_spawn(&backend_pid,args[0],NULL,&attr,args,environ);posix_spawnattr_destroy(&attr);return rc==0;
}
static void restore_link_preferences(void){
    char path[140];int minimum=0,maximum=0;
    snprintf(path,sizeof(path),"%s/link-intervals",runtime_dir);
    FILE *f=fopen(path,"r");if(!f)return;
    int valid=fscanf(f,"%d %d",&minimum,&maximum)==2&&minimum>=6&&maximum>=minimum&&maximum<=3200;fclose(f);
    if(valid){
        const char *names[]={"conn_max_interval","conn_min_interval"};int values[]={maximum,minimum};
        for(int i=0;i<2;i++){
            char node[160];snprintf(node,sizeof(node),"/sys/kernel/debug/bluetooth/hci0/%s",names[i]);
            f=fopen(node,"w");if(f){fprintf(f,"%d\n",values[i]);fclose(f);}
        }
    }
    unlink(path);
}
static void backend_stop(void){
    if(backend_pid<=0)return;
    rpc("cancel",(char[65536]){0},65536);
    /* Allow the daemon to unregister advertising and release its own peer.
       Killing after 400ms could leave a live BLE link across app reentry. */
    kill(-backend_pid,SIGTERM);double until=now()+5.0;
    while(now()<until){if(waitpid(backend_pid,NULL,WNOHANG)==backend_pid)break;struct timespec pause={0,10000000};nanosleep(&pause,NULL);}
    kill(-backend_pid,SIGKILL);waitpid(backend_pid,NULL,0);backend_pid=0;
    restore_link_preferences();
}
static void *run(void *unused){
    (void)unused;
    pthread_mutex_lock(&mutex);
    while(!presented&&!stopping)pthread_cond_wait(&wake,&mutex);
    int stop=stopping;
    pthread_mutex_unlock(&mutex);
    if(stop)return NULL;
    int failed=!backend_start(0),was_ready=0,resumed=0,retries=0,misses=0;
    double started=now(),retry_at=now()+1,status_at=0;
    if(failed)publish("{\"state\":\"error\",\"error\":\"蓝牙启动失败，正在重试\"}");
    for(;;){
        char command[96]="",out[65536];
        pthread_mutex_lock(&mutex);
        int sleep=pause_requested,restart=restart_requested;restart_requested=0;
        if(sleep&&!paused){
            head=tail;
            pthread_mutex_unlock(&mutex);
            rpc("sleep",out,sizeof(out));
            pthread_mutex_lock(&mutex);
            paused=1;pthread_cond_broadcast(&wake);
        }
        while(paused&&pause_requested&&!stopping){
            /* Keep the backend and BLE alive. A small local status read once a
             * second keeps the idle-disconnect timer honest without rendering. */
            if(deep_requested){
                deep_paused=1;pthread_cond_broadcast(&wake);
                while(deep_requested&&pause_requested&&!stopping)pthread_cond_wait(&wake,&mutex);
                deep_paused=0;pthread_cond_broadcast(&wake);
                continue;
            }
            struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec++;
            int rc=pthread_cond_timedwait(&wake,&mutex,&until);
            if(rc==ETIMEDOUT&&pause_requested&&!deep_requested&&!stopping){
                pthread_mutex_unlock(&mutex);
                if(rpc("status",out,sizeof(out))&&out[0]=='{')publish(out);
                else{pthread_mutex_lock(&mutex);connection=-1;connection_at=now();pthread_mutex_unlock(&mutex);}
                pthread_mutex_lock(&mutex);
            }
        }
        int waking=paused;paused=0;stop=stopping;
        if(head!=tail){snprintf(command,sizeof(command),"%s",commands[head%32]);head++;}
        restart|=restart_requested;restart_requested=0;
        pthread_mutex_unlock(&mutex);
        if(stop)break;
        if(waking)rpc("wake",out,sizeof(out));
        if(restart||(failed&&retries<3&&now()>=retry_at)){
            if(restart){resumed=restart!=2;retries=0;}
            else retries++;
            backend_stop();was_ready=misses=0;started=now();
            pthread_mutex_lock(&mutex);command_error[0]=0;head=tail;pthread_mutex_unlock(&mutex);
            command[0]=0;
            publish("{\"state\":\"bluetooth\",\"connected\":false}");
            printf("microphone_backend_restart resumed=%d attempt=%d\n",resumed,retries);
            failed=!backend_start(resumed);
            retry_at=now()+1+retries;
            if(failed)publish("{\"state\":\"error\",\"error\":\"蓝牙服务无法启动\"}");
        }
        if(!failed&&backend_pid>0&&waitpid(backend_pid,NULL,WNOHANG)==backend_pid){
            backend_stop();failed=1;retry_at=now()+1+retries;
            publish("{\"state\":\"error\",\"error\":\"蓝牙服务启动失败，正在重试\"}");
        }
        if(!failed){
            if(*command){
                int ok=rpc(command,out,sizeof(out));pthread_mutex_lock(&mutex);
                command_error[0]=0;
                if(!ok)snprintf(command_error,sizeof(command_error),"语音服务未连接，请稍后重试");
                else if(out[0]!='{'){out[strcspn(out,"\r\n")]=0;snprintf(command_error,sizeof(command_error),"%.250s",out);}
                pthread_mutex_unlock(&mutex);
            }
            if(now()<status_at){struct timespec pause={0,20000000};nanosleep(&pause,NULL);continue;}status_at=now()+0.1;
            if(rpc("ping",out,sizeof(out))&&!strncmp(out,"ready\n",6)){
                was_ready=1;misses=0;retries=0;
                if(rpc("status",out,sizeof(out))&&out[0]=='{')publish(out);
            }else if((was_ready&&++misses>=5)||(!was_ready&&now()-started>20)){
                backend_stop();failed=1;retry_at=now()+1+retries;
                publish("{\"state\":\"error\",\"error\":\"语音服务连接中断，正在重试\"}");
            }else if(!was_ready){
                char path[140],stage[24]="bluetooth";
                snprintf(path,sizeof(path),"%s/stage",runtime_dir);
                FILE *f=fopen(path,"r");
                if(f){if(!fgets(stage,sizeof(stage),f))snprintf(stage,sizeof(stage),"bluetooth");fclose(f);}
                publish(!strncmp(stage,"service",7)?"{\"state\":\"service\",\"connected\":false}":"{\"state\":\"bluetooth\",\"connected\":false}");
            }
        }
        if(failed&&retries>=3)publish("{\"state\":\"error\",\"error\":\"蓝牙服务未能恢复\"}");
        struct timespec pause={0,20000000};nanosleep(&pause,NULL);
    }
    backend_stop();
    return NULL;
}
void brick_mic_first_present(void){pthread_mutex_lock(&mutex);presented=1;pthread_cond_broadcast(&wake);pthread_mutex_unlock(&mutex);}
void brick_mic_sleep(int asleep,int restart){
    pthread_mutex_lock(&mutex);pause_requested=asleep;
    if(!asleep){deep_requested=0;wake_generation++;restart_requested=restart;command_error[0]=0;}
    pthread_cond_broadcast(&wake);
    if(asleep&&worker_started){
        struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=400000000;
        if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
        while(!paused&&!stopping)if(pthread_cond_timedwait(&wake,&mutex,&until)==ETIMEDOUT)break;
    }
    pthread_mutex_unlock(&mutex);
}
int brick_mic_connection(void){
    pthread_mutex_lock(&mutex);int value=now()-connection_at<=3?connection:-1;pthread_mutex_unlock(&mutex);return value;
}
void brick_mic_abort_deep_sleep(void){
    char out[65536];rpc("abort-deep-sleep",out,sizeof(out));
    pthread_mutex_lock(&mutex);deep_requested=0;pthread_cond_broadcast(&wake);pthread_mutex_unlock(&mutex);
}
int brick_mic_prepare_deep_sleep(void){
    pthread_mutex_lock(&mutex);
    if(!pause_requested||!paused||stopping||!worker_started||connection!=0||now()-connection_at>3){pthread_mutex_unlock(&mutex);return 0;}
    deep_requested=1;pthread_cond_broadcast(&wake);
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=400000000;
    if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
    while(!deep_paused&&pause_requested&&!stopping)if(pthread_cond_timedwait(&wake,&mutex,&until)==ETIMEDOUT)break;
    int ok=deep_paused&&pause_requested&&!stopping;
    pthread_mutex_unlock(&mutex);
    char out[65536];
    /* The daemon reserves the disconnected session under its state lock. It
     * rejects a new hello until wake/abort, closing the final reconnect race. */
    ok=ok&&rpc("prepare-deep-sleep",out,sizeof(out))&&!strcmp(out,"asleep-ready\n");
    if(!ok)brick_mic_abort_deep_sleep();
    return ok;
}
static JSValue get_state(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;(void)argc;(void)argv;char copy[65536],error[256];
    pthread_mutex_lock(&mutex);snprintf(copy,sizeof(copy),"%s",snapshot);snprintf(error,sizeof(error),"%s",command_error);pthread_mutex_unlock(&mutex);
    JSValue state=JS_ParseJSON(ctx,copy,strlen(copy),"mic-state");
    if(!JS_IsException(state)){
        pthread_mutex_lock(&mutex);unsigned epoch=wake_generation;pthread_mutex_unlock(&mutex);
        JS_SetPropertyStr(ctx,state,"wakeGeneration",JS_NewUint32(ctx,epoch));
        JS_SetPropertyStr(ctx,state,"fastWake",JS_NewBool(ctx,mic_power_fast_wake()));
        if(*error){JS_SetPropertyStr(ctx,state,"state",JS_NewString(ctx,"error"));JS_SetPropertyStr(ctx,state,"error",JS_NewString(ctx,error));}
        if(mic_power_shutdown_requested()){JS_SetPropertyStr(ctx,state,"state",JS_NewString(ctx,"poweroff"));JS_SetPropertyStr(ctx,state,"error",JS_NewString(ctx,""));}
    }
    return state;
}
static JSValue send_command(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(!argc)return JS_FALSE;const char *command=JS_ToCString(ctx,argv[0]);if(!command)return JS_EXCEPTION;
    int retry=!strcmp(command,"retry");
    int valid=strlen(command)<96&&(retry||!strcmp(command,"start")||!strcmp(command,"stop")||!strcmp(command,"cancel")||(!strncmp(command,"control:",8)||!strncmp(command,"hosts:",6)));
    pthread_mutex_lock(&mutex);
    if(retry){if(restart_requested!=1)restart_requested=2;pthread_cond_broadcast(&wake);}
    else if(valid&&!strcmp(command,"cancel")){head=tail;snprintf(commands[tail%32],96,"%s",command);tail++;}
    else if(valid&&!strcmp(command,"stop")){
        /* Drop queued cursor repeats, but keep a quick tap's pending start. */
        char pending[32][96];unsigned n=0;
        while(head!=tail){const char *item=commands[head%32];if(strncmp(item,"control:",8)&&n<31)snprintf(pending[n++],96,"%s",item);head++;}
        head=tail=0;for(unsigned i=0;i<n;i++)snprintf(commands[tail++%32],96,"%s",pending[i]);
        snprintf(commands[tail++%32],96,"stop");
    }else if(valid&&tail-head<31){snprintf(commands[tail%32],96,"%s",command);tail++;}else valid=0;
    pthread_mutex_unlock(&mutex);
    JS_FreeCString(ctx,command);return JS_NewBool(ctx,valid);
}
static JSValue get_theme(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;(void)argc;(void)argv;JSValue colors=JS_NewArray(ctx);
    for(int i=0;i<8;i++){char hex[10];snprintf(hex,sizeof(hex),"#%06x",palette[i]>>8);JS_SetPropertyUint32(ctx,colors,(uint32_t)i,JS_NewString(ctx,hex));}
    return colors;
}
static void put16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void put32(uint8_t *p,uint32_t n){for(int i=0;i<4;i++)p[i]=n>>(i*8);}
static int compare(const void *a,const void *b){return (int)*(const uint16_t*)a-(int)*(const uint16_t*)b;}
static int atlas(const uint16_t *points,unsigned count,int px,int slot){
    TTF_Font *face=TTF_OpenFont(font_path,px),*fallback=TTF_OpenFont("NotoSansSC-Regular.otf",px);
    if(!face||!fallback){if(face)TTF_CloseFont(face);if(fallback)TTF_CloseFont(fallback);return 0;}
    int base=TTF_FontAscent(face);if(TTF_FontAscent(fallback)>base)base=TTF_FontAscent(fallback);
    int height=TTF_FontHeight(face)+base-TTF_FontAscent(face),other=TTF_FontHeight(fallback)+base-TTF_FontAscent(fallback);
    if(other>height)height=other;
    int width=px*2;unsigned n=count+1;size_t cell=(size_t)width*height,size=16+n*8+n*cell;
    uint8_t *bytes=height<=255&&height>0?calloc(1,size):NULL;int ok=bytes!=NULL;
    if(ok){put32(bytes,0x41464344);put16(bytes+4,3);put16(bytes+6,n);bytes[8]=width;bytes[9]=height;bytes[10]=base;bytes[11]=height;bytes[12]=slot;bytes[14]=1;}
    for(unsigned i=0;ok&&i<count;i++){
        uint16_t cp=points[i];TTF_Font *font=TTF_GlyphIsProvided(face,cp)?face:fallback;
        int minx,maxx,miny,maxy,advance;
        if(!TTF_GlyphIsProvided(font,cp)||TTF_GlyphMetrics(font,cp,&minx,&maxx,&miny,&maxy,&advance)){ok=0;break;}
        SDL_Surface *raw=TTF_RenderGlyph_Blended(font,cp,(SDL_Color){255,255,255,255});
        SDL_Surface *s=raw?SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0):NULL;if(raw)SDL_FreeSurface(raw);
        int offset=base-TTF_FontAscent(font);
        if(!s||s->w>width||s->h+offset>height||advance>255){if(s)SDL_FreeSurface(s);ok=0;break;}
        uint8_t *entry=bytes+16+i*8;put32(entry,cp);put16(entry+4,i+1);entry[6]=advance;entry[7]=minx<0?-minx:0;
        uint8_t *pixels=bytes+16+n*8+(i+1)*cell;
        for(int y=0;y<s->h;y++)for(int x=0;x<s->w;x++){uint32_t p;memcpy(&p,(uint8_t*)s->pixels+y*s->pitch+x*4,4);pixels[(y+offset)*width+x]=p>>24;}
        SDL_FreeSurface(s);
    }
    if(ok){uint8_t *entry=bytes+16+count*8;put32(entry,0xfffd);entry[6]=px;ok=ui_load_font_atlas(bytes,size);}
    free(bytes);TTF_CloseFont(face);TTF_CloseFont(fallback);return ok;
}
static JSValue load_fonts(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(!argc)return JS_FALSE;const char *text=JS_ToCString(ctx,argv[0]);if(!text)return JS_EXCEPTION;
    uint16_t points[512];unsigned count=0;
    for(const unsigned char *p=(const unsigned char*)text;*p&&count<512;){
        uint32_t cp=*p++;if(cp>=0xc0&&cp<0xe0&&p[0]){cp=((cp&31)<<6)|(p[0]&63);p++;}
        else if(cp>=0xe0&&cp<0xf0&&p[0]&&p[1]){cp=((cp&15)<<12)|((p[0]&63)<<6)|(p[1]&63);p+=2;}
        else if(cp>=0x80)continue;
        if(cp<32||cp>=0xfffd)continue;
        int duplicate=0;for(unsigned i=0;i<count;i++)if(points[i]==cp)duplicate=1;
        if(!duplicate)points[count++]=(uint16_t)cp;
    }
    JS_FreeCString(ctx,text);qsort(points,count,sizeof(*points),compare);
    if(!TTF_WasInit()&&TTF_Init())return JS_FALSE;
    int ok=atlas(points,count,30,21)&&atlas(points,count,40,22)&&atlas(points,count,54,23);return JS_NewBool(ctx,ok);
}
static int32_t boot(JSContext *ctx,const uint8_t *pak,size_t size,int32_t w,int32_t h){
    stopping=presented=paused=pause_requested=restart_requested=0;deep_requested=deep_paused=0;connection=-1;connection_at=0;wake_generation=0;head=tail=0;command_error[0]=0;backend_pid=0;
    snprintf(snapshot,sizeof(snapshot),"{\"state\":\"bluetooth\",\"connected\":false}");
    theme();setenv("POCKETJS_FONT",font_path,1);setenv("POCKETJS_HARDWARE","0",1);
    if(!brick_services_boot(ctx,pak,size,w,h))return 0;
    preview=getenv("BRICK_MIC_PREVIEW")!=NULL;
    snprintf(runtime_dir,sizeof(runtime_dir),"/tmp/brick-mic-%ld-XXXXXX",(long)getpid());if(!mkdtemp(runtime_dir))return 0;
    const char *external=getenv("BRICK_MIC_SOCKET");
    if(external&&preview)snprintf(socket_path,sizeof(socket_path),"%s",external);
    else snprintf(socket_path,sizeof(socket_path),"%s/mic.sock",runtime_dir);
    setenv("BRICK_MIC_RUNTIME",runtime_dir,1);setenv("BRICK_MIC_SOCKET",socket_path,1);
    JSValue bridge=JS_NewObject(ctx),global=JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx,bridge,"theme",JS_NewCFunction(ctx,get_theme,"theme",0));
    JS_SetPropertyStr(ctx,bridge,"fonts",JS_NewCFunction(ctx,load_fonts,"fonts",1));
    JS_SetPropertyStr(ctx,bridge,"snapshot",JS_NewCFunction(ctx,get_state,"snapshot",0));
    JS_SetPropertyStr(ctx,bridge,"command",JS_NewCFunction(ctx,send_command,"command",1));
    int ok=JS_SetPropertyStr(ctx,global,"mic",bridge)>=0;JS_FreeValue(ctx,global);
    if(!ok||pthread_create(&worker,NULL,run,NULL))return 0;
    worker_started=1;return 1;
}
static void shutdown_mic(int32_t gl){
    pthread_mutex_lock(&mutex);stopping=1;pthread_cond_broadcast(&wake);pthread_mutex_unlock(&mutex);
    if(worker_started){pthread_join(worker,NULL);worker_started=0;}
    brick_services_shutdown(gl);
    restore_link_preferences();
    if(*runtime_dir){char path[140];snprintf(path,sizeof(path),"%s/stage",runtime_dir);unlink(path);snprintf(path,sizeof(path),"%s/mic.sock",runtime_dir);unlink(path);snprintf(path,sizeof(path),"%s/receiver",runtime_dir);unlink(path);rmdir(runtime_dir);}
}
static const PocketJsSymbianExtensionV1 extension={1,sizeof(PocketJsSymbianExtensionV1),0,boot,shutdown_mic,NULL,NULL,NULL,NULL};
const PocketJsSymbianExtensionV1 *pocketjs_symbian_extension_v1(void){return &extension;}

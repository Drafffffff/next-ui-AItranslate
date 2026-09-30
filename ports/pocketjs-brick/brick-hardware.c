/* Optional capabilities for the creative app; no hardware writes until requested. */
#include "brick-hardware.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static const uint32_t palette[16]={0xff191d28,0xfff1e8ce,0xffe8b54a,0xffd86e3b,0xffb94749,0xff8f5377,0xff5b526f,0xff3c4059,0xff496e85,0xff639caa,0xff9bc6b6,0xff6a9367,0xff385e48,0xffa9b77a,0xffc7ad87,0xff857461};
static pthread_t thread;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static int running,thread_started,enabled,pulse,led_pending,led_owned;
static uint32_t led_color;
static int battery=-1,temperature=-1,motor_owned;
static unsigned char led_snapshot[92];
static size_t led_snapshot_size;
static char effect_snapshot[32],motor_voltage[32];
static int led_error,motor_error;
static int canvas_on,n=32,cursor_x,cursor_y,onion;
static unsigned char pixels[4096],previous[4096];
static uint32_t *composite;
static double work[240],present[240],times[240];
static unsigned frames;
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
static int read_bytes(const char *path,void *buf,size_t len){FILE *f=fopen(path,"rb");if(!f)return -1;size_t z=fread(buf,1,len,f);fclose(f);return (int)z;}
static int write_bytes(const char *path,const void *buf,size_t len){FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(buf,1,len,f)==len;int closed=fclose(f);return ok&&!closed;}
static int read_number(const char *path){char b[32]={0};return read_bytes(path,b,31)>0?atoi(b):-1;}
static void *hardware_loop(void *unused){
    (void)unused;double off=0,last_sample=-1000;
    while(1){
        pthread_mutex_lock(&mutex);int go=running,flash=led_pending,duration=pulse;uint32_t color=led_color;led_pending=0;pulse=0;pthread_mutex_unlock(&mutex);
        if(!go)break;
        double t=now();
        if(enabled && flash){
            if(!led_owned){
                int z=read_bytes("/sys/class/led_anim/frame",led_snapshot,sizeof(led_snapshot));led_snapshot_size=z>0?(size_t)z:0;
                memset(effect_snapshot,0,sizeof(effect_snapshot));read_bytes("/sys/class/led_anim/effect_enable",effect_snapshot,31);
                led_owned=led_snapshot_size==92 && effect_snapshot[0];
            }
            if(led_owned){
                uint32_t colors[23];for(int i=0;i<23;i++)colors[i]=color&0xffffff;
                int ok=write_bytes("/sys/class/led_anim/effect_enable","0",1)&&write_bytes("/sys/class/led_anim/frame",colors,sizeof(colors));
                pthread_mutex_lock(&mutex);led_error=!ok;pthread_mutex_unlock(&mutex);
            }else {pthread_mutex_lock(&mutex);led_error=1;pthread_mutex_unlock(&mutex);}
        }
        if(enabled && duration){
            if(!motor_owned){memset(motor_voltage,0,sizeof(motor_voltage));read_bytes("/sys/class/motor/voltage",motor_voltage,31);motor_owned=1;}
            int ok=write_bytes("/sys/class/motor/voltage","900000",6)&&write_bytes("/sys/class/gpio/gpio227/value","1",1);
            pthread_mutex_lock(&mutex);motor_error=!ok;pthread_mutex_unlock(&mutex);off=t+duration;
        }
        if(off && t>=off){write_bytes("/sys/class/gpio/gpio227/value","0",1);off=0;}
        if(enabled && t-last_sample>=1000){int b=read_number("/sys/class/power_supply/axp2202-battery/capacity");int h=read_number("/sys/class/thermal/thermal_zone0/temp");pthread_mutex_lock(&mutex);battery=b;temperature=h;pthread_mutex_unlock(&mutex);last_sample=t;}
        struct timespec delay={0,10000000};nanosleep(&delay,NULL);
    }
    if(motor_owned){write_bytes("/sys/class/gpio/gpio227/value","0",1);if(motor_voltage[0])write_bytes("/sys/class/motor/voltage",motor_voltage,strlen(motor_voltage));}
    if(led_owned){write_bytes("/sys/class/led_anim/frame",led_snapshot,led_snapshot_size);write_bytes("/sys/class/led_anim/effect_enable",effect_snapshot,strlen(effect_snapshot));}
    return NULL;
}
static int num(JSContext *ctx,JSValueConst obj,const char *name){JSValue v=JS_GetPropertyStr(ctx,obj,name);int32_t x=0;JS_ToInt32(ctx,&x,v);JS_FreeValue(ctx,v);return x;}
static int decode(JSContext *ctx,JSValueConst obj,const char *name,unsigned char *out,int count){
    JSValue v=JS_GetPropertyStr(ctx,obj,name);const char *s=JS_ToCString(ctx,v);int ok=s && (int)strlen(s)==count;
    if(ok)for(int i=0;i<count;i++){char c=s[i];int x=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;if(x<0){ok=0;break;}out[i]=(unsigned char)x;}
    if(s) JS_FreeCString(ctx,s);
    JS_FreeValue(ctx,v);
    return ok;
}
static JSValue set_canvas(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(argc!=1)return JS_ThrowTypeError(ctx,"Expected canvas state");
    if(!num(ctx,argv[0],"visible")){canvas_on=0;return JS_UNDEFINED;}
    int size=num(ctx,argv[0],"size");int x=num(ctx,argv[0],"x"),y=num(ctx,argv[0],"y");
    if((size!=16&&size!=32&&size!=64)||x<0||y<0||x>=size||y>=size)return JS_ThrowTypeError(ctx,"Invalid canvas bounds");
    unsigned char a[4096],b[4096];
    if(!decode(ctx,argv[0],"pixels",a,size*size)||!decode(ctx,argv[0],"previous",b,size*size))return JS_ThrowTypeError(ctx,"Invalid canvas pixels");
    if(!composite){composite=malloc(1024*768*4);if(!composite)return JS_ThrowOutOfMemory(ctx);}
    memcpy(pixels,a,size*size);memcpy(previous,b,size*size);n=size;cursor_x=x;cursor_y=y;onion=num(ctx,argv[0],"onion");canvas_on=1;return JS_UNDEFINED;
}
static JSValue feedback(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;uint32_t color=0;int32_t ms=0;if(argc>0)JS_ToUint32(ctx,&color,argv[0]);if(argc>1)JS_ToInt32(ctx,&ms,argv[1]);
    if(ms<0||ms>60)return JS_ThrowTypeError(ctx,"Feedback pulse must be 0..60 ms");
    pthread_mutex_lock(&mutex);led_color=color;led_pending=1;pulse=ms;pthread_mutex_unlock(&mutex);return JS_UNDEFINED;
}
static int compare(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return x<y?-1:x>y;}
static JSValue metrics(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;(void)argc;(void)argv;JSValue v=JS_NewObject(ctx);unsigned count=frames<240?frames:240;double sorted[240],peak=0,sum=0;
    for(unsigned i=0;i<count;i++){sorted[i]=work[i];if(work[i]>peak)peak=work[i];sum+=present[i];}qsort(sorted,count,sizeof(double),compare);
    JS_SetPropertyStr(ctx,v,"p95",JS_NewFloat64(ctx,count?sorted[(count-1)*95/100]:0));JS_SetPropertyStr(ctx,v,"peak",JS_NewFloat64(ctx,peak));JS_SetPropertyStr(ctx,v,"present",JS_NewFloat64(ctx,count?sum/count:0));
    unsigned first=frames>240?frames%240:0,last=frames?(frames-1)%240:0;double elapsed=count>1?times[last]-times[first]:0;
    JS_SetPropertyStr(ctx,v,"fps",JS_NewFloat64(ctx,elapsed>0?(count-1)*1000/elapsed:0));
    pthread_mutex_lock(&mutex);JS_SetPropertyStr(ctx,v,"battery",JS_NewInt32(ctx,battery));JS_SetPropertyStr(ctx,v,"temperature",JS_NewInt32(ctx,temperature));JS_SetPropertyStr(ctx,v,"hardware",JS_NewBool(ctx,enabled));JS_SetPropertyStr(ctx,v,"ledError",JS_NewBool(ctx,led_error));JS_SetPropertyStr(ctx,v,"motorError",JS_NewBool(ctx,motor_error));pthread_mutex_unlock(&mutex);return v;
}
int brick_hardware_boot(JSContext *ctx,JSValue bridge){
    const char *flag=getenv("POCKETJS_HARDWARE");enabled=flag&&!strcmp(flag,"1");canvas_on=0;frames=0;led_owned=motor_owned=0;led_error=motor_error=0;battery=temperature=-1;led_pending=pulse=0;running=1;
    if(pthread_create(&thread,NULL,hardware_loop,NULL)){free(composite);composite=NULL;running=0;return 0;}thread_started=1;
    JS_SetPropertyStr(ctx,bridge,"canvas",JS_NewCFunction(ctx,set_canvas,"canvas",1));JS_SetPropertyStr(ctx,bridge,"feedback",JS_NewCFunction(ctx,feedback,"feedback",2));JS_SetPropertyStr(ctx,bridge,"metrics",JS_NewCFunction(ctx,metrics,"metrics",0));return 1;
}
void brick_hardware_shutdown(void){pthread_mutex_lock(&mutex);running=0;pthread_mutex_unlock(&mutex);if(thread_started)pthread_join(thread,NULL);thread_started=0;free(composite);composite=NULL;canvas_on=0;}
const uint8_t *brick_canvas_render(const uint8_t *source){
    if(!canvas_on||!source||!composite)return source;
    memcpy(composite,source,1024*768*4);
    int cell=512/n;
    for(int y=0;y<512;y++)for(int x=0;x<512;x++){
        int cx=x/cell,cy=y/cell,index=cy*n+cx;uint32_t c=palette[pixels[index]];
        if(!pixels[index]&&onion&&previous[index]){uint32_t p=palette[previous[index]];c=0xff000000|(((p>>16&255)+25)/2<<16)|(((p>>8&255)+29)/2<<8)|((p&255)+40)/2;}
        if(n<=32&&(x%cell==0||y%cell==0))c=0xff2b3040;
        if(cx==cursor_x&&cy==cursor_y&&(x%cell<2||y%cell<2||x%cell>=cell-2||y%cell>=cell-2))c=0xffffda76;
        composite[(144+y)*1024+40+x]=c;
    }
    return (const uint8_t*)composite;
}
void brick_hardware_frame(double work_ms,double present_ms){unsigned i=frames++%240;work[i]=work_ms;present[i]=present_ms;times[i]=now();}

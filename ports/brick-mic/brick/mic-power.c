//go:build ignore

#define _GNU_SOURCE
#include "mic-power.h"
#include "mic-native.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#ifndef __APPLE__
#include <linux/input.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif
static int sleeping,shutdown_requested,down,preview,screen_timeout=60;
static Uint32 pressed_at,last_activity,woke_at,disconnected_at;
static int disconnect_timer,deep_woke;
#define DEEP_DISCONNECT_MILLISECONDS (5u*60u*1000u)
static char marker[1100];
#ifndef __APPLE__
static int device_fd=-1;
static void *settings;
static void (*settings_quit)(void),(*set_raw_brightness)(int),(*set_brightness)(int);
static int (*get_brightness)(void);
static pid_t monitors[2];
static const char *monitor_names[]={"keymon.elf","audiomon.elf"};
static void monitors_signal(int signal){
    for(int i=0;i<2;i++){
        if(signal==SIGSTOP){char command[80];snprintf(command,sizeof(command),"pidof %s",monitor_names[i]);FILE *f=popen(command,"r");int pid=0;if(f){if(fscanf(f,"%d",&pid)==1)monitors[i]=pid;pclose(f);}}
        if(monitors[i]>0)kill(monitors[i],signal);
    }
}

#endif
void mic_power_init(void){
    sleeping=shutdown_requested=down=disconnect_timer=deep_woke=0;pressed_at=woke_at=disconnected_at=0;
    preview=getenv("BRICK_MIC_PREVIEW")!=NULL;
    const char *path=getenv("BRICK_MIC_RESUME");if(path)snprintf(marker,sizeof(marker),"%s",path);
    const char *cfg=getenv("BRICK_MIC_SETTINGS");FILE *f=cfg?fopen(cfg,"r"):NULL;char line[256];
    if(f){while(fgets(line,sizeof(line),f)){
        int n;if(sscanf(line,"screentimeout=%d",&n)==1&&n>=0&&n<=3600)screen_timeout=n;
    }fclose(f);}
    puts("fast_wake=1 bluetooth_kept_during_sleep=1 disconnected_deep_sleep_seconds=300");
    last_activity=SDL_GetTicks();
#ifndef __APPLE__
    if(preview)return;
    settings=dlopen("libmsettings.so",RTLD_NOW|RTLD_LOCAL);
    if(settings){
        void (*init)(void)=dlsym(settings,"InitSettings");settings_quit=dlsym(settings,"QuitSettings");
        get_brightness=dlsym(settings,"GetBrightness");set_brightness=dlsym(settings,"SetBrightness");set_raw_brightness=dlsym(settings,"SetRawBrightness");
        if(init&&get_brightness&&set_brightness&&set_raw_brightness)init();else{dlclose(settings);settings=NULL;}
    }
    for(int i=0;i<16;i++){
        char path[40],name[128]="";snprintf(path,sizeof(path),"/dev/input/event%d",i);int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
        ioctl(fd,EVIOCGNAME(sizeof(name)),name);if(strstr(name,"axp2202-pek")){device_fd=fd;break;}close(fd);
    }
#endif
}
void mic_power_presented(void){
    if(!*marker||preview)return;
    char temp[1120];snprintf(temp,sizeof(temp),"%s.new",marker);int fd=open(temp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(fd>=0){const char *json="{\"version\":1,\"app\":\"brick-mic\"}\n";int ok=write(fd,json,strlen(json))==(ssize_t)strlen(json)&&fsync(fd)==0;close(fd);if(ok&&rename(temp,marker)==0){char dir[1100];snprintf(dir,sizeof(dir),"%s",marker);char *slash=strrchr(dir,'/');if(slash){*slash=0;fd=open(dir,O_RDONLY|O_DIRECTORY);if(fd>=0){fsync(fd);close(fd);}}}else unlink(temp);}
}
static void wake_screen(int deep){
    Uint32 began=SDL_GetTicks();
    brick_mic_sleep(0,deep);sleeping=0;down=0;disconnect_timer=0;woke_at=last_activity=SDL_GetTicks();
#ifndef __APPLE__
    if(!preview&&settings){set_raw_brightness(8);set_brightness(get_brightness());}
    if(!preview)monitors_signal(SIGCONT);
#endif
    printf("wake_kept_pocketjs_runtime=1 deep=%d wake_screen_ms=%u\n",deep,SDL_GetTicks()-began);
}
void mic_power_toggle(void){
    if(sleeping){wake_screen(0);return;}
#ifndef __APPLE__
    if(!preview&&!settings){fputs("Sleep unavailable: NextUI libmsettings failed to load\n",stderr);return;}
#endif
    brick_mic_sleep(1,0);sleeping=1;disconnect_timer=0;down=0;
#ifndef __APPLE__
    if(!preview){monitors_signal(SIGSTOP);set_raw_brightness(0);sync();}
#endif
    puts("sleeping=1 microphone_cancelled=1 runtime_retained=1");
}
void mic_power_activity(void){last_activity=SDL_GetTicks();}
void mic_power_button(int pressed){
    Uint32 now=SDL_GetTicks();
    if(pressed){if(!down){down=1;pressed_at=now;}return;}
    if(!down)return;
    down=0;
    if(now-woke_at<1000)return;
    if(now-pressed_at>=1000){brick_mic_sleep(1,0);shutdown_requested=1;}else mic_power_toggle();
}
static void read_power_events(void){
#ifndef __APPLE__
    if(device_fd>=0){struct input_event e;while(read(device_fd,&e,sizeof(e))==sizeof(e))if(e.type==EV_KEY&&e.code==KEY_POWER&&e.value<2)mic_power_button(e.value);}
#endif
}
static int platform_suspend_available(void){
#if defined(BRICK_MIC_POWER_TEST)
    return 1;
#elif !defined(__APPLE__)
    if(preview)return 0;
    const char *root=getenv("SYSTEM_PATH");if(!root)root="/mnt/SDCARD/.system/tg5040";
    char path[1200];int n=snprintf(path,sizeof(path),"%s/bin/suspend",root);
    return n>=0&&(size_t)n<sizeof(path)&&access(path,X_OK)==0;
#else
    return 0;
#endif
}
static int platform_suspend(void){
#if defined(BRICK_MIC_POWER_TEST)
    extern int brick_mic_test_platform_suspend(void);
    return brick_mic_test_platform_suspend();
#elif !defined(__APPLE__)
    if(preview)return -1;
    /* Reuse NextUI's service shutdown/restore ownership; writing mem directly
     * would leave BlueZ's UART controller broken on resume. */
    const char *root=getenv("SYSTEM_PATH");if(!root)root="/mnt/SDCARD/.system/tg5040";
    char path[1200];int n=snprintf(path,sizeof(path),"%s/bin/suspend",root);
    if(n<0||(size_t)n>=sizeof(path)||access(path,X_OK))return -1;
    pid_t pid;char *args[]={path,NULL};int rc=posix_spawn(&pid,path,NULL,NULL,args,environ);
    if(rc)return -1;
    int status;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return -1;}
    return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;
#else
    return -1;
#endif
}
static void disconnected_sleep(Uint32 now){
    if(!sleeping||down||shutdown_requested){disconnect_timer=0;return;}
    int connected=brick_mic_connection();
    /* A stale or unavailable backend cannot prove the Mac disconnected. */
    if(connected!=0){disconnect_timer=0;return;}
    if(!disconnect_timer){disconnect_timer=1;disconnected_at=now;return;}
    if(now-disconnected_at<DEEP_DISCONNECT_MILLISECONDS)return;
    disconnected_at=now;
    if(!platform_suspend_available()){puts("deep_sleep_skipped=platform_suspend_unavailable");return;}
    if(!brick_mic_prepare_deep_sleep())return;
    /* Preparation may wait for an in-flight status RPC. Prefer the user's wake
     * or shutdown input if a new edge arrived during that wait. */
    read_power_events();SDL_PumpEvents();
    if(!sleeping||down||shutdown_requested||SDL_HasEvents(SDL_KEYDOWN,SDL_KEYUP)||SDL_HasEvents(SDL_JOYBUTTONDOWN,SDL_JOYBUTTONUP)){
        brick_mic_abort_deep_sleep();disconnect_timer=0;return;
    }
    puts("deep_sleep=1 reason=screen_off_disconnected timeout_seconds=300");
    int rc=platform_suspend();
    printf("deep_sleep_platform_result=%d\n",rc);
    if(rc==0){deep_woke=1;wake_screen(1);}
    else{
        /* The script may have stopped Bluetooth before failing. Reuse its
         * background restore path and show the reconnecting UI. */
        brick_mic_abort_deep_sleep();deep_woke=1;wake_screen(1);
    }
}
void mic_power_poll(void){
    read_power_events();
    Uint32 now=SDL_GetTicks();
    if(down&&now-pressed_at>=1000&&!shutdown_requested){brick_mic_sleep(1,0);shutdown_requested=1;}
    if(!sleeping&&!down&&screen_timeout>0&&now-last_activity>=(Uint32)screen_timeout*1000)mic_power_toggle();
    disconnected_sleep(now);
}
int mic_power_sleeping(void){return sleeping;}
int mic_power_fast_wake(void){return 1;}
int mic_power_take_deep_wake(void){int value=deep_woke;deep_woke=0;return value;}
int mic_power_shutdown_requested(void){return shutdown_requested;}
void mic_power_quit(int preserve){
    if(!preserve&&*marker&&!preview){unlink(marker);sync();}
#ifndef __APPLE__
    if(sleeping&&!preview){if(settings){set_raw_brightness(8);set_brightness(get_brightness());}monitors_signal(SIGCONT);}
    if(device_fd>=0)close(device_fd);
    if(settings){if(settings_quit)settings_quit();dlclose(settings);}
#endif
}

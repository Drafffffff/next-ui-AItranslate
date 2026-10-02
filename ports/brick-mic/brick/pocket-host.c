//go:build ignore

/* PocketJS owns all layout, text and rasterization; SDL only presents pixels. */
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>
#include "pocket_runtime.h"
#include "pocket_spec.h"
#include "mic-native.h"
#include "mic-power.h"
#define WIDTH 1024
#define HEIGHT 768
static volatile sig_atomic_t stopped;
static volatile sig_atomic_t interrupted,sleep_toggle;
static uint32_t buttons,pressed;
static int force_present=1;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
static void stop(int sig){(void)sig;interrupted=1;stopped=1;}
static void toggle(int sig){(void)sig;sleep_toggle=1;}
static void *read_file(const char *path,size_t *length){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
    if(n<1||n>32*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char *data=malloc((size_t)n+1);if(!data){fclose(f);return NULL;}
    if(fread(data,1,(size_t)n,f)!=(size_t)n){free(data);fclose(f);return NULL;}
    fclose(f);data[n]=0;*length=(size_t)n;return data;
}
static uint32_t key(SDL_Keycode k){
    switch(k){case SDLK_UP:return POCKET_BTN_UP;case SDLK_DOWN:return POCKET_BTN_DOWN;case SDLK_LEFT:return POCKET_BTN_LEFT;case SDLK_RIGHT:return POCKET_BTN_RIGHT;
    case SDLK_RETURN:case SDLK_SPACE:return POCKET_BTN_CIRCLE;
    case SDLK_BACKSPACE:case SDLK_b:return POCKET_BTN_CROSS;case SDLK_x:return POCKET_BTN_SQUARE;case SDLK_y:return POCKET_BTN_TRIANGLE;case SDLK_q:return POCKET_BTN_LTRIGGER;case SDLK_e:return POCKET_BTN_RTRIGGER;case SDLK_TAB:return POCKET_BTN_SELECT;case SDLK_s:return POCKET_BTN_START;case SDLK_z:return POCKET_BTN_ZL;case SDLK_c:return POCKET_BTN_ZR;default:return 0;}
}
static void event(const SDL_Event *e){
    if(e->type==SDL_KEYDOWN&&e->key.keysym.sym==SDLK_p&&!e->key.repeat){mic_power_toggle();force_present=1;buttons=pressed=0;return;}
    if((e->type==SDL_JOYBUTTONDOWN||e->type==SDL_JOYBUTTONUP)&&e->jbutton.button==102){mic_power_button(e->type==SDL_JOYBUTTONDOWN);return;}
    /* A wakes this app and the same down edge enters the guest. No second press. */
    if(mic_power_sleeping()){
        int record=(e->type==SDL_KEYDOWN&&!e->key.repeat&&key(e->key.keysym.sym)==POCKET_BTN_CIRCLE)||(e->type==SDL_JOYBUTTONDOWN&&e->jbutton.button==1);
        if(record){mic_power_toggle();force_present=1;buttons=pressed=0;}else if(e->type!=SDL_QUIT)return;
    }
    if(e->type==SDL_KEYDOWN||e->type==SDL_JOYBUTTONDOWN||e->type==SDL_JOYHATMOTION)mic_power_activity();
    if(e->type==SDL_WINDOWEVENT&&(e->window.event==SDL_WINDOWEVENT_EXPOSED||e->window.event==SDL_WINDOWEVENT_SIZE_CHANGED||e->window.event==SDL_WINDOWEVENT_RESTORED))force_present=1;
    if(e->type==SDL_QUIT)stopped=1;
    else if(e->type==SDL_KEYDOWN||e->type==SDL_KEYUP){
        if(e->type==SDL_KEYDOWN&&(e->key.keysym.sym==SDLK_ESCAPE||e->key.keysym.sym==SDLK_m))stopped=1;
        uint32_t bit=key(e->key.keysym.sym);
        if(e->type==SDL_KEYDOWN){buttons|=bit;if(!e->key.repeat)pressed|=bit;}else buttons&=~bit;
    }else if(e->type==SDL_JOYHATMOTION){
        buttons&=~(POCKET_BTN_UP|POCKET_BTN_DOWN|POCKET_BTN_LEFT|POCKET_BTN_RIGHT);
        if(e->jhat.value&SDL_HAT_UP)buttons|=POCKET_BTN_UP;
        if(e->jhat.value&SDL_HAT_DOWN)buttons|=POCKET_BTN_DOWN;
        if(e->jhat.value&SDL_HAT_LEFT)buttons|=POCKET_BTN_LEFT;
        if(e->jhat.value&SDL_HAT_RIGHT)buttons|=POCKET_BTN_RIGHT;
    }else if(e->type==SDL_JOYBUTTONDOWN||e->type==SDL_JOYBUTTONUP){
        static const uint32_t map[]={POCKET_BTN_CROSS,POCKET_BTN_CIRCLE,POCKET_BTN_TRIANGLE,POCKET_BTN_SQUARE,POCKET_BTN_LTRIGGER,POCKET_BTN_RTRIGGER,POCKET_BTN_SELECT,POCKET_BTN_START};
        uint32_t bit=e->jbutton.button<8?map[e->jbutton.button]:e->jbutton.button==9?POCKET_BTN_ZL:e->jbutton.button==10?POCKET_BTN_ZR:0;
        if(e->type==SDL_JOYBUTTONDOWN){buttons|=bit;pressed|=bit;}else buttons&=~bit;
        if(e->type==SDL_JOYBUTTONDOWN&&e->jbutton.button==8)stopped=1;
    }else if(e->type==SDL_WINDOWEVENT&&e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)buttons=pressed=0;
}
static int dump(const uint8_t *pixels,const char *path){
    FILE *f=fopen(path,"wb");if(!f)return 0;fprintf(f,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    unsigned char *rgb=malloc(WIDTH*HEIGHT*3);if(!rgb){fclose(f);return 0;}
    for(int i=0;i<WIDTH*HEIGHT;i++){rgb[i*3]=pixels[i*4+2];rgb[i*3+1]=pixels[i*4+1];rgb[i*3+2]=pixels[i*4];}
    int ok=fwrite(rgb,1,WIDTH*HEIGHT*3,f)==WIDTH*HEIGHT*3;free(rgb);return fclose(f)==0&&ok;
}
int main(int argc,char **argv){
    const char *ui_page_test=NULL;
    int connections_input_test=0;
    int live_reply_test=0,review_input_test=0,reply_input_test=0,navigation_input_test=0,read_input_test=0,aux_input_test=0;
    setvbuf(stdout,NULL,_IOLBF,0);signal(SIGPIPE,SIG_IGN);signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGUSR1,toggle);
    int limit=0,result=1,headless=0,input_test=0,power_test=0,wake_record_test=0,control_input_test=0;const char *last=NULL,*first=NULL;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--connections-input-test"))connections_input_test=1;
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)limit=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--ui-page-test")&&i+1<argc)ui_page_test=argv[++i];
        else if(!strcmp(argv[i],"--headless"))headless=1;
        else if(!strcmp(argv[i],"--input-test"))input_test=1;
        else if(!strcmp(argv[i],"--power-test"))power_test=1;
        else if(!strcmp(argv[i],"--wake-record-test"))wake_record_test=1;
        else if(!strcmp(argv[i],"--control-input-test"))control_input_test=1;
        else if(!strcmp(argv[i],"--review-input-test"))review_input_test=1;
        else if(!strcmp(argv[i],"--reply-input-test"))reply_input_test=1;
        else if(!strcmp(argv[i],"--live-reply-test")){reply_input_test=1;live_reply_test=1;}
        else if(!strcmp(argv[i],"--navigation-input-test"))navigation_input_test=1;
        else if(!strcmp(argv[i],"--read-input-test"))read_input_test=1;
        else if(!strcmp(argv[i],"--aux-input-test"))aux_input_test=1;
        else if(!strcmp(argv[i],"--dump")&&i+1<argc)last=argv[++i];
        else if(!strcmp(argv[i],"--dump-first")&&i+1<argc)first=argv[++i];else return 2;
    }
    double boot=now(),peak=0,total=0;size_t js_size=0,pack_size=0;
    char *base=SDL_GetBasePath();if(base){if(chdir(base))fprintf(stderr,"Cannot enter application resource directory\n");SDL_free(base);}
    char *js=read_file("brick-app.js",&js_size);uint8_t *pack=read_file("brick-app.pak",&pack_size);
    if(!js||!pack){fprintf(stderr,"PocketJS application bundle missing\n");goto done;}
    if(!pocket_runtime_boot(js,js_size,pack,pack_size,WIDTH,HEIGHT))goto done;
    printf("Brick Mic / PocketJS + Solid + QuickJS / build_id=20261002-mic-interaction-r14 upstream=%s guest_boot_ms=%.1f\n",POCKETJS_REV,now()-boot);
    if(SDL_Init((headless?0:SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)|SDL_INIT_TIMER))goto done;
    SDL_Window *window=NULL;SDL_Renderer *renderer=NULL;SDL_Texture *texture=NULL;
    SDL_Joystick **joys=NULL;int njoy=0;unsigned frames=0,changed_frames=0,unchanged_frames=0,settled_changed=0,settled_unchanged=0;const uint8_t *pixels=NULL;
    uint8_t *presented_pixels=malloc(WIDTH*HEIGHT*4);if(!presented_pixels)goto sdl_done;
    if(!headless){
#ifndef __APPLE__
        SDL_ShowCursor(SDL_DISABLE);
#endif
        window=SDL_CreateWindow("Brick Mic · PocketJS",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WIDTH,HEIGHT,SDL_WINDOW_SHOWN|
#ifdef __APPLE__
            SDL_WINDOW_RESIZABLE
#else
            SDL_WINDOW_FULLSCREEN_DESKTOP
#endif
        );
        renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):NULL;
        if(!renderer&&window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        texture=renderer?SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,WIDTH,HEIGHT):NULL;
        if(!texture)goto sdl_done;
        SDL_RenderSetLogicalSize(renderer,WIDTH,HEIGHT);
        njoy=SDL_NumJoysticks();joys=calloc((size_t)njoy,sizeof(*joys));if(njoy&&!joys)goto sdl_done;
        for(int i=0;i<njoy;i++)joys[i]=SDL_JoystickOpen(i);
    }
    mic_power_init();unsigned loops=0;Uint32 shutdown_at=0;
    while(!stopped&&(!limit||frames<(unsigned)limit)){
        double start=now();SDL_Event e;while(SDL_PollEvent(&e))event(&e);if(stopped)break;
        loops++;
        if(sleep_toggle){sleep_toggle=0;mic_power_toggle();force_present=1;buttons=pressed=0;}
        if(power_test&&(loops==90||loops==110)){mic_power_toggle();force_present=1;buttons=pressed=0;}
        if(wake_record_test&&loops==90){mic_power_toggle();force_present=1;buttons=pressed=0;}
        if(wake_record_test&&(loops==110||loops==150)){memset(&e,0,sizeof(e));e.type=loops==110?SDL_JOYBUTTONDOWN:SDL_JOYBUTTONUP;e.jbutton.button=1;event(&e);}
        if(buttons)mic_power_activity();
        mic_power_poll();
        if(mic_power_take_deep_wake()){
            force_present=1;
            /* The power edge that woke the CPU must not become a fresh app
             * command. A only wakes the earlier BLE-preserving screen-off state. */
            buttons=pressed=0;
            SDL_FlushEvents(SDL_KEYDOWN,SDL_KEYUP);
            SDL_FlushEvents(SDL_JOYBUTTONDOWN,SDL_JOYBUTTONUP);
            SDL_FlushEvent(SDL_JOYHATMOTION);
        }
        if(mic_power_shutdown_requested()){if(!shutdown_at)shutdown_at=SDL_GetTicks();if(SDL_GetTicks()-shutdown_at>=180)break;}
        if(mic_power_sleeping()&&!mic_power_shutdown_requested()){buttons=pressed=0;SDL_Delay(20);continue;}
        if(connections_input_test){
            if(frames==60||frames==65||frames==110||frames==115){
                memset(&e,0,sizeof(e));e.type=(frames==60||frames==110)?SDL_KEYDOWN:SDL_KEYUP;
                e.key.keysym.sym=SDLK_q;event(&e);e.key.keysym.sym=SDLK_e;event(&e);
            }
            SDL_Keycode k=frames==80?SDLK_DOWN:frames==90?SDLK_RETURN:frames==140?SDLK_b:0;
            if(k){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=k;event(&e);e.type=SDL_KEYUP;event(&e);}
        }
        if(input_test&&frames==120){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_RETURN;event(&e);e.type=SDL_KEYUP;event(&e);}
        if(input_test&&frames==165){memset(&e,0,sizeof(e));e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=0;event(&e);e.type=SDL_JOYBUTTONUP;event(&e);}
        if(control_input_test){
            if(frames==140||frames==210){memset(&e,0,sizeof(e));e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=frames==140?2:3;event(&e);e.type=SDL_JOYBUTTONUP;event(&e);}
            SDL_Keycode k=frames==120?SDLK_s:frames==150?SDLK_s:frames==160?SDLK_RETURN:frames==197?SDLK_b:frames==200?SDLK_b:frames==220?SDLK_TAB:frames==230?SDLK_DOWN:frames==235?SDLK_DOWN:frames==240?SDLK_RETURN:frames==250?SDLK_s:0;
            if(k){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=k;event(&e);e.type=SDL_KEYUP;event(&e);}
            if(frames==195){memset(&e,0,sizeof(e));e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=2;event(&e);e.type=SDL_JOYBUTTONUP;event(&e);}
            if(frames==175||frames==185){buttons|=POCKET_BTN_LEFT|POCKET_BTN_LTRIGGER;}if(frames==180||frames==190){buttons=0;}
        }
        if(review_input_test){
            SDL_Keycode k=frames==120?SDLK_y:0;
            if(frames==160)k=SDLK_RETURN;
            if(frames==180)k=SDLK_s;
            if(frames==190)k=SDLK_y;
            if(frames==200)k=SDLK_TAB;
            if(frames==240)k=SDLK_RETURN;
            if(k){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=k;event(&e);e.type=SDL_KEYUP;event(&e);}
        }
        if(reply_input_test){
            if(frames==120||frames==140||frames==160){memset(&e,0,sizeof(e));e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=5;event(&e);e.type=SDL_JOYBUTTONUP;event(&e);}
            if(frames==190){buttons=POCKET_BTN_LTRIGGER|POCKET_BTN_LEFT;}if(frames==195)buttons=0;
            if(frames==210){memset(&e,0,sizeof(e));e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=1;event(&e);}
            if(frames==230){memset(&e,0,sizeof(e));e.type=SDL_JOYBUTTONUP;e.jbutton.button=1;event(&e);}
            if(frames==270&&!live_reply_test){memset(&e,0,sizeof(e));e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=3;event(&e);e.type=SDL_JOYBUTTONUP;event(&e);}
        }
        if(navigation_input_test){
            /* Ordinary Y does nothing; only SR enters Codex; subpage shortcuts are inert. */
            SDL_Keycode k=frames==60?SDLK_y:frames==80?SDLK_TAB:frames==90?SDLK_s:frames==100?SDLK_y:frames==110?SDLK_b:
                frames==130?SDLK_s:frames==140?SDLK_y:frames==150?SDLK_s:frames==160?SDLK_TAB:frames==170?SDLK_RETURN:
                frames==210?SDLK_y:frames==220?SDLK_TAB:frames==230?SDLK_s:frames==240?SDLK_b:frames==250?SDLK_s:
                frames==260?SDLK_TAB:frames==270?SDLK_RETURN:frames==300?SDLK_s:frames==310?SDLK_y:frames==320?SDLK_TAB:
                frames==330?SDLK_s:frames==340?SDLK_RETURN:frames==380?SDLK_s:0;
            if(k){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=k;event(&e);e.type=SDL_KEYUP;event(&e);}
        }
        if(read_input_test){
            SDL_Keycode k=frames==60?SDLK_TAB:(frames>=80&&frames<=180&&(frames-80)%20==0)?SDLK_DOWN:
                frames==200?SDLK_RETURN:frames==220?SDLK_RETURN:frames==230?SDLK_s:frames==240?SDLK_y:
                frames==250?SDLK_TAB:frames==260?SDLK_DOWN:frames==280?SDLK_b:0;
            if(k){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=k;event(&e);e.type=SDL_KEYUP;event(&e);}
        }
        if(aux_input_test){
            int joy=frames==60||frames==90||frames==140||frames==180||frames==210||frames==250?9:
                frames==100||frames==110||frames==150||frames==190||frames==220||frames==260?10:-1;
            if(joy>=0){memset(&e,0,sizeof(e));e.type=frames==90||frames==110?SDL_JOYBUTTONUP:SDL_JOYBUTTONDOWN;e.jbutton.button=joy;event(&e);
                if(frames!=60&&frames!=90&&frames!=100&&frames!=110){e.type=SDL_JOYBUTTONUP;event(&e);}}
            SDL_Keycode k=frames==120?SDLK_s:frames==160?SDLK_RETURN:frames==200?SDLK_TAB:frames==230?SDLK_b:frames==270?SDLK_b:0;
            if(k){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=k;event(&e);e.type=SDL_KEYUP;event(&e);}
            if(frames==240||frames==275){memset(&e,0,sizeof(e));e.type=frames==240?SDL_JOYBUTTONDOWN:SDL_JOYBUTTONUP;e.jbutton.button=1;event(&e);}
        }
        if(ui_page_test&&headless&&getenv("BRICK_MIC_PREVIEW")){
            SDL_Keycode k=0;
            if(frames==20&&!strcmp(ui_page_test,"edit"))k=SDLK_TAB;
            if(!strcmp(ui_page_test,"read")){
                if(frames==20)k=SDLK_TAB;
                if(frames>=35&&frames<=60&&(frames-35)%5==0)k=SDLK_DOWN;
                if(frames==75)k=SDLK_RETURN;
            }
            if(frames==20&&(!strcmp(ui_page_test,"hub")||!strcmp(ui_page_test,"tasks")||!strcmp(ui_page_test,"alerts")||!strcmp(ui_page_test,"approval")))k=SDLK_y;
            if(frames==35&&(!strcmp(ui_page_test,"tasks")||!strcmp(ui_page_test,"approval")))k=SDLK_RETURN;
            if(!strcmp(ui_page_test,"alerts")){if(frames==35)k=SDLK_DOWN;if(frames==50)k=SDLK_RETURN;}
            if(k){memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.keysym.sym=k;event(&e);e.type=SDL_KEYUP;event(&e);}
            if(frames==100){settled_changed=changed_frames;settled_unchanged=unchanged_frames;}
        }
        uint32_t mask=buttons|pressed;pressed=0;PocketRuntimeInput input={0};input.buttons=mask;
        if(!pocket_runtime_tick(&input)||(pixels=pocket_runtime_render())==NULL)goto sdl_done;
        if(!strcmp(pocket_runtime_action_name(),"app.exit"))stopped=1;
        if(pocket_runtime_length()!=WIDTH*HEIGHT*4)goto sdl_done;
        double cost=now()-start;if(cost>peak)peak=cost;total+=cost;
        /* SDL software fallback may touch the visible surface while clearing.
         * Do not blank or re-present an unchanged screen on every 60 Hz tick. */
        if(force_present||memcmp(presented_pixels,pixels,WIDTH*HEIGHT*4)){
            if(!headless&&(SDL_UpdateTexture(texture,NULL,pixels,WIDTH*4)||SDL_RenderCopy(renderer,texture,NULL,NULL)))goto sdl_done;
            if(!headless)SDL_RenderPresent(renderer);
            memcpy(presented_pixels,pixels,WIDTH*HEIGHT*4);force_present=0;changed_frames++;
        }else unchanged_frames++;
        if(frames==0){
            printf("first_present_ms=%.1f; bluetooth_start_after_present=1\n",now()-boot);
            if(first&&!dump(pixels,first))goto sdl_done;
            brick_mic_first_present();
            mic_power_presented();
        }
        frames++;double delay=1000.0/60-(now()-start);if(delay>0)SDL_Delay((Uint32)delay);
    }
    if(last&&pixels&&!dump(pixels,last))goto sdl_done;
    result=0;
    if(mic_power_shutdown_requested())result=42;
sdl_done:
    if(result&&result!=42)fprintf(stderr,"SDL: %s\n",SDL_GetError());
    if(joys){for(int i=0;i<njoy;i++)if(joys[i])SDL_JoystickClose(joys[i]);free(joys);}
    free(presented_pixels);
    if(texture)SDL_DestroyTexture(texture);
    if(renderer)SDL_DestroyRenderer(renderer);
    if(window)SDL_DestroyWindow(window);
    SDL_Quit();
    printf("changed_frames=%u unchanged_frames=%u\n",changed_frames,unchanged_frames);
    if(ui_page_test)printf("settled_changed_frames=%u settled_unchanged_frames=%u\n",changed_frames-settled_changed,unchanged_frames-settled_unchanged);
    printf("frames=%u tick_raster_mean_ms=%.2f max_ms=%.2f\n",frames,frames?total/frames:0,peak);
done:
    if(result&&result!=42)fprintf(stderr,"Runtime: %s\n",pocket_runtime_error());
    pocket_runtime_shutdown();mic_power_quit(result==42||interrupted);free(pack);free(js);struct rusage usage;getrusage(RUSAGE_SELF,&usage);
    printf("exit=%d peak_rss_kib=%ld\n",result,
#ifdef __APPLE__
        usage.ru_maxrss/1024
#else
        usage.ru_maxrss
#endif
    );return result;
}

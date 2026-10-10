/* Bounded, isolated first-frame renderer. No window, audio device, input or saves. */
#include "workshop/third-party/libretro.h"
#include "workshop/third-party/lodepng.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned format=RETRO_PIXEL_FORMAT_0RGB1555,width,height,callbacks;
static unsigned char *rgba;
static struct retro_frame_time_callback frame_time;
static bool environment(unsigned command,void *data){
 switch(command){
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:format=*(unsigned*)data;return format<=2;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data="/tmp";return true;
 case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:frame_time=*(struct retro_frame_time_callback*)data;return true;
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned*)data=0;return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
 case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:case RETRO_ENVIRONMENT_SET_VARIABLES:return true;
 default:return false;
 }
}
static void video(const void *data,unsigned w,unsigned h,size_t pitch){
 if(rgba||!data||w<1||h<1||w>256||h>256)return;
 // fake-08 first loads the queued cartridge and starts its coroutine.
 // Those two callbacks precede _draw; callback 3 is the first game frame.
 // Keep intentional black frames instead of guessing from image brightness.
 if(++callbacks<3)return;
 width=w;height=h;rgba=malloc((size_t)w*h*4);if(!rgba)return;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  unsigned char *p=rgba+(y*w+x)*4;const unsigned char *src=(const unsigned char*)data+y*pitch;
  if(format==RETRO_PIXEL_FORMAT_XRGB8888){uint32_t n;memcpy(&n,src+x*4,4);p[0]=n>>16;p[1]=n>>8;p[2]=n;}
  else{uint16_t n;memcpy(&n,src+x*2,2);unsigned r=format==RETRO_PIXEL_FORMAT_RGB565?(n>>11)&31:(n>>10)&31,g=format==RETRO_PIXEL_FORMAT_RGB565?(n>>5)&63:(n>>5)&31;p[0]=r*255/31;p[1]=g*255/(format==RETRO_PIXEL_FORMAT_RGB565?63:31);p[2]=(n&31)*255/31;}p[3]=255;
 }
}
static void audio(int16_t a,int16_t b){(void)a;(void)b;}
static size_t audio_batch(const int16_t *data,size_t frames){(void)data;return frames;}
static void poll_input(void){}
static int16_t input(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 0;}
#define BIND(type,name) type name=(type)dlsym(core,"retro_"#name);if(!name)return 2
typedef void (*VoidFn)(void);
int main(int argc,char **argv){
 if(argc!=4)return 2;
 alarm(3);void *core=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!core)return 2;
 typedef void (*EnvironmentFn)(retro_environment_t);BIND(EnvironmentFn,set_environment);
 typedef void (*VideoFn)(retro_video_refresh_t);BIND(VideoFn,set_video_refresh);
 typedef void (*AudioFn)(retro_audio_sample_t);BIND(AudioFn,set_audio_sample);
 typedef void (*BatchFn)(retro_audio_sample_batch_t);BIND(BatchFn,set_audio_sample_batch);
 typedef void (*PollFn)(retro_input_poll_t);BIND(PollFn,set_input_poll);
 typedef void (*InputFn)(retro_input_state_t);BIND(InputFn,set_input_state);
 BIND(VoidFn,init);BIND(VoidFn,deinit);BIND(VoidFn,run);BIND(VoidFn,unload_game);
 typedef void (*AVFn)(struct retro_system_av_info*);BIND(AVFn,get_system_av_info);
 typedef bool (*LoadFn)(const struct retro_game_info*);BIND(LoadFn,load_game);
 set_environment(environment);set_video_refresh(video);set_audio_sample(audio);set_audio_sample_batch(audio_batch);set_input_poll(poll_input);set_input_state(input);init();
 FILE *f=fopen(argv[2],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);if(size<1||size>8*1024*1024)return 2;void *cart=malloc((size_t)size);if(!cart||fread(cart,1,(size_t)size,f)!=(size_t)size)return 2;fclose(f);
 struct retro_game_info game={argv[2],cart,(size_t)size,NULL};if(!load_game(&game))return 2;
 struct retro_system_av_info av;get_system_av_info(&av);
 for(int i=0;i<4&&!rgba;i++){if(frame_time.callback)frame_time.callback(frame_time.reference);run();}
 int ok=rgba&&!lodepng_encode32_file(argv[3],rgba,width,height);
 unload_game();deinit();free(cart);free(rgba);dlclose(core);return ok?0:1;
}

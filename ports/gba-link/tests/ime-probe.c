/* Exercise the real core with a homebrew ROM and actual timer IRQs. */
#include <libretro.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool environment(unsigned cmd,void *p){
 switch(cmd){
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
 case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)p=".";return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=p;
  if(!strcmp(v->key,"gpsp_drc"))v->value=getenv("NP_DYNAREC")?"enabled":"disabled";
  else if(!strcmp(v->key,"gpsp_boot_mode"))v->value="game";
  else if(!strcmp(v->key,"gpsp_serial"))v->value="disabled";
  else v->value=NULL;
  return v->value!=NULL;}
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)p=false;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return true;
 default:return false;
 }
}
static void video(const void *p,unsigned w,unsigned h,size_t n){(void)p;(void)w;(void)h;(void)n;}
static void audio(int16_t l,int16_t r){(void)l;(void)r;}
static size_t batch(const int16_t *p,size_t n){(void)p;return n;}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned n){(void)p;(void)d;(void)i;(void)n;return 0;}
#define GET(var,name) do{void *sym=dlsym(core,name);if(!sym){fprintf(stderr,"Missing %s\n",name);return 2;}memcpy(&(var),&sym,sizeof(sym));}while(0)
int main(int argc,char**argv){
 if(argc!=3)return 2;
 FILE*f=fopen(argv[2],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);void*rom=malloc((size_t)size);if(!rom||fread(rom,1,(size_t)size,f)!=(size_t)size)return 2;fclose(f);
 void*core=dlopen(argv[1],RTLD_NOW);if(!core){fprintf(stderr,"%s\n",dlerror());return 2;}
 void(*init)(void),(*run)(void),(*unload)(void),(*deinit)(void),(*env)(retro_environment_t),(*vid)(retro_video_refresh_t),(*aud)(retro_audio_sample_t),(*bat)(retro_audio_sample_batch_t),(*pol)(retro_input_poll_t),(*inp)(retro_input_state_t);
 bool(*load)(const struct retro_game_info*);void*(*mem)(unsigned);
 GET(init,"retro_init");GET(run,"retro_run");GET(unload,"retro_unload_game");GET(deinit,"retro_deinit");GET(env,"retro_set_environment");GET(vid,"retro_set_video_refresh");GET(aud,"retro_set_audio_sample");GET(bat,"retro_set_audio_sample_batch");GET(pol,"retro_set_input_poll");GET(inp,"retro_set_input_state");GET(load,"retro_load_game");GET(mem,"retro_get_memory_data");
 env(environment);vid(video);aud(audio);bat(batch);pol(poll);inp(input);init();struct retro_game_info info={.path=argv[2],.data=rom,.size=(size_t)size};if(!load(&info))return 2;
 for(unsigned n=0;n<8;n++)run();
 uint32_t*r=mem(RETRO_MEMORY_SYSTEM_RAM);if(!r)return 2;
 printf("IME probe %s: result=%08x reason=%u read_disabled=%04x pending_if=%04x irq_count=%u\n",getenv("NP_DYNAREC")?"JIT":"interpreter",r[0],r[1],r[2],r[3],r[4]);bool pass=r[0]==0x50415353&&r[2]==0&&r[4]==1;unload();deinit();dlclose(core);free(rom);return pass?0:1;
}

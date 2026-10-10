/* System-font sizes match Brick Mic; atlas code reused from mic-native.c. */
#define _GNU_SOURCE
#include "workshop-visual.h"
#include "pocket_ui_cabi.h"
#include "workshop/third-party/lodepng.h"
#include "workshop/icons/masks.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <errno.h>
static char font_path[1024];
extern char **environ;
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
static int32_t textures[128];
static char texture_ids[128][49];static unsigned texture_count;
static int32_t icons[2],rings[41],qr_texture=-1;static int visual_started;
static int color_value(JSContext *ctx,JSValueConst value,uint32_t *color){
 const char *s=JS_ToCString(ctx,value);if(!s)return 0;
 int valid=strlen(s)==9&&s[0]=='#';char *end=NULL;
 if(valid){*color=(uint32_t)strtoul(s+1,&end,16);valid=*end==0;}
 JS_FreeCString(ctx,s);return valid;
}
#include "workshop-avatar.c"
static JSValue icon(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;uint32_t color;if(argc<2||!color_value(ctx,argv[1],&color))return JS_NewInt32(ctx,-1);
 const char *name=JS_ToCString(ctx,argv[0]);if(!name)return JS_EXCEPTION;
 int index=!strcmp(name,"gamepad-2")?0:!strcmp(name,"folder-plus")?1:-1;JS_FreeCString(ctx,name);
 if(index<0)return JS_NewInt32(ctx,-1);
 if(icons[index]<0){unsigned char pixels[64*64*4];const unsigned char *mask=index?workshop_icon_1:workshop_icon_0;
  for(unsigned i=0;i<4096;i++){pixels[i*4]=color>>24;pixels[i*4+1]=color>>16;pixels[i*4+2]=color>>8;pixels[i*4+3]=(mask[i]*(color&255)+127)/255;}
  icons[index]=ui_upload_texture(pixels,sizeof(pixels),64,64,3);
 }
 return JS_NewInt32(ctx,icons[index]);
}
/* Render one QR texture at an integer module scale, including a four-module
 * quiet zone. QR contrast is fixed for scanning; surrounding UI uses theme. */
static JSValue qr(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;int32_t size;size_t length=0;
 if(argc!=2||JS_ToInt32(ctx,&size,argv[1]))return JS_ThrowTypeError(ctx,"Invalid QR matrix");
 const char *bits=JS_ToCStringLen(ctx,&length,argv[0]);if(!bits)return JS_EXCEPTION;
 if(size==0&&length==0){if(qr_texture>=0)ui_free_texture(qr_texture);qr_texture=-1;JS_FreeCString(ctx,bits);return JS_NewInt32(ctx,-1);}
 if(size<21||size>177||length!=(size_t)size*size){JS_FreeCString(ctx,bits);return JS_ThrowTypeError(ctx,"Invalid QR matrix");}
 for(size_t i=0;i<length;i++)if(bits[i]!='0'&&bits[i]!='1'){JS_FreeCString(ctx,bits);return JS_ThrowTypeError(ctx,"Invalid QR bit");}
 /* PocketJS only accepts power-of-two textures. Display 512px at 384px;
  * scales divisible by four keep modules integral on the screen. */
 int side=512,scale=side/(size+8);if(scale>8)scale=8;if(scale>=4)scale=scale/4*4;int inset=(side-size*scale)/2;
 size_t bytes=(size_t)side*side*4;unsigned char *pixels=malloc(bytes);
 if(!pixels){JS_FreeCString(ctx,bits);return JS_ThrowInternalError(ctx,"QR memory exhausted");}
 memset(pixels,255,bytes);
 for(int y=0;y<size;y++)for(int x=0;x<size;x++)if(bits[y*size+x]=='1')for(int dy=0;dy<scale;dy++)for(int dx=0;dx<scale;dx++){
  size_t at=((size_t)(inset+y*scale+dy)*side+inset+x*scale+dx)*4;pixels[at]=pixels[at+1]=pixels[at+2]=0;
 }
 JS_FreeCString(ctx,bits);int32_t handle=ui_upload_texture(pixels,bytes,side,side,3);free(pixels);
 if(handle<0)return JS_NewInt32(ctx,-1);
 if(qr_texture>=0){ui_free_texture(qr_texture);}
 qr_texture=handle;return JS_NewInt32(ctx,handle);
}
static JSValue monotonic_clock(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;(void)argc;(void)argv;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return JS_NewFloat64(ctx,t.tv_sec*1000.0+t.tv_nsec/1000000.0);
}
static JSValue ring(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;uint32_t color;int32_t step;
 if(argc<2||JS_ToInt32(ctx,&step,argv[0])||step<0||step>40||!color_value(ctx,argv[1],&color))return JS_NewInt32(ctx,-1);
 if(rings[step]<0){unsigned char pixels[64*64*4];
  for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
   double coverage=0;
   for(unsigned sy=0;sy<4;sy++)for(unsigned sx=0;sx<4;sx++){
    double dx=x+(sx+.5)/4-32,dy=y+(sy+.5)/4-32,radius=hypot(dx,dy);
    if(radius<29.6||radius>31.9)continue;
    double angle=atan2(dx,-dy);if(angle<0)angle+=6.283185307179586;
    coverage+=angle<=step*6.283185307179586/40?1:.18;
   }
   unsigned i=(y*64+x)*4;pixels[i]=color>>24;pixels[i+1]=color>>16;pixels[i+2]=color>>8;pixels[i+3]=(unsigned char)(coverage*(color&255)/16);
  }
  rings[step]=ui_upload_texture(pixels,sizeof(pixels),64,64,3);
 }
 return JS_NewInt32(ctx,rings[step]);
}
static int cover_file(const char *id,char *path,size_t size,const char **source){
 const char *root=getenv("POCKETJS_DATA");if(!root)return 0;
 snprintf(path,size,"%s/covers/%s.png",root,id);struct stat st;
 if(!stat(path,&st)&&S_ISREG(st.st_mode)){*source="cover";return 1;}
 snprintf(path,size,"%s/previews/%s.png",root,id);
 if(!stat(path,&st)&&S_ISREG(st.st_mode)){*source="frame";return 1;}
 return 0;
}
static int safe_id(const char *s){if(strncmp(s,"game-",5)||strlen(s)>48)return 0;for(const char *p=s+5;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-'))return 0;return strlen(s)>5;}
/* NextUI resolves art by ROM basename (not map.txt's display title).
 * Mirror the selected custom cover/first frame atomically to that exact path. */
static int menu_cover(const char *id,const char *source){
 const char *roms=getenv("POCKETJS_ROMS");if(!roms||!safe_id(id))return 0;
 unsigned char *bytes=NULL,*pixels=NULL;size_t size=0;unsigned w=0,h=0;
 if(lodepng_load_file(&bytes,&size,source)||size>2*1024*1024){free(bytes);return 0;}
 LodePNGState state;lodepng_state_init(&state);unsigned error=lodepng_inspect(&w,&h,&state,bytes,size);lodepng_state_cleanup(&state);
 if(error||!w||!h||w>512||h>512||lodepng_decode32(&pixels,&w,&h,bytes,size)){free(bytes);free(pixels);return 0;}free(pixels);
 char dir[1100],target[1200],temp[1240];snprintf(dir,sizeof(dir),"%s/.media",roms);
 if(mkdir(dir,0700)&&errno!=EEXIST){free(bytes);return 0;}
 snprintf(target,sizeof(target),"%s/%s.png",dir,id);
 FILE *existing=fopen(target,"rb");int same=0;
 if(existing){unsigned char *old=malloc(size+1);if(old){same=fread(old,1,size+1,existing)==size&&!memcmp(old,bytes,size);free(old);}fclose(existing);}
 if(same){free(bytes);return 1;}
 snprintf(temp,sizeof(temp),"%s/.%s-XXXXXX",dir,id);int fd=mkstemp(temp),ok=fd>=0;size_t offset=0;
 if(ok){while(offset<size){ssize_t n=write(fd,bytes+offset,size-offset);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}offset+=(size_t)n;}
  if(fsync(fd))ok=0;
  if(close(fd))ok=0;
  if(ok&&rename(temp,target))ok=0;
  if(!ok)unlink(temp);else{fd=open(dir,O_RDONLY|O_DIRECTORY);if(fd<0||fsync(fd))ok=0;if(fd>=0)close(fd);}
 }
 free(bytes);return ok;
}
static JSValue image(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;if(!argc)return JS_NULL;const char *id=JS_ToCString(ctx,argv[0]);if(!id)return JS_EXCEPTION;
 char path[1200],key[49];snprintf(key,sizeof(key),"%s",id);const char *source=NULL;int found=safe_id(id)&&cover_file(id,path,sizeof(path),&source);JS_FreeCString(ctx,id);
 unsigned slot=0;while(slot<texture_count&&strcmp(texture_ids[slot],key))slot++;
 if(!found||(slot==texture_count&&texture_count>=128))return JS_NULL;
 unsigned char *pixels=NULL;unsigned w=0,h=0;size_t size=0;unsigned char *file=NULL;
 if(lodepng_load_file(&file,&size,path)||size>2*1024*1024){free(file);return JS_NULL;}
 // Inspect dimensions before decode to keep malicious/accidental images bounded.
 LodePNGState state;lodepng_state_init(&state);unsigned error=lodepng_inspect(&w,&h,&state,file,size);lodepng_state_cleanup(&state);
 if(error||!w||!h||w>512||h>512||lodepng_decode32(&pixels,&w,&h,file,size)){free(file);free(pixels);return JS_NULL;}free(file);
 // Always a 128x128 texture; letterboxing preserves a custom cover's aspect ratio.
 unsigned char *thumb=calloc(128*128,4);if(!thumb){free(pixels);return JS_NULL;}
 unsigned tw=w>=h?128:w*128/h,th=h>=w?128:h*128/w;if(!tw)tw=1;if(!th)th=1;
 for(unsigned y=0;y<th;y++)for(unsigned x=0;x<tw;x++)memcpy(thumb+(((y+(128-th)/2)*128)+x+(128-tw)/2)*4,pixels+((y*h/th)*w+x*w/tw)*4,4);
 free(pixels);int32_t handle=ui_upload_texture(thumb,128*128*4,128,128,3);free(thumb);if(handle<0)return JS_NULL;
 if(slot<texture_count)ui_free_texture(textures[slot]);else texture_count++;
 textures[slot]=handle;snprintf(texture_ids[slot],sizeof(texture_ids[slot]),"%s",key);JSValue result=JS_NewObject(ctx);JS_SetPropertyStr(ctx,result,"handle",JS_NewInt32(ctx,handle));JS_SetPropertyStr(ctx,result,"source",JS_NewString(ctx,source));return result;
}
int workshop_cover_prepare(const char *id,char **output,size_t *length){
 if(!safe_id(id))return 0;
 const char *root=getenv("POCKETJS_DATA"),*roms=getenv("POCKETJS_ROMS"),*core=getenv("POCKETJS_P8_CORE");
 char path[1200],rom[1200],target[1200],dir[1100];const char *source=NULL;
 snprintf(dir,sizeof(dir),"%s/previews",root);mkdir(dir,0700);
 snprintf(target,sizeof(target),"%s/%s.png",dir,id);snprintf(rom,sizeof(rom),"%s/%s.p8",roms,id);
 struct stat current,preview;int stale=stat(target,&preview)||(!stat(rom,&current)&&current.st_mtime>preview.st_mtime);
 snprintf(path,sizeof(path),"%s/covers/%s.png",root,id);struct stat cover;int custom=!stat(path,&cover)&&S_ISREG(cover.st_mode);
 if(!custom&&stale&&core&&*core&&!access("./workshop-cover",X_OK)&&!stat(rom,&current)){
  char temp[1240];snprintf(temp,sizeof(temp),"%s/.%s-%ld.png",dir,id,(long)getpid());
  char *args[]={"./workshop-cover",(char*)core,rom,temp,NULL};posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
  int null=open("/dev/null",O_WRONLY);if(null>=0){posix_spawn_file_actions_adddup2(&actions,null,1);posix_spawn_file_actions_adddup2(&actions,null,2);}
  pid_t child;int rc=posix_spawn(&child,args[0],&actions,NULL,args,environ);posix_spawn_file_actions_destroy(&actions);if(null>=0)close(null);
  if(!rc){int status=0;waitpid(child,&status,0);if(WIFEXITED(status)&&!WEXITSTATUS(status)){int fd=open(temp,O_RDONLY);if(fd>=0){fsync(fd);close(fd);}rename(temp,target);}else unlink(temp);}
 }
 int ready=cover_file(id,path,sizeof(path),&source),menu=ready&&menu_cover(id,path);char json[128];
 snprintf(json,sizeof(json),"{\"source\":\"%s\",\"menu_cover_ready\":%s}",ready?source:"",menu?"true":"false");
 *output=strdup(json);*length=strlen(json);return *output!=NULL;
}
int workshop_visual_boot(JSContext *ctx,JSValue info){
 const char *font=getenv("POCKETJS_FONT");snprintf(font_path,sizeof(font_path),"%s",font?font:"font1.ttf");texture_count=0;
 for(unsigned i=0;i<2;i++)icons[i]=-1;
 for(unsigned i=0;i<41;i++)rings[i]=-1;
 qr_texture=-1;visual_started=1;workshop_avatar_boot(ctx,info);
 JS_SetPropertyStr(ctx,info,"clock",JS_NewCFunction(ctx,monotonic_clock,"clock",0));
 JS_SetPropertyStr(ctx,info,"qr",JS_NewCFunction(ctx,qr,"qr",2));
 JS_SetPropertyStr(ctx,info,"icon",JS_NewCFunction(ctx,icon,"icon",2));JS_SetPropertyStr(ctx,info,"ring",JS_NewCFunction(ctx,ring,"ring",2));
 JS_SetPropertyStr(ctx,info,"fonts",JS_NewCFunction(ctx,load_fonts,"fonts",1));return JS_SetPropertyStr(ctx,info,"image",JS_NewCFunction(ctx,image,"image",1))>=0;
}
void workshop_visual_shutdown(void){if(!visual_started)return;visual_started=0;if(qr_texture>=0)ui_free_texture(qr_texture);qr_texture=-1;workshop_avatar_shutdown();for(unsigned i=0;i<texture_count;i++)ui_free_texture(textures[i]);for(unsigned i=0;i<2;i++)if(icons[i]>=0)ui_free_texture(icons[i]);for(unsigned i=0;i<41;i++)if(rings[i]>=0)ui_free_texture(rings[i]);texture_count=0;}

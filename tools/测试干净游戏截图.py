#!/usr/bin/env python3
"""Extract actual frame/capture/layout functions and test with real SDL2.

python3 tools/测试干净游戏截图.py --source /tmp/clean-capture.c
cc /tmp/clean-capture.c $(pkg-config --cflags --libs sdl2) -o /tmp/clean-capture
/tmp/clean-capture
"""
from pathlib import Path
import argparse, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
def function(text, signature):
    a=text.index(signature);start=text.index('{',a);depth=1;end=start+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[a:end]
video=(root/'workspace/all/minarch/ma_video.c').read_text()
platform=(root/'workspace/all/common/generic_video.c').read_text()
api=(root/'workspace/all/common/api.h').read_text()
renderer=api[api.index('typedef struct GFX_Renderer {'):api.index('} GFX_Renderer;')+len('} GFX_Renderer;')]
state=video[video.index('const void* lastframe = NULL;'):video.index('// ARM NEON SIMD optimization')]
convert=video[video.index('// ARM NEON SIMD optimization'):video.index('void video_refresh_callback(const void* data')]
head=r'''
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LOG_info(...) ((void)0)
enum retro_pixel_format { RETRO_PIXEL_FORMAT_RGB565, RETRO_PIXEL_FORMAT_XRGB8888 };
static enum retro_pixel_format fmt=RETRO_PIXEL_FORMAT_XRGB8888;
static int quit, show_debug, ambient_mode, fast_forward;
static int device_width=8, device_height=8, screenx, screeny, should_rotate;
static void GFX_setAmbientColor(const void*d,unsigned w,unsigned h,size_t p,int m){(void)d;(void)w;(void)h;(void)p;(void)m;}
/* Simulate the production debug HUD painting into the display source. */
static void video_refresh_callback_main(const void*d,unsigned w,unsigned h,size_t p){
    (void)w;(void)h;(void)p;
    if(show_debug)((Uint32*)d)[0]=0xffff00ff;
}
'''
tests=r'''
static void pixel(SDL_Surface*s,int x,int y,int r,int g,int b){
    Uint32 p;memcpy(&p,(unsigned char*)s->pixels+y*s->pitch+x*4,4);
    Uint8 rr,gg,bb,aa;SDL_GetRGBA(p,s->format,&rr,&gg,&bb,&aa);
    if(rr!=r||gg!=g||bb!=b||aa!=255)fprintf(stderr,"(%d,%d) got %d,%d,%d,%d expected %d,%d,%d,255\n",x,y,rr,gg,bb,aa,r,g,b);
    assert(rr==r&&gg==g&&bb==b&&aa==255);
}
int main(void){
    assert(!SDL_Init(0));
    assert(!Video_captureGameFrame(8,8,SDL_PIXELFORMAT_ARGB8888));
    uint32_t raw[12]={0x00ff0000,0x0000ff00,0x000000ff,0x00ffffff,0,0,
                      0x00ff0000,0x0000ff00,0x000000ff,0x00ffffff,0,0};
    renderer=(GFX_Renderer){.src_w=4,.src_h=2,.true_w=4,.true_h=2,.scale=1,.aspect=2};
    show_debug=1;video_refresh_callback(raw,4,2,6*4);
    assert(rgbaData[0]==0xff0000ff && debugData[0]==0xffff00ff);
    SDL_Surface*s=Video_captureGameFrame(8,8,SDL_PIXELFORMAT_ARGB8888);assert(s);
    pixel(s,0,0,0,0,0);pixel(s,0,2,255,0,0);pixel(s,2,2,0,255,0);pixel(s,4,2,0,0,255);pixel(s,7,5,255,255,255);
    /* The returned snapshot is independent of later source/overlay changes. */
    memset(raw,0,sizeof(raw));video_refresh_callback(raw,4,2,6*4);pixel(s,0,2,255,0,0);SDL_FreeSurface(s);
    /* Duplicate frames with changed supplied geometry cannot reallocate/free the last frame. */
    Uint32*previous=rgbaData;video_refresh_callback(NULL,1000,1000,4000);assert(rgbaData==previous && game_frame_w==4 && game_frame_h==2);
    show_debug=0;fmt=RETRO_PIXEL_FORMAT_RGB565;
    uint16_t rgb[8]={0xf800,0x07e0,0x001f,0xffff,0xf800,0x07e0,0x001f,0xffff};
    video_refresh_callback(rgb,4,2,8);
    renderer.src_x=1;renderer.src_w=2;renderer.aspect=1;
    s=Video_captureGameFrame(8,8,SDL_PIXELFORMAT_ABGR8888);assert(s);
    pixel(s,0,0,0,255,0);pixel(s,7,7,0,0,255);SDL_FreeSurface(s);
    renderer.src_x=0;renderer.src_w=4;renderer.aspect=0;
    s=Video_captureGameFrame(8,8,SDL_PIXELFORMAT_ARGB8888);assert(s);
    pixel(s,1,3,0,0,0);pixel(s,2,3,255,0,0);pixel(s,5,4,255,255,255);SDL_FreeSurface(s);
    renderer.aspect=-1;screenx=2;
    s=Video_captureGameFrame(4,4,SDL_PIXELFORMAT_ABGR8888);assert(s);
    pixel(s,0,0,0,0,0);pixel(s,1,0,255,0,0);SDL_FreeSurface(s);
    screenx=0;renderer.src_x=100;assert(!Video_captureGameFrame(8,8,SDL_PIXELFORMAT_ARGB8888));renderer.src_x=0;
    assert(!Video_captureGameFrame(0,8,SDL_PIXELFORMAT_ARGB8888));
    Uint32 start=SDL_GetTicks();
    for(int i=0;i<100;i++){s=Video_captureGameFrame(720,720,SDL_PIXELFORMAT_ARGB8888);assert(s);SDL_FreeSurface(s);}
    printf("PASS: 核心原图与HUD隔离、RGB565/XRGB8888及行距、完整快照、重复帧、裁剪/比例/偏移/缩放、生命周期；100次720截图 %u ms (container)\n",SDL_GetTicks()-start);
    Video_cleanup();assert(!lastframe && !rgbaData && !debugData);assert(!Video_captureGameFrame(8,8,SDL_PIXELFORMAT_ARGB8888));
    SDL_Quit();return 0;
}
'''
source=head+renderer+'\nstatic GFX_Renderer renderer;\n'+state+convert+'\n'+function(platform,'void PLAT_getGameRect(')+'\n'+function(video,'void video_refresh_callback(')+'\n'+function(video,'SDL_Surface* Video_captureGameFrame(')+'\n'+function(video,'void Video_cleanup(')+'\n'+tests
args=argparse.ArgumentParser();args.add_argument('--source',type=Path);opt=args.parse_args()
if opt.source:
    opt.source.write_text(source)
else:
    with tempfile.TemporaryDirectory() as temp:
        p=Path(temp);(p/'test.c').write_text(source)
        flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','sdl2'],text=True))
        subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror',str(p/'test.c'),*flags,'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)

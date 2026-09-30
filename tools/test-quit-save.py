#!/usr/bin/env python3
"""Compile production save/quit functions with controlled core, storage and input."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
def function(path, signature):
    s=(root/path).read_text(); a=s.index(signature); start=s.index('{',a); depth=1; end=start+1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}');end+=1
    return s[a:end]
save=function('workspace/all/minarch/ma_saves.c','int State_write(void)')
quit_fn=function('workspace/all/minarch/ma_menu.c','static int Menu_saveOnQuit(void)')
stubs=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define MAX_PATH 512
#define NOTIFICATION_ACHIEVEMENT 0
#define STATE_FORMAT_SRM 1
#define STATE_FORMAT_SRM_EXTRADOT 2
#define LOG_error(...) ((void)0)
#define sync() ((void)0)
static int hardcore, serial_ok=1, fast_forward=3, state_slot=4, format, io_fail;
static size_t size=4;
static char state_path[MAX_PATH];
static int RA_isHardcoreModeActive(void) {return hardcore;}
static void Notification_push(int a,const char*b,void*c){(void)a;(void)b;(void)c;}
static size_t get_size(void){return size;}
static int serialize(void *p,size_t n){memset(p,'N',n);return serial_ok;}
static struct {size_t(*serialize_size)(void);int(*serialize)(void*,size_t);}core={get_size,serialize};
static void State_getPath(char *p){strcpy(p,state_path);}
#ifdef HAS_SRM
static int CFG_getStateFormat(void){return format;}
static int write_file(const char *p,const void *s,size_t n){FILE*f=fopen(p,"wb");if(!f)return 0;size_t w=fwrite(s,1,io_fail?1:n,f);int c=fclose(f);return !io_fail&&w==n&&!c;}
#define rzipstream_write_file write_file
#define filestream_write_file write_file
#endif
static struct {int slot;} menu={3};
static int save_result=1, saved_slot=-1, key, resets, updated;
static int Menu_saveState(void){saved_slot=menu.slot;state_slot=menu.slot;return save_result;}
static void Menu_updateState(void){updated++;}
#define MODE_MAIN 0
#define MODE_MENU 1
#define BTN_A 1
#define BTN_B 2
#define PADDING 1
#define PILL_SIZE 1
#define SCALE1(x) (x)
typedef struct {int x,y,w,h;} SDL_Rect;
static struct {int w,h;} surface={720,720}, *screen=&surface;
static struct {int medium;} font;
static void GFX_setMode(int x){(void)x;}
static void GFX_startFrame(void){}
static void PAD_poll(void){}
static int PAD_justPressed(int x){return key==x;}
static void GFX_clear(void*p){(void)p;}
static void GFX_blitMessage(int f,char*m,void*s,SDL_Rect*r){(void)f;(void)m;(void)s;(void)r;}
static void GFX_blitButtonGroup(char**p,int a,void*s,int b){(void)p;(void)a;(void)s;(void)b;}
static void GFX_flip(void*s){(void)s;}
static void hdmimon(void){}
static void expect(const char*s){char b[16]={0};FILE*f=fopen(state_path,"rb");assert(f);fread(b,1,sizeof(b)-1,f);fclose(f);assert(!strcmp(b,s));}
'''
main=r'''
int main(int argc,char**argv){
 assert(argc==2);snprintf(state_path,sizeof(state_path),"%s/state",argv[1]);
 FILE*f=fopen(state_path,"wb");assert(f);fputs("OLD",f);fclose(f);
 hardcore=1;assert(!State_write());expect("OLD");hardcore=0;
 size=0;assert(!State_write());expect("OLD");size=4;
 serial_ok=0;assert(!State_write());expect("OLD");serial_ok=1;
 char tmp[MAX_PATH+5];snprintf(tmp,sizeof(tmp),"%s.tmp",state_path);assert(!mkdir(tmp,0700));
 assert(!State_write());expect("OLD");assert(!rmdir(tmp));
#ifdef HAS_SRM
 io_fail=1;assert(!State_write());expect("OLD");io_fail=0;
 for(format=0;format<=2;format++){assert(State_write());expect("NNNN");}
#else
 (void)format;(void)io_fail;
 assert(State_write());expect("NNNN");
#endif
 assert(fast_forward==3 && access(tmp,F_OK));
 assert(Menu_saveOnQuit());assert(saved_slot==8 && menu.slot==3 && state_slot==4 && resets==0);
 save_result=0;key=BTN_B;assert(!Menu_saveOnQuit());assert(menu.slot==3 && state_slot==4);
 key=BTN_A;assert(Menu_saveOnQuit());assert(menu.slot==3 && state_slot==4);
 assert(updated==3);puts("PASS: 自动槽8、手动槽恢复、失败返回/退出、硬核/不支持存档、失败保留旧文件");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(stubs+save+'\n'+quit_fn+main)
    for defines in ([],['-DHAS_SRM']):
        subprocess.run(['clang','-std=gnu99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*defines,str(p/'test.c'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test'),tmp],check=True)

#!/usr/bin/env python3
"""提取实际 C 上下文代码，在临时目录及可控时钟下验证，不访问真实游戏存档。"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'workspace/all/minarch/ma_ai.c').read_text()
context = source[source.index('/* 场景记忆按屏保存'):source.index('/*\n * 提示词：')]
item = source[source.index('typedef struct {\n\tchar  orig[768];'):source.index('/* ------------------------------------------------------------------ 配置读取 */')]
head = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <sys/stat.h>
#define AI_ITEM_LINES 12
#define AI_MAX_ITEMS 32
#include <ctype.h>
static char test_dir[256];
#define AI_TMP_DIR test_dir
static struct { char name[128], path[256], m3u_path[256]; } game = {.name="game-a", .path="/roms/a/game.rom"};
static int ai_c_context = 1;
static void ai_cfg_load(void) {}
static const char* language = "简体中文";
static const char* ai_lang(void) { return language; }
static time_t fake_now = 1000;
static time_t test_time(time_t* out) { if(out) *out=fake_now; return fake_now; }
#define time test_time
'''
tests = r'''
static void item_set(AI_Item* it, const char* orig, const char* zh, const char* kind) {
    memset(it,0,sizeof(*it)); snprintf(it->orig,sizeof(it->orig),"%s",orig);
    snprintf(it->zh,sizeof(it->zh),"%s",zh); snprintf(it->kind,sizeof(it->kind),"%s",kind);
}
static AI_Context ctx;
static AI_Item items[AI_MAX_ITEMS];
static char prefix[AI_CTX_PREFIX];
static void load(void) { ai_ctx_load(&ctx,fake_now); }
int main(int argc,char** argv) {
    assert(argc==2); snprintf(test_dir,sizeof(test_dir),"%s",argv[1]);
    item_set(&items[0],"Who is he?","他是谁？","dialogue");
    item_set(&items[1],"The captain.","是船长。","dialogue");
    ai_ctx_update("他指船长", "Captain=船长; Island=海岛；", items,2);
    load(); assert(ctx.n==1 && ctx.screens[0].n==2 && ctx.nt==2);
    assert(ai_ctx_screen_bytes(&ctx.screens[0],fake_now)>0);
    fake_now=1059; ai_ctx_prefix(prefix,sizeof(prefix));
    assert(strstr(prefix,"59秒前的一屏 · 优先承接") && strstr(prefix,"他是谁"));
    fake_now=1060; ai_ctx_prefix(prefix,sizeof(prefix)); assert(strstr(prefix,"60秒前的一屏 · 弱参考"));
    fake_now=1179; ai_ctx_prefix(prefix,sizeof(prefix)); assert(strstr(prefix,"179秒前"));
    fake_now=1180; ai_ctx_prefix(prefix,sizeof(prefix));
    assert(!strstr(prefix,"他是谁") && !strstr(prefix,"他指船长") && strstr(prefix,"Captain=船长"));
    load(); assert(ctx.n==0 && !ctx.summary[0] && ctx.nt==2);
    fake_now=1200; ai_ctx_update("新的场景", "captain=船主;Chest=宝箱;Broken=不完整", items,2);
    fake_now=1210; ai_ctx_update("新的场景", "", items,2); load();
    assert(ctx.n==1 && ctx.screens[0].at==1210 && ctx.screens[0].n==2);
    assert(ctx.nt==3 && !strcmp(ctx.terms[0].z,"船长"));
    /* 相邻画面保留一条重复台词和新台词，旧屏只保留不重复部分。 */
    item_set(&items[0],"The captain.","船长。","dialogue");
    item_set(&items[1],"Go find him.","去找他。","dialogue");
    fake_now=1220; ai_ctx_update("找船长", "", items,2); load();
    assert(ctx.n==2 && ctx.screens[0].n==1 && ctx.screens[1].n==2);
    assert(!strcmp(ctx.screens[0].pairs[0].o,"Who is he?"));
    item_set(&items[0],"CONTINUE","继续游戏","menu");
    fake_now=1250; ai_ctx_update("菜单不应覆盖剧情", "Sword=剑;", items,1); load();
    assert(ctx.n==2 && ctx.summary_at==1220 && !strcmp(ctx.summary,"找船长"));
    fake_now=1310; ai_c_context=0; ai_ctx_prefix(prefix,sizeof(prefix)); assert(!prefix[0]);
    ai_ctx_update("不应存储","Bad=坏;",items,1); ai_c_context=1; load(); assert(ctx.nt==4);
    strcpy(game.path,"/roms/b/game.rom"); ai_ctx_prefix(prefix,sizeof(prefix)); assert(!prefix[0]);
    strcpy(game.path,"/roms/a/game.rom"); language="日本語"; ai_ctx_prefix(prefix,sizeof(prefix)); assert(!prefix[0]); language="简体中文";
    strcpy(game.m3u_path,"/roms/a/discs.m3u");
    item_set(&items[0],"Same story","同一故事","dialogue"); ai_ctx_update("剧情","",items,1);
    strcpy(game.path,"/roms/a/disc2.rom"); ai_ctx_prefix(prefix,sizeof(prefix)); assert(strstr(prefix,"同一故事"));
    game.m3u_path[0]=0;strcpy(game.path,"/roms/a/game.rom");
    assert(ai_ctx_reset_scene());load();assert(ctx.n==0 && !ctx.summary[0] && ctx.nt==4);
    char cut[8]; ai_ctx_copy(cut,sizeof(cut),"船长正在说话"); assert(!strcmp(cut,"船长"));
    ai_ctx_copy(cut,sizeof(cut),"a\tb\nc"); assert(!strcmp(cut,"a b c"));
    ai_ctx_copy(cut,sizeof(cut),"x\xe8\x88");assert(!strcmp(cut,"x"));
    /* 整屏淘汰；最新整屏必须优先保留、屏内顺序不变。 */
    for(int i=0;i<8;i++) {
        char o[64],z[256];memset(z,'x',sizeof(z)-1);z[sizeof(z)-1]=0;
        snprintf(o,sizeof(o),"screen-%02d-A",i);item_set(&items[0],o,z,"dialogue");
        snprintf(o,sizeof(o),"screen-%02d-B",i);item_set(&items[1],o,z,"dialogue");
        fake_now=1400+i;ai_ctx_update("当前摘要","",items,2);
    }
    load();assert(ctx.n==6 && !strcmp(ctx.screens[0].pairs[0].o,"screen-02-A"));
    assert(AI_CTX_PREFIX == 4096);
    ai_ctx_prefix(prefix,AI_CTX_PREFIX);
    assert(strlen(prefix)<4096 && strstr(prefix,"screen-07-A") && strstr(prefix,"screen-07-B"));
    ai_ctx_prefix(prefix,1150);
    assert(strstr(prefix,"screen-07-A") && strstr(prefix,"screen-07-B") && !strstr(prefix,"screen-06-A"));
    assert(strstr(prefix,"screen-07-A") < strstr(prefix,"screen-07-B"));
    for(int cap=1;cap<2200;cap++) {
        memset(prefix,'!',sizeof(prefix));ai_ctx_prefix(prefix,cap);
        assert(prefix[cap]=='!' && memchr(prefix,0,cap));
        for(int i=2;i<8;i++) { char a[64],b[64];snprintf(a,sizeof(a),"screen-%02d-A",i);snprintf(b,sizeof(b),"screen-%02d-B",i);
            assert((strstr(prefix,a)!=NULL)==(strstr(prefix,b)!=NULL)); }
    }
    /* 一屏大于预算时不截掉半屏，只使用摘要和术语。 */
    assert(ai_ctx_reset_scene());
    for(int i=0;i<AI_MAX_ITEMS;i++) {
        char o[768],z[768];memset(o,'a',767);o[767]=0;memset(z,'z',767);z[767]=0;o[0]='A'+i%26;
        item_set(&items[i],o,z,"dialogue");
    }
    fake_now=1450;ai_ctx_update("超长一屏摘要","",items,AI_MAX_ITEMS);ai_ctx_prefix(prefix,sizeof(prefix));
    assert(strstr(prefix,"超长一屏摘要") && !strstr(prefix,"zzzzzz"));
    /* 设备时钟回拨清空短期记录并落盘，之后校时也不能复活。 */
    fake_now=1440;ai_ctx_prefix(prefix,sizeof(prefix));assert(!strstr(prefix,"超长一屏摘要"));
    fake_now=1460;load();assert(ctx.n==0 && !ctx.summary[0] && ctx.nt==4);
    item_set(&items[0],"New dialogue","新对白","dialogue");fake_now=1500;ai_ctx_update("新摘要","",items,1);
    fake_now=1680;ai_ctx_prefix(prefix,sizeof(prefix));load();assert(ctx.n==0 && !ctx.summary[0] && ctx.nt==4);
    /* 无 kind 的旧响应可用；新对白缺少摘要时不会沿用旧指代。 */
    fake_now=1700;ai_ctx_update("旧指代","",items,1);
    item_set(&items[0],"Next","下一句","");fake_now=1710;ai_ctx_update("","",items,1);load();assert(ctx.n==2 && !ctx.summary[0]);
    /* 截断文件只丢不完整一屏，不能越界或凭空补齐。 */
    FILE* f=fopen(ai_ctx_path(),"a");assert(f);fprintf(f,"F\t1710\t2\nP\tunfinished\t不完整\n");fclose(f);
    load();assert(ctx.n==2 && ctx.dirty);
    /* 原子写失败不能毁掉现有上下文，也不会假报清空成功。 */
    char temp[540];snprintf(temp,sizeof(temp),"%s.tmp",ai_ctx_path());assert(!mkdir(temp,0700));
    assert(!ai_ctx_reset_scene());assert(!rmdir(temp));load();assert(ctx.n==2 && ctx.nt==4);
    puts("PASS: 按屏去重、整屏预算、60/180秒边界、菜单不续期、术语增量合并、手动重置、时钟回拨、语言/ROM/多碟隔离、UTF-8、损坏文件、写入失败");
}
'''

with tempfile.TemporaryDirectory(prefix='nextui-context-') as directory:
    folder = Path(directory)
    cfile, binary = folder/'test.c', folder/'test'
    cfile.write_text(head+item+context+tests)
    subprocess.run([os.environ.get('CC','clang'),str(cfile),'-o',str(binary),'-Wall','-Werror','-fsanitize=address,undefined','-g'],check=True)
    subprocess.run([str(binary),str(folder)],check=True)

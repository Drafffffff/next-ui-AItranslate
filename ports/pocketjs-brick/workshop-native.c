#define _GNU_SOURCE
#include "workshop-native.h"
#include "workshop-voice.h"
#include "workshop-visual.h"
#include "../../workspace/all/common/config.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

static char root[1024], roms[1024];
static int enabled, runner_ready;
static int valid_id(const char *id) {
    size_t n = strlen(id);
    if (n < 6 || n > 48 || strncmp(id, "game-", 5)) return 0;
    for (size_t i = 5; i < n; i++) if (!isalnum((unsigned char)id[i]) && id[i] != '-') return 0;
    return 1;
}
int workshop_allowed_file(const char *name) {
    if (!enabled) return 0;
    if (!strcmp(name, "last-project.txt") || !strcmp(name, "launch-error.txt")) return 1;
    unsigned prefix=!strncmp(name,"projects/",9)?9:!strncmp(name,"project-info/",13)?13:!strncmp(name,"history/",8)?8:0;
    if(!prefix)return 0;
    char id[64]; size_t n = strlen(name + prefix);
    if (n < 6 || n >= 96 || (prefix!=8&&n>=sizeof(id)) || strcmp(name + strlen(name) - 5, ".json")) return 0;
    if(prefix==8){
        if(n<31)return 0;
        const char *suffix=name+prefix+n-30;
        if(*suffix!='-')return 0;
        for(int i=1;i<=24;i++)if(!isxdigit((unsigned char)suffix[i]))return 0;
        size_t base=(size_t)(suffix-(name+prefix));if(base>=sizeof(id))return 0;
        memcpy(id,name+prefix,base);id[base]=0;
    }else{memcpy(id, name + prefix, n - 5); id[n - 5] = 0;}
    return valid_id(id);
}
/* Hidden temporary files never become menu entries. The directory fsync also
   protects the rename across an abrupt power loss. */
static int atomic_file(const char *dir, const char *name, const char *text) {
    char path[1200], temp[1200];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path) ||
        snprintf(temp, sizeof(temp), "%s/.workshop-XXXXXX", dir) >= (int)sizeof(temp)) return 0;
    int fd = mkstemp(temp); if (fd < 0) return 0;
    fchmod(fd, 0600); size_t size = strlen(text), offset = 0; int ok = 1;
    while (offset < size) {
        ssize_t n = write(fd, text + offset, size - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = 0; break; }
        offset += (size_t)n;
    }
    if (fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (ok && rename(temp, path)) ok = 0;
    if (!ok) unlink(temp);
    if (ok) { fd = open(dir, O_RDONLY | O_DIRECTORY); if (fd < 0 || fsync(fd)) ok = 0; if (fd >= 0) close(fd); }
    return ok;
}
static int regular_file(const char *path) {
    struct stat st; return !lstat(path, &st) && S_ISREG(st.st_mode);
}
static int publish(const char *id, const char *cart, const char *title) {
    const char *header = "pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\n";
    if (strncmp(cart, header, strlen(header)) ||
        !strstr(cart, "\n__gfx__\n")) return 0;
    char name[64], path[1200]; snprintf(name, sizeof(name), "%s.p8", id);
    snprintf(path,sizeof(path),"%s/%s",roms,name);
    FILE *old=fopen(path,"rb");int same=0;
    if(old){size_t size=strlen(cart);char *bytes=malloc(size+1);if(bytes){size_t n=fread(bytes,1,size+1,old);same=n==size&&!memcmp(bytes,cart,size);free(bytes);}fclose(old);}
    if(!same){
        if (!atomic_file(roms, name, cart)) return 0;
        snprintf(path,sizeof(path),"%s/previews/%s.png",root,id);unlink(path);
        snprintf(path,sizeof(path),"%s/.media/%s.png",roms,id);unlink(path);
    }
    snprintf(path, sizeof(path), "%s/map.txt", roms);
    FILE *f = fopen(path, "r");
    char *map = calloc(1, 262144); if (!map) { if (f) fclose(f); return 0; }
    char line[512]; size_t used = 0; int ok = 1;
    while (f && fgets(line, sizeof(line), f)) {
        if (!strncmp(line, name, strlen(name)) && line[strlen(name)] == '\t') continue;
        size_t n = strlen(line); if (used + n + 256 >= 262144) { ok = 0; break; }
        memcpy(map + used, line, n); used += n;
    }
    if (f) { if (ferror(f)) ok = 0; fclose(f); }
    if (used && map[used-1] != '\n') map[used++] = '\n';
    if (ok) { snprintf(map + used, 262144 - used, "%s\t%s\n", name, title); ok = atomic_file(roms, "map.txt", map); }
    free(map);
    if(ok){char *cover=NULL;size_t length=0;workshop_cover_prepare(id,&cover,&length);free(cover);}
    return ok;
}
static int erase_file(const char *dir,const char *name){
    char path[1200];if(snprintf(path,sizeof(path),"%s/%s",dir,name)>=(int)sizeof(path))return 0;
    if(unlink(path)){return errno==ENOENT;}
    int fd=open(dir,O_RDONLY|O_DIRECTORY);if(fd<0)return 0;int ok=!fsync(fd);close(fd);return ok;
}
static int clear_pointer(const char *name,const char *id){
    char path[1200],value[96]={0};snprintf(path,sizeof(path),"%s/%s",root,name);
    FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT;
    size_t n=fread(value,1,sizeof(value)-1,f);int ok=!ferror(f);fclose(f);if(!ok)return 0;
    while(n&&isspace((unsigned char)value[n-1]))value[--n]=0;
    return strcmp(value,id)||erase_file(root,name);
}
static int finish_delete(const char *id){
    char name[64],path[1200],dir[1100];snprintf(name,sizeof(name),"%s.p8",id);
    snprintf(path,sizeof(path),"%s/map.txt",roms);FILE *f=fopen(path,"r");
    if(f){
        char *map=calloc(1,262144);if(!map){fclose(f);return 0;}char line[512];size_t used=0;int ok=1;
        while(fgets(line,sizeof(line),f)){
            if(!strncmp(line,name,strlen(name))&&line[strlen(name)]=='\t')continue;
            size_t n=strlen(line);if(used+n>=262144){ok=0;break;}memcpy(map+used,line,n);used+=n;
        }
        if(ferror(f))ok=0;
        fclose(f);
        if(ok)ok=atomic_file(roms,"map.txt",map);
        free(map);if(!ok)return 0;
    }else if(errno!=ENOENT)return 0;
    if(!erase_file(roms,name))return 0;
    snprintf(name,sizeof(name),"%s.png",id);
    snprintf(dir,sizeof(dir),"%s/previews",root);if(!erase_file(dir,name))return 0;
    snprintf(dir,sizeof(dir),"%s/covers",root);if(!erase_file(dir,name))return 0;
    snprintf(dir,sizeof(dir),"%s/.media",roms);if(!erase_file(dir,name))return 0;
    if(!clear_pointer("last-project.txt",id)||!clear_pointer("play-request.txt",id))return 0;
    snprintf(dir,sizeof(dir),"%s/projects",root);snprintf(name,sizeof(name),"%s.json",id);if(!erase_file(dir,name))return 0;
    snprintf(dir,sizeof(dir),"%s/project-info",root);if(!erase_file(dir,name))return 0;
    snprintf(dir,sizeof(dir),"%s/history",root);DIR *history=opendir(dir);
    if(history){struct dirent *entry;int ok=1;size_t len=strlen(id);
        while((entry=readdir(history)))if(!strncmp(entry->d_name,id,len)&&entry->d_name[len]=='-'){
            char allowed[sizeof(entry->d_name)+8];snprintf(allowed,sizeof(allowed),"history/%s",entry->d_name);
            if(workshop_allowed_file(allowed)&&!erase_file(dir,entry->d_name)){ok=0;break;}
        }
        closedir(history);if(!ok)return 0;
    }else if(errno!=ENOENT)return 0;
    snprintf(dir,sizeof(dir),"%s/deletions",root);return erase_file(dir,id);
}
static int recover_deletions(void){
    char path[1100];snprintf(path,sizeof(path),"%s/deletions",root);DIR *d=opendir(path);if(!d)return errno==ENOENT;
    int ok=1;struct dirent *entry;while((entry=readdir(d))){
        char marker[1200];snprintf(marker,sizeof(marker),"%s/%.48s",path,entry->d_name);
        if(valid_id(entry->d_name)&&regular_file(marker)&&!finish_delete(entry->d_name)){ok=0;break;}
    }
    closedir(d);return ok;
}
static int delete_project(const char *id){
    char dir[1100];snprintf(dir,sizeof(dir),"%s/deletions",root);if(mkdir(dir,0700)&&errno!=EEXIST)return 0;
    /* Persist user intent first. An interrupted deletion is completed on boot. */
    return atomic_file(dir,id,"delete\n")&&finish_delete(id);
}
int workshop_job(const char *op, const char *id, const char *text, const char *title,
                 char **output, size_t *length, char *error, size_t error_size) {
    if (!enabled) { snprintf(error, error_size, "工坊服务未启用"); return 0; }
    if (!strcmp(op, "projects")) {
        char dir[1100]; snprintf(dir, sizeof(dir), "%s/projects", root);
        DIR *d = opendir(dir); if (!d) goto disk_error;
        char *result = calloc(1, 65536); if (!result) { closedir(d); goto disk_error; }
        size_t used = 1; result[0] = '['; struct dirent *item;
        while ((item = readdir(d))) {
            char name[128]; snprintf(name, sizeof(name), "projects/%.100s", item->d_name);
            if (!workshop_allowed_file(name)) continue;
            if (used + strlen(item->d_name) + 6 >= 65536) { free(result); closedir(d); goto disk_error; }
            used += (size_t)snprintf(result + used, 65536 - used, "%s\"%s\"", used > 1 ? "," : "", item->d_name);
        }
        closedir(d); result[used++] = ']'; result[used] = 0; *output = result; *length = used; return 1;
    }
    if (!valid_id(id)) { snprintf(error, error_size, "项目编号无效"); return 0; }
    if (!strcmp(op,"cover")) return workshop_cover_prepare(id,output,length);
    if (!strcmp(op,"delete")) {
        if(delete_project(id))return 1;
        snprintf(error,error_size,"删除尚未完成，请检查 SD 卡后重试");return 0;
    }
    if (!strcmp(op, "publish")) {
        if (!title || !*title || strlen(title) > 96 || strpbrk(title, "\t\r\n") || title[0] == '.') {
            snprintf(error, error_size, "游戏名称无效"); return 0;
        }
        if (!publish(id, text, title)) goto disk_error;
        return 1;
    }
    if (!strcmp(op, "play")) {
        if (!runner_ready) { snprintf(error, error_size, "请先安装完整的 P8 运行器"); return 0; }
        char path[1200]; snprintf(path, sizeof(path), "%s/%s.p8", roms, id);
        if (!regular_file(path)) { snprintf(error, error_size, "请先生成并保存游戏"); return 0; }
        if (!atomic_file(root, "play-request.txt", id)) goto disk_error;
        return 1;
    }
    snprintf(error, error_size, "未知工坊操作"); return 0;
disk_error:
    snprintf(error, error_size, "保存游戏失败，请检查 SD 卡空间与目录权限"); return 0;
}
static void setting(char *out, size_t size, const char *file, const char *key) {
    FILE *f = fopen(file, "r"); if (!f) return;
    char line[1024]; size_t n = strlen(key);
    while (fgets(line, sizeof(line), f)) if (!strncmp(line, key, n) && line[n] == '=') {
        char *value = line + n + 1; value[strcspn(value, "\r\n")] = 0;
        snprintf(out, size, "%s", value);
    }
    fclose(f);
}
void workshop_configure_font(void){
    if(!getenv("POCKETJS_WORKSHOP"))return;
    const char *shared=getenv("SHARED_USERDATA_PATH"),*sd=getenv("SDCARD_PATH");
    if(!shared)shared="/mnt/SDCARD/.userdata/shared";
    if(!sd)sd="/mnt/SDCARD";
    char file[1200],name[128]="",path[1200];snprintf(file,sizeof(file),"%s/minuisettings.txt",shared);setting(name,sizeof(name),file,"font");
    if(!strcmp(name,"0"))snprintf(name,sizeof(name),"font2.ttf");else if(!strcmp(name,"1"))snprintf(name,sizeof(name),"font1.ttf");
    if(*name&&!strchr(name,'/')&&!strstr(name,"..")){snprintf(path,sizeof(path),"%s/.system/res/%s",sd,name);if(!access(path,R_OK))setenv("POCKETJS_FONT",path,1);}
}
int workshop_boot(JSContext *ctx, JSValue bridge) {
    enabled = getenv("POCKETJS_WORKSHOP") != NULL; if (!enabled) return 1;
    const char *data = getenv("POCKETJS_DATA"), *games = getenv("POCKETJS_ROMS");
    if (!data || !games || strlen(data) > 1000 || strlen(games) > 1000) return 0;
    snprintf(root, sizeof(root), "%s", data); snprintf(roms, sizeof(roms), "%s", games);
    char path[1200]; snprintf(path, sizeof(path), "%s/projects", root);
    if (mkdir(path, 0700) && errno != EEXIST) return 0;
    snprintf(path,sizeof(path),"%s/project-info",root);if(mkdir(path,0700)&&errno!=EEXIST)return 0;
    snprintf(path,sizeof(path),"%s/history",root);if(mkdir(path,0700)&&errno!=EEXIST)return 0;
    if (mkdir(roms, 0755) && errno != EEXIST) return 0;
    if (!recover_deletions()) return 0;
    JSValue info = JS_NewObject(ctx), theme = JS_NewObject(ctx);
    const char *shared = getenv("SHARED_USERDATA_PATH"); if (!shared) shared = "/mnt/SDCARD/.userdata/shared";
    snprintf(path, sizeof(path), "%s/minuisettings.txt", shared);
    const uint32_t defaults[] = {CFG_DEFAULT_COLOR1,CFG_DEFAULT_COLOR2,CFG_DEFAULT_COLOR3,CFG_DEFAULT_COLOR4,CFG_DEFAULT_COLOR5,CFG_DEFAULT_COLOR6,CFG_DEFAULT_COLOR7};
    const char *names[] = {"main", "accent", "accent2", "listText", "selectedText", "hint", "background"};
    for (int i = 0; i < 7; i++) {
        char key[16], value[128] = "", color[16]; snprintf(key, sizeof(key), "color%d", i + 1); setting(value, sizeof(value), path, key);
        uint32_t rgba = defaults[i];
        if (*value) {
            const char *p = value; while (*p == ' ' || *p == '\t') p++;
            if (*p == '"') p++;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
            size_t digits = 0; while (isxdigit((unsigned char)p[digits])) digits++;
            if (digits > 0 && digits <= 8) { rgba = (uint32_t)strtoul(p, NULL, 16); if (digits <= 6) rgba = (rgba << 8) | 255; }
        }
        snprintf(color, sizeof(color), "#%08x", rgba); JS_SetPropertyStr(ctx, theme, names[i], JS_NewString(ctx, color));
    }
    JS_SetPropertyStr(ctx, info, "theme", theme);
    const char *runner = getenv("POCKETJS_P8_LAUNCH");
    runner_ready = runner && !access(runner, X_OK);
    JS_SetPropertyStr(ctx, info, "runnerReady", JS_NewBool(ctx, runner_ready));
    JS_SetPropertyStr(ctx, info, "romDirectory", JS_NewString(ctx, roms));
    char key[512] = ""; snprintf(path, sizeof(path), "%s/ai-translate.txt", shared); setting(key, sizeof(key), path, "aiDeepseekKey");
    char voice_key[512] = ""; snprintf(path, sizeof(path), "%s/voice-ai.txt", shared); setting(voice_key, sizeof(voice_key), path, "voiceChatKey");
    JS_SetPropertyStr(ctx, info, "existingKey", JS_NewString(ctx, *voice_key ? voice_key : key));
    JS_SetPropertyStr(ctx, info, "resume", JS_NewBool(ctx, getenv("POCKETJS_WORKSHOP_RESUME") != NULL));
    if (!workshop_visual_boot(ctx,info) || !workshop_voice_boot(ctx,info)) { JS_FreeValue(ctx,info); return 0; }
    return JS_SetPropertyStr(ctx, bridge, "workshop", info) >= 0;
}

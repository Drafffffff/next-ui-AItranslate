#define _GNU_SOURCE
#include "brick-services.h"
#include "brick-hardware.h"
#include "../../workspace/all/common/ai_credentials.h"
#include "workshop-native.h"
#include "workshop-voice.h"
#include "workshop-visual.h"
#include "workshop-sha256.h"
#include "pocketjs_symbian_extension.h"
#include "pocket_ui_cabi.h"
#include "quickjs.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <pthread.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <ctype.h>

extern char **environ;
#define JOBS 8
#define MAX_BODY (1024 * 1024)
#define MAX_PROJECT (16 * 1024 * 1024)
#define MAX_REQUEST (8 * 1024 * 1024)
#define MAX_WORKSHOP_RESPONSE (16 * 1024 * 1024)
#define MAX_STREAM_PENDING (256 * 1024)
#define MAX_STREAM_PROGRESS (64 * 1024)
#define MAX_TEXT (64 * 1024)
#define MAX_GLYPHS 8192
#define DYNAMIC_SLOT 20
static unsigned dynamic_px=36;
#define DYNAMIC_PX dynamic_px

enum { EMPTY, QUEUED, RUNNING, DONE, ASSEMBLING };
enum { READ, WRITE, TEXT, HTTP, CREDENTIALS, PROJECTS, PUBLISH, PLAY, COVER, DELETE_PROJECT, SYNTAX, TERMINAL, SYSTEM_READ };
typedef struct {
    int state, type, id, cancelled, timeout_ms, http_status;
    int page_lines, reverse, first_char, last_char, streaming, stream_kind, wrap_width;
    size_t stream_offset,input_size,input_capacity;
    pid_t pid;
    char name[96], label[128], url[2048], method[8];
    char auth[512];
    char *input, *output;
    size_t output_size,received_size;
    char error[192];
    uint8_t *atlas;
    size_t atlas_size;
    char **lines;
    int line_count;
    int line_first[16],line_last[16];
} Job;
static Job jobs[JOBS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static pthread_t threads[2];
static int stopping, started, next_id;
static char data_root[1024], font_path[1024];
static TTF_Font *font, *fallback;
static unsigned baseline;
static uint32_t glyphs[MAX_GLYPHS];
static unsigned glyph_count;
static uint8_t *glyph_pixels[MAX_GLYPHS];
static uint8_t glyph_advances[MAX_GLYPHS], glyph_offsets[MAX_GLYPHS];
static uint16_t glyph_lookup[65536];
static atomic_uint uploaded_glyphs;
static atomic_uint text_jobs,atlas_builds,atlas_uploads;
static atomic_size_t http_received_bytes,http_reclaimed_bytes,http_peak_buffer_bytes,http_max_progress_bytes;
static double frame_max_ms;
void brick_services_frame(double ms){if(ms>frame_max_ms)frame_max_ms=ms;}
static unsigned cell_width, cell_height;

static void fail(Job *j, const char *message) { snprintf(j->error, sizeof(j->error), "%s", message); }
static int cancelled(Job *j) {
    pthread_mutex_lock(&lock); int yes = j->cancelled || stopping; pthread_mutex_unlock(&lock); return yes;
}
static double millis(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
static void release(Job *j) {
    free(j->input); free(j->output); free(j->atlas);
    for (int i = 0; i < j->line_count; i++) free(j->lines[i]);
    free(j->lines); memset(j, 0, sizeof(*j));
}
static int allowed_file(const char *name) {
    return workshop_allowed_file(name) || !strcmp(name, "config.json") || !strcmp(name, "history.json") || !strcmp(name, "prompt.txt") || !strcmp(name, "pixel-project.json") || !strcmp(name, "pixel-export.svg");
}
static void disk(Job *j) {
    char path[1200], temp[1240]; snprintf(path, sizeof(path), "%s/%s", data_root, j->name);
    if (j->type == READ) {
        FILE *f = fopen(path, "rb");
        if (!f) { if (errno == ENOENT) j->output = strdup(""); else fail(j, "读取文件失败"); return; }
        struct stat st;size_t limit=(!strncmp(j->name,"projects/",9)||!strncmp(j->name,"history/",8))?MAX_PROJECT:MAX_BODY;
        if(fstat(fileno(f),&st)||st.st_size<0||(size_t)st.st_size>limit){fclose(f);fail(j,"文件过大或读取失败");return;}
        size_t capacity=(size_t)st.st_size;
        j->output = malloc(capacity + 1);
        if (!j->output) { fclose(f); fail(j, "内存不足"); return; }
        j->output_size = fread(j->output, 1, capacity, f);
        if (ferror(f) || fgetc(f) != EOF) fail(j, "文件过大或读取失败");
        fclose(f); j->output[j->output_size] = 0;
    } else {
        if(!strncmp(j->name,"projects/",9)){
            char meta[1200];snprintf(meta,sizeof(meta),"%s/project-info/%s",data_root,j->name+9);unlink(meta);
        }
        snprintf(temp, sizeof(temp), "%s/.write-%d.tmp", data_root, j->id);
        int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) { fail(j, "无法创建保存文件"); return; }
        size_t size = strlen(j->input), offset = 0;
        while (offset < size) {
            ssize_t n = write(fd, j->input + offset, size - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { fail(j, "写入失败"); break; }
            offset += (size_t)n;
        }
        if (!j->error[0] && fsync(fd)) fail(j, "同步保存失败");
        if (close(fd) && !j->error[0]) fail(j, "关闭保存文件失败");
        if (!j->error[0] && rename(temp, path)) fail(j, "更新保存文件失败");
        if (j->error[0]) unlink(temp);
        else {
            char parent[1200]; snprintf(parent, sizeof(parent), "%s", path);
            char *slash = strrchr(parent, '/'); if (slash) *slash = 0;
            fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fd < 0 || fsync(fd)) fail(j, "同步目录失败");
            if (fd >= 0) close(fd);
        }
    }
}
static int utf8(const char **cursor, uint32_t *cp) {
    const unsigned char *p = (const unsigned char *)*cursor;
    if (!*p) return 0;
    unsigned n; uint32_t value;
    if (*p < 0x80) { n = 1; value = *p; }
    else if (*p >= 0xc2 && *p <= 0xdf) { n = 2; value = *p & 31; }
    else if (*p >= 0xe0 && *p <= 0xef) { n = 3; value = *p & 15; }
    else if (*p >= 0xf0 && *p <= 0xf4) { n = 4; value = *p & 7; }
    else return -1;
    for (unsigned i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) return -1;
        value = (value << 6) | (p[i] & 63);
    }
    if ((n == 3 && value < 0x800) || (n == 4 && value < 0x10000) ||
        value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return -1;
    *cursor += n; *cp = value; return 1;
}
static int glyph_index(uint32_t cp) {
    return cp<65536 && glyph_lookup[cp] ? (int)glyph_lookup[cp]-1 : -1;
}
static int add_glyph(Job *j, uint32_t cp) {
    if(cp>=65536){fail(j,"文本包含未支持的字形");return 0;}
    if (glyph_index(cp) >= 0) return 1;
    if (glyph_count == MAX_GLYPHS) { fail(j, "当前字形缓存已满，请重启应用"); return 0; }
    TTF_Font *face = TTF_GlyphIsProvided(font, (Uint16)cp) ? font : fallback;
    if (cp >= 0xfffd || !face || !TTF_GlyphIsProvided(face, (Uint16)cp)) { fail(j, "字体不包含此字符，请换用完整中文字体"); return 0; }
    int minx, maxx, miny, maxy, advance;
    if (TTF_GlyphMetrics(face, (Uint16)cp, &minx, &maxx, &miny, &maxy, &advance)) { fail(j, "读取字形失败"); return 0; }
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *raw = TTF_RenderGlyph_Blended(face, (Uint16)cp, white);
    SDL_Surface *s = raw ? SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0) : NULL;
    if (raw) SDL_FreeSurface(raw);
    int y_offset = (int)baseline - TTF_FontAscent(face);
    if (!s || advance < 0 || advance > 255 || s->w > (int)cell_width || s->h + y_offset > (int)cell_height) {
        if (s) SDL_FreeSurface(s);
        fail(j, "字形尺寸超出限制"); return 0;
    }
    uint8_t *pixels = calloc(cell_width, cell_height);
    if (!pixels) { SDL_FreeSurface(s); fail(j, "内存不足"); return 0; }
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) {
        uint32_t p; memcpy(&p, (uint8_t *)s->pixels + y * s->pitch + x * 4, 4);
        pixels[(y + y_offset) * cell_width + x] = (uint8_t)(p >> 24);
    }
    SDL_FreeSurface(s);
    glyphs[glyph_count] = cp; glyph_pixels[glyph_count] = pixels;
    glyph_advances[glyph_count] = (uint8_t)advance;
    glyph_offsets[glyph_count] = (uint8_t)(minx < 0 ? -minx : 0);
    glyph_lookup[cp]=(uint16_t)(glyph_count+1);glyph_count++; return 1;
}
static void put16(uint8_t *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void put32(uint8_t *p, uint32_t n) { for (int i = 0; i < 4; i++) p[i] = n >> (i*8); }
static int compare_cp(const void *a, const void *b) {
    uint32_t x = glyphs[*(const unsigned *)a], y = glyphs[*(const unsigned *)b]; return (x > y) - (x < y);
}
static int append_line(Job *j, const char *from, size_t length) {
    if (j->line_count >= 1024) { fail(j, "文本过长"); return 0; }
    char **lines = realloc(j->lines, (size_t)(j->line_count + 1) * sizeof(*lines));
    if (!lines) { fail(j, "内存不足"); return 0; }
    j->lines = lines; lines[j->line_count] = strndup(from, length);
    if (!lines[j->line_count]) { fail(j, "内存不足"); return 0; }
    j->line_count++; return 1;
}
static int page_line(Job *j,const char *from,size_t length,int first,int last){
    if(j->reverse&&j->line_count==j->page_lines){
        free(j->lines[0]);memmove(j->lines,j->lines+1,(j->line_count-1)*sizeof(*j->lines));
        memmove(j->line_first,j->line_first+1,(j->line_count-1)*sizeof(int));
        memmove(j->line_last,j->line_last+1,(j->line_count-1)*sizeof(int));j->line_count--;
    }
    if(!append_line(j,from,length))return 0;
    int i=j->line_count-1;j->line_first[i]=first;j->line_last[i]=last;
    j->first_char=j->line_first[0];j->last_char=last;return 1;
}
static void prepare_text(Job *j) {
    atomic_fetch_add(&text_jobs,1);
    if (!font) {
        if (!TTF_WasInit() && TTF_Init()) { fail(j, "字体服务初始化失败"); return; }
        font = TTF_OpenFont(font_path, DYNAMIC_PX);
        if (!font) { fail(j, "无法读取中文字体，请检查 font1.ttf"); return; }
        fallback = TTF_OpenFont("NotoSansSC-Regular.otf", DYNAMIC_PX);
        if (!fallback) { TTF_CloseFont(font); font = NULL; fail(j, "中文补字字体缺失，请重新安装完整应用包"); return; }
        baseline = (unsigned)TTF_FontAscent(font);
        if (TTF_FontAscent(fallback) > (int)baseline) baseline = (unsigned)TTF_FontAscent(fallback);
        cell_width = DYNAMIC_PX * 2;
        cell_height = (unsigned)TTF_FontHeight(font) + baseline - TTF_FontAscent(font);
        unsigned other_height = (unsigned)TTF_FontHeight(fallback) + baseline - TTF_FontAscent(fallback);
        if (other_height > cell_height) cell_height = other_height;
        if (cell_height < 1 || cell_height > 255) { fail(j, "字体高度不支持"); return; }
    }
    const char *p = j->input; uint32_t cp; int rc;
    while ((rc = utf8(&p, &cp)) > 0) {
        if (cancelled(j)) return;
        if (cp == '\n') continue;
        if (!add_glyph(j, cp)) return;
    }
    if (rc < 0) { fail(j, "文本不是有效 UTF-8"); return; }
    /* Reuse cached rasters and publish a complete snapshot, including after cancelled jobs. */
    if(glyph_count!=uploaded_glyphs) {
        atomic_fetch_add(&atlas_builds,1);
        unsigned n = glyph_count + 1;
        size_t cell = cell_width * cell_height;
        j->atlas_size = 16 + n * 8 + n * cell;
        j->atlas = calloc(1, j->atlas_size);
        if (!j->atlas) { fail(j, "内存不足"); return; }
        uint8_t *b = j->atlas; put32(b, 0x41464344); put16(b+4, 3); put16(b+6, n);
        b[8] = cell_width; b[9] = cell_height; b[10] = baseline;
        b[11] = cell_height; b[12] = DYNAMIC_SLOT; b[14] = 1;
        unsigned order[MAX_GLYPHS]; for (unsigned i = 0; i < glyph_count; i++) order[i] = i;
        qsort(order, glyph_count, sizeof(*order), compare_cp);
        /* U+FFFD is the final cmap entry and gid 0 (glyph pool excludes it). */
        for (unsigned k = 0; k < glyph_count; k++) {
            unsigned i = order[k]; uint8_t *e = b + 16 + k*8;
            put32(e, glyphs[i]); put16(e+4, i+1); e[6] = glyph_advances[i]; e[7] = glyph_offsets[i];
            memcpy(b + 16 + n*8 + (i+1)*cell, glyph_pixels[i], cell);
        }
        uint8_t *e = b + 16 + glyph_count*8; put32(e, 0xfffd); e[6] = DYNAMIC_PX;
        uint8_t *tofu = b + 16 + n*8;
        for (unsigned y = 4; y < cell_height-4; y++) for (unsigned x = 2; x < DYNAMIC_PX-2; x++)
            if (y == 4 || y == cell_height-5 || x == 2 || x == DYNAMIC_PX-3) tofu[y*cell_width+x] = 180;
    }
    p = j->input; const char *start = p; unsigned width = 0;int chars=0,first=0;
    while (*p) {
        const char *previous = p; if (utf8(&p, &cp) < 0) break;
        if (cp == '\n') {
            chars++;
            if (!(j->page_lines?page_line(j,start,(size_t)(previous-start),first,chars):append_line(j,start,(size_t)(previous-start))))return;
            if(j->page_lines&&!j->reverse&&j->line_count>=j->page_lines)return;
            start = p;first=chars; width = 0; continue;
        }
        unsigned advance = glyph_advances[glyph_index(cp)];
        if (width && width + advance > (unsigned)(j->wrap_width?j->wrap_width:888)) {
            if (!(j->page_lines?page_line(j,start,(size_t)(previous-start),first,chars):append_line(j,start,(size_t)(previous-start))))return;
            if(j->page_lines&&!j->reverse&&j->line_count>=j->page_lines)return;
            start = previous;first=chars; width = 0;
        }
        width += advance;
        chars+=cp>0xffff?2:1;
    }
    if (p != start || !j->line_count) {
        if(j->page_lines)page_line(j,start,(size_t)(p-start),first,chars);else append_line(j,start,(size_t)(p-start));
    }
}

/* curl reads quoted values from stdin; no shell and no API key in argv. */
static char *quote(const char *text) {
    size_t n = strlen(text); char *out = malloc(n*2+3); if (!out) return NULL;
    char *p = out; *p++ = '"';
    for (size_t i = 0; i < n; i++) { if (text[i] == '"' || text[i] == '\\') *p++ = '\\'; *p++ = text[i]; }
    *p++ = '"'; *p = 0; return out;
}
/* Detect body framing without assuming that stream=true was honored. A split
 * SSE prefix remains undecided; JSON/error bodies keep their complete text. */
static int http_stream_kind(const char *body,size_t size){
    while(size&&isspace((unsigned char)*body)){body++;size--;}
    if(!size)return 0;
    if(*body==':')return 2;
    const char *fields[]={"data:","event:","id:","retry:"};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++){
        size_t n=strlen(fields[i]),compare=size<n?size:n;
        if(!memcmp(body,fields[i],compare))return size<n?0:2;
    }
    return 1;
}
static void http(Job *j) {
    char shared[512];
    const char *provider = nextui_ai_provider_for_url(j->url);
    int common = provider ? nextui_ai_read_key(provider, shared, sizeof(shared)) : 0;
    if (common < 0) { fail(j, "公共 Key 配置格式无效，请检查 ai-keys.txt"); return; }
    const char *auth = common ? shared : j->auth;
    int output[2] = {-1,-1};
    FILE *config = tmpfile();
    if (!config) { fail(j, "无法准备请求"); return; }
    char *q = quote(j->url); if (!q) { fclose(config); fail(j, "内存不足"); return; }
    fprintf(config, "url = %s\nrequest = \"%s\"\n", q, j->method); free(q);
    char header[640]; snprintf(header, sizeof(header), "Authorization: Bearer %s", auth);
    q = quote(header);
    if (!q) { fclose(config); fail(j, "内存不足"); return; }
    fprintf(config, "header = \"Content-Type: application/json\"\nheader = %s\n", q); free(q);
    FILE *body = NULL; char body_path[64];
    if (j->input && *j->input) {
        /* Anonymous descriptor, never a secret argv. */
        body = tmpfile();
        if (!body || fwrite(j->input, 1, strlen(j->input), body) != strlen(j->input)) { fail(j, "无法准备请求内容"); goto cleanup; }
        fflush(body); rewind(body);
#ifdef __APPLE__
        snprintf(body_path, sizeof(body_path), "@/dev/fd/3");
#else
        snprintf(body_path, sizeof(body_path), "@/proc/self/fd/3");
#endif
    }
    fflush(config); rewind(config);
#ifdef __APPLE__
    if (pipe(output)) { fail(j, "无法创建网络管道"); goto cleanup; }
    fcntl(output[0], F_SETFD, FD_CLOEXEC);
    fcntl(output[1], F_SETFD, FD_CLOEXEC);
#else
    if (pipe2(output, O_CLOEXEC)) { fail(j, "无法创建网络管道"); goto cleanup; }
#endif
    int total_ms=getenv("POCKETJS_WORKSHOP")&&j->streaming&&j->timeout_ms>=120000?300000:j->timeout_ms;
    char seconds[24]; snprintf(seconds, sizeof(seconds), "%.3f", total_ms/1000.0);
    const char *ca = getenv("POCKETJS_CA");
    char *args[32] = {"curl", "--silent", "--show-error", "--connect-timeout", "5", "--max-time", seconds,
        "--proto", "=http,https", "--proto-redir", "=https", "--max-redirs", "3", "--location",
        "--config", "-", "--write-out", "\n%{http_code}", NULL};
    int count = 18;
    if(j->streaming)args[count++]="--no-buffer";
    if (common) { args[count++] = "--max-redirs"; args[count++] = "0"; }
    if (body) { args[count++] = "--data-binary"; args[count++] = body_path; }
    if (ca && *ca) { args[count++] = "--cacert"; args[count++] = (char *)ca; }
    args[count] = NULL;
    posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fileno(config), STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (devnull >= 0) posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
    if (body) posix_spawn_file_actions_adddup2(&actions, fileno(body), 3);
    pid_t pid = 0;
    int spawn_error = posix_spawnp(&pid, "curl", &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (devnull >= 0) close(devnull);
    close(output[1]); output[1] = -1;
    if (spawn_error) { fail(j, "设备缺少 curl，无法发送请求"); goto cleanup; }
    pthread_mutex_lock(&lock); j->pid = pid; if (j->cancelled || stopping) kill(pid, SIGKILL); pthread_mutex_unlock(&lock);
    size_t response_limit=getenv("POCKETJS_WORKSHOP")?MAX_WORKSHOP_RESPONSE:MAX_BODY,capacity=16384;
    j->output = malloc(capacity);
    if (!j->output) { fail(j, "内存不足"); kill(pid, SIGKILL); }
    fcntl(output[0], F_SETFL, O_NONBLOCK);
    double began=millis(),total_deadline=began+total_ms+1000,deadline=began+j->timeout_ms+1000;
    int ended = 0;
    while (!ended && !j->error[0]) {
        if (cancelled(j)) { kill(pid, SIGKILL); break; }
        if (millis() > deadline) { fail(j, "请求超时"); kill(pid, SIGKILL); break; }
        /* Let the pipe/curl apply backpressure while the UI consumes bounded
         * deliveries. Do not build a giant JS callback after a slow frame. */
        pthread_mutex_lock(&lock);
        int backpressure=j->stream_kind==2&&j->output_size-j->stream_offset>=MAX_STREAM_PENDING;
        pthread_mutex_unlock(&lock);
        if(backpressure){poll(NULL,0,5);continue;}
        struct pollfd fd = {output[0], POLLIN | POLLHUP, 0}; poll(&fd, 1, 25);
        char chunk[8192];ssize_t n = read(output[0],chunk,sizeof(chunk));
        if (n > 0) {
            atomic_fetch_add(&http_received_bytes,(size_t)n);
            double next=millis()+j->timeout_ms+1000;deadline=next<total_deadline?next:total_deadline;
            pthread_mutex_lock(&lock);
            j->received_size+=(size_t)n;
            if(j->stream_kind==2&&j->stream_offset&&j->output_size+(size_t)n+1>capacity){
                memmove(j->output,j->output+j->stream_offset,j->output_size-j->stream_offset);
                j->output_size-=j->stream_offset;atomic_fetch_add(&http_reclaimed_bytes,j->stream_offset);j->stream_offset=0;
            }
            size_t required=j->output_size+(size_t)n+1;
            if(required>response_limit+5){fail(j,"响应内容过大");kill(pid,SIGKILL);}
            else{
                if(required>capacity){size_t next=capacity*2;while(next<required)next*=2;if(next>response_limit+16)next=response_limit+16;char *grown=realloc(j->output,next);if(!grown){fail(j,"内存不足");kill(pid,SIGKILL);}else{j->output=grown;capacity=next;}}
                if(!j->error[0]){
                    memcpy(j->output+j->output_size,chunk,(size_t)n);j->output_size+=(size_t)n;
                    if(j->streaming&&!j->stream_kind)j->stream_kind=http_stream_kind(j->output,j->output_size);
                    size_t peak=atomic_load(&http_peak_buffer_bytes);
                    while(capacity>peak&&!atomic_compare_exchange_weak(&http_peak_buffer_bytes,&peak,capacity)){}
                }
            }
            pthread_mutex_unlock(&lock);
        } else if (n == 0) ended = 1;
        else if (errno != EAGAIN && errno != EINTR) { fail(j, "读取网络响应失败"); kill(pid, SIGKILL); }
    }
    int status = 0; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    pthread_mutex_lock(&lock); j->pid = 0; pthread_mutex_unlock(&lock);
    if (cancelled(j)) fail(j, "请求已取消");
    if (!j->error[0] && (!WIFEXITED(status) || WEXITSTATUS(status))) {
        int code=WIFEXITED(status)?WEXITSTATUS(status):-1;
        if(code==28)fail(j,"请求超时（curl 28）");
        else if(code==60)fail(j,"证书校验失败，请检查设备时间（curl 60）");
        else if(code==6)fail(j,"域名解析失败（curl 6）");
        else if(code==7)fail(j,"无法连接服务器（curl 7）");
        else if(code==18||code==52||code==55||code==56||code==92)snprintf(j->error,sizeof(j->error),"连接被重置或传输中断（curl %d）",code);
        else snprintf(j->error,sizeof(j->error),"网络请求失败（curl %d）",code);
    }
    pthread_mutex_lock(&lock);
    if (!j->error[0]) {
        if (j->output_size < 4 || j->output[j->output_size-4] != '\n') fail(j, "无效 HTTP 响应");
        else {
            j->output[j->output_size] = 0;
            j->http_status = atoi(j->output + j->output_size-3);
            j->output_size -= 4; j->output[j->output_size] = 0;
            if (j->http_status < 100 || j->http_status > 599) fail(j, "无效 HTTP 状态码");
        }
    }
    pthread_mutex_unlock(&lock);
cleanup:
    if(getenv("POCKETJS_WORKSHOP"))fprintf(stderr,"Workshop HTTP: status=%d bytes=%zu buffered=%zu error=%s\n",j->http_status,j->received_size,j->output_size,j->error[0]?j->error:"none");
    for (int k = 0; k < 2; k++) { if (output[k] >= 0) close(output[k]); }
    if (body) fclose(body);
    fclose(config);
}
static int system_path(const char *name,char *path,size_t size,char *root,size_t root_size){
    if(!name||strlen(name)>=96||strstr(name,"..")||strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.")!=strlen(name))return 0;
    const char *proc=NULL;
    if(!strcmp(name,"system/meminfo"))proc="meminfo";
    else if(!strcmp(name,"system/cpuinfo"))proc="cpuinfo";
    else if(!strcmp(name,"system/version"))proc="version";
    else if(!strcmp(name,"system/uptime"))proc="uptime";
    else if(!strcmp(name,"system/loadavg"))proc="loadavg";
    else if(!strcmp(name,"system/app-status"))proc="self/status";
    if(proc){snprintf(root,root_size,"/proc");snprintf(path,size,"/proc/%s",proc);return 1;}
    if(!strncmp(name,"skill/",6)){
        const char *suffix=strrchr(name,'.');if(!suffix||(strcmp(suffix,".md")&&strcmp(suffix,".lua")&&strcmp(suffix,".p8")&&strcmp(suffix,".txt")&&strcmp(suffix,".json")))return 0;
        snprintf(root,root_size,"./skills/pico8-game-dev");snprintf(path,size,"%s/%s",root,name+6);return 1;
    }
    if(!strcmp(name,"logs/workshop.log")||!strcmp(name,"logs/launch-error.txt")){
        snprintf(root,root_size,"%s",data_root);snprintf(path,size,"%s/%s",data_root,name+5);return 1;
    }
    if(!strcmp(name,"runtime/launch.sh")||!strcmp(name,"runtime/README.md")||!strcmp(name,"runtime/build-receipt.txt")){
        snprintf(root,root_size,".");snprintf(path,size,"./%s",name+8);return 1;
    }
    if(!strcmp(name,"runtime/P8-launch.sh")){
        const char *launch=getenv("POCKETJS_P8_LAUNCH");if(!launch||!launch[0]||strlen(launch)>=size)return 0;
        snprintf(path,size,"%s",launch);snprintf(root,root_size,"%s",launch);char *slash=strrchr(root,'/');if(!slash)return 0;*slash=0;return 1;
    }
    return 0;
}
static void system_read(Job *j){
    char path[1200],root[1200],canonical[4096],base[4096];
    if(!system_path(j->name,path,sizeof(path),root,sizeof(root))||!realpath(root,base)||!realpath(path,canonical)){fail(j,"诊断文件未安装或不在允许读取范围");return;}
    size_t length=strlen(base);if(strncmp(base,canonical,length)||canonical[length]!='/'){fail(j,"诊断路径越界，未读取");return;}
    int fd=open(canonical,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)){if(fd>=0)close(fd);fail(j,"无法读取诊断文件");return;}
    size_t limit=65536;int log=!strncmp(j->name,"logs/",5);
    if(log&&st.st_size>(off_t)limit)lseek(fd,st.st_size-(off_t)limit,SEEK_SET);
    j->output=malloc(limit+1);if(!j->output){close(fd);fail(j,"内存不足");return;}
    size_t used=0;while(used<limit&&!cancelled(j)){ssize_t n=read(fd,j->output+used,limit-used);if(n>0)used+=(size_t)n;else if(n<0&&errno==EINTR)continue;else break;}close(fd);j->output[used]=0;
    if(log){ /* Never expose accidental credential lines to the model. */
        char *line=j->output;while(*line){char *end=strchr(line,'\n');size_t n=end?(size_t)(end-line):strlen(line);char saved=line[n];line[n]=0;
            if(strstr(line,"sk-")||strstr(line,"Bearer ")||strstr(line,"api_key")||strstr(line,"Authorization"))memset(line,'*',n);
            line[n]=saved;if(!end)break;line=end+1;}
    }
    j->output_size=used;if(cancelled(j))fail(j,"文件读取已取消");
}
static void script_job(Job *j){
    int terminal=j->type==TERMINAL;FILE *body=tmpfile();int output[2]={-1,-1};pid_t pid=0;
    if(!body||fwrite(j->input,1,strlen(j->input),body)!=strlen(j->input)){fail(j,"无法准备脚本输入");if(body)fclose(body);return;}
    fflush(body);rewind(body);
    if(pipe(output)){fail(j,"无法创建脚本管道");fclose(body);return;}
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,fileno(body),STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,output[1],STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions,output[0]);posix_spawn_file_actions_addclose(&actions,output[1]);
    char *args[]={terminal?"./workshop-terminal":"./workshop-check",NULL};int code=posix_spawn(&pid,args[0],&actions,NULL,args,environ);
    posix_spawn_file_actions_destroy(&actions);close(output[1]);fclose(body);
    if(code){close(output[0]);fail(j,terminal?"安全脚本环境未安装，未运行任何系统命令":"PICO-8 语法检查器未安装");return;}
    pthread_mutex_lock(&lock);j->pid=pid;if(j->cancelled||stopping)kill(pid,SIGKILL);pthread_mutex_unlock(&lock);
    char detail[16448]={0};size_t size=0;double deadline=millis()+(terminal?5000:4000);fcntl(output[0],F_SETFL,O_NONBLOCK);
    for(;;){if(cancelled(j)||millis()>deadline){kill(pid,SIGKILL);fail(j,terminal?"辅助脚本已取消或超过时间预算":"语法检查取消或超过时间预算");break;}
        struct pollfd fd={output[0],POLLIN|POLLHUP,0};poll(&fd,1,25);char chunk[256];ssize_t n=read(output[0],chunk,sizeof(chunk));
        if(n>0){size_t take=(size_t)n;if(take>sizeof(detail)-1-size)take=sizeof(detail)-1-size;memcpy(detail+size,chunk,take);size+=take;}
        else if(!n)break;else if(errno!=EAGAIN&&errno!=EINTR){fail(j,"脚本输出读取失败");kill(pid,SIGKILL);break;}}
    close(output[0]);int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){}
    pthread_mutex_lock(&lock);j->pid=0;pthread_mutex_unlock(&lock);
    if(!j->error[0]&&(!WIFEXITED(status)||WEXITSTATUS(status)))snprintf(j->error,sizeof(j->error),terminal?"辅助脚本失败：%.140s":"PICO-8 语法检查失败：%.140s",detail);
    if(!j->error[0]){j->output=strdup(terminal?detail:"syntax_checked");if(j->output)j->output_size=strlen(j->output);else fail(j,"内存不足");}
}
static void *worker(void *arg) {
    int network = (int)(intptr_t)arg;
    for (;;) {
        pthread_mutex_lock(&lock); Job *j = NULL;
        while (!stopping) {
            for (int i = 0; i < JOBS; i++) if (jobs[i].state == QUEUED && (jobs[i].type == HTTP) == network) { j = &jobs[i]; break; }
            if (j) break;
            pthread_cond_wait(&ready, &lock);
        }
        if (stopping) { pthread_mutex_unlock(&lock); break; }
        j->state = RUNNING; pthread_mutex_unlock(&lock);
        if (!cancelled(j)) {
            if (j->type == HTTP) http(j);
            else if (j->type == TEXT) prepare_text(j);
            else if (j->type == CREDENTIALS) {
                char key[512]; const char *provider = nextui_ai_provider_for_url(j->url);
                int available = provider ? nextui_ai_read_key(provider, key, sizeof(key)) : 0;
                if (available < 0) fail(j, "公共 Key 配置格式无效，请检查 ai-keys.txt");
                else { j->output = strdup(available ? "yes" : "no"); if (j->output) j->output_size = strlen(j->output); else fail(j, "内存不足"); }
            }
            else if(j->type==SYSTEM_READ)system_read(j);
            else if(j->type==SYNTAX||j->type==TERMINAL)script_job(j);
            else if (j->type >= PROJECTS) workshop_job(j->type == PROJECTS ? "projects" : j->type == PUBLISH ? "publish" : j->type == COVER ? "cover" : j->type == DELETE_PROJECT ? "delete" : "play", j->name, j->input ? j->input : "", j->label, &j->output, &j->output_size, j->error, sizeof(j->error));
            else disk(j);
        }
        pthread_mutex_lock(&lock); j->state = DONE; pthread_mutex_unlock(&lock);
    }
    return NULL;
}
static const char *property(JSContext *ctx, JSValueConst value, const char *name, JSValue *hold) {
    *hold = JS_GetPropertyStr(ctx, value, name); return JS_ToCString(ctx, *hold);
}
static JSValue start_job(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx, "Expected one request");
    const char *raw = JS_ToCString(ctx, argv[0]); if (!raw) return JS_EXCEPTION;
    JSValue request = JS_ParseJSON(ctx, raw, strlen(raw), "brick-request"); JS_FreeCString(ctx, raw);
    if (JS_IsException(request)) return request;
    JSValue opvalue; const char *op = property(ctx, request, "op", &opvalue);
    Job local = {0}; int valid = 1;
    if (op && (!strcmp(op, "read") || !strcmp(op, "write"))) {
        local.type = !strcmp(op, "read") ? READ : WRITE;
        JSValue value; const char *name = property(ctx, request, "name", &value);
        if (!name || !allowed_file(name)) valid = 0;
        else snprintf(local.name, sizeof(local.name), "%s", name);
        if (name) JS_FreeCString(ctx, name);
        JS_FreeValue(ctx, value);
    } else if(op&&!strcmp(op,"system-read")&&getenv("POCKETJS_WORKSHOP")){
        local.type=SYSTEM_READ;JSValue value;const char *name=property(ctx,request,"name",&value);char path[1200],root[1200];
        if(!name||!system_path(name,path,sizeof(path),root,sizeof(root)))valid=0;else snprintf(local.name,sizeof(local.name),"%s",name);
        if(name){JS_FreeCString(ctx,name);}JS_FreeValue(ctx,value);
    } else if(op&&!strcmp(op,"terminal")&&getenv("POCKETJS_WORKSHOP"))local.type=TERMINAL;
    else if(op&&!strcmp(op,"syntax")&&getenv("POCKETJS_WORKSHOP"))local.type=SYNTAX;
    else if (op && !strcmp(op, "projects")) local.type = PROJECTS;
    else if (op && (!strcmp(op, "publish") || !strcmp(op, "play") || !strcmp(op,"cover") || !strcmp(op,"delete"))) {
        local.type = !strcmp(op, "publish") ? PUBLISH : !strcmp(op,"cover") ? COVER : !strcmp(op,"delete") ? DELETE_PROJECT : PLAY;
        JSValue value; const char *name = property(ctx, request, "name", &value);
        if (!name || strlen(name) >= sizeof(local.name)) valid = 0;
        else snprintf(local.name, sizeof(local.name), "%s", name);
        if (name) JS_FreeCString(ctx, name);
        JS_FreeValue(ctx, value);
        if (local.type == PUBLISH) {
            const char *title = property(ctx, request, "title", &value);
            if (!title || strlen(title) >= sizeof(local.label)) valid = 0;
            else snprintf(local.label, sizeof(local.label), "%s", title);
            if (title) JS_FreeCString(ctx, title);
            JS_FreeValue(ctx, value);
        }
    } else if (op && (!strcmp(op, "text")||!strcmp(op,"text-page"))) {
        local.type=TEXT;
        if(!strcmp(op,"text-page")){
            JSValue v=JS_GetPropertyStr(ctx,request,"maxLines");JS_ToInt32(ctx,&local.page_lines,v);JS_FreeValue(ctx,v);
            v=JS_GetPropertyStr(ctx,request,"reverse");local.reverse=JS_ToBool(ctx,v);JS_FreeValue(ctx,v);
            v=JS_GetPropertyStr(ctx,request,"width");if(!JS_IsUndefined(v)){if(JS_ToInt32(ctx,&local.wrap_width,v)||local.wrap_width<128||local.wrap_width>888)valid=0;}JS_FreeValue(ctx,v);
            if(local.page_lines<1||local.page_lines>16)valid=0;
        }
    }
    else if (op && !strcmp(op, "credentials")) {
        local.type = CREDENTIALS;
        JSValue value; const char *url = property(ctx, request, "url", &value);
        if (!url || strlen(url) >= sizeof(local.url)) valid = 0;
        else snprintf(local.url, sizeof(local.url), "%s", url);
        if (url) JS_FreeCString(ctx, url);
        JS_FreeValue(ctx, value);
    }
    else if (op && !strcmp(op, "http")) {
        local.type = HTTP;
        JSValue stream_value=JS_GetPropertyStr(ctx,request,"stream");local.streaming=JS_ToBool(ctx,stream_value)>0;JS_FreeValue(ctx,stream_value);
        JSValue value; const char *url = property(ctx, request, "url", &value);
        if (!url || strlen(url) >= sizeof(local.url) || strpbrk(url, "\r\n\"\\") ||
            (strncmp(url, "https://", 8) && strncmp(url, "http://127.0.0.1:", 17))) valid = 0;
        else snprintf(local.url, sizeof(local.url), "%s", url);
        if (url) JS_FreeCString(ctx, url);
        JS_FreeValue(ctx, value);
        const char *method = property(ctx, request, "method", &value);
        if (!method || (strcmp(method, "GET") && strcmp(method, "POST"))) valid = 0;
        else snprintf(local.method, sizeof(local.method), "%s", method);
        if (method) JS_FreeCString(ctx, method);
        JS_FreeValue(ctx, value);
        value = JS_GetPropertyStr(ctx, request, "timeoutMs"); JS_ToInt32(ctx, &local.timeout_ms, value); JS_FreeValue(ctx, value);
        if (local.timeout_ms < 100 || local.timeout_ms > (getenv("POCKETJS_WORKSHOP")?300000:120000)) valid = 0;
        const char *auth = property(ctx, request, "key", &value);
        if (auth && strlen(auth) < 512 && !strpbrk(auth, "\r\n")) {
            snprintf(local.auth, sizeof(local.auth), "%s", auth);
        } else valid = 0;
        if (auth) JS_FreeCString(ctx, auth);
        JS_FreeValue(ctx, value);
    } else valid = 0;
    if (local.type == WRITE || local.type == TEXT || local.type == HTTP || local.type == PUBLISH || (local.type==SYNTAX||local.type==TERMINAL)) {
        JSValue value; const char *text = property(ctx, request, "text", &value);
        size_t limit=local.type==TEXT?MAX_TEXT:(local.type==SYNTAX||local.type==TERMINAL||local.type==PUBLISH||(local.type==HTTP&&getenv("POCKETJS_WORKSHOP")))?MAX_REQUEST:local.type==WRITE&&(!strncmp(local.name,"projects/",9)||!strncmp(local.name,"history/",8))?MAX_PROJECT:MAX_BODY;
        if (!text || strlen(text) > limit) valid = 0;
        else local.input = strdup(text);
        if (text) JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, value);
        if (!local.input) valid = 0;
    }
    if (op) JS_FreeCString(ctx, op);
    JS_FreeValue(ctx, opvalue); JS_FreeValue(ctx, request);
    if (!valid) { release(&local); return JS_ThrowTypeError(ctx, "Invalid Brick request"); }
    pthread_mutex_lock(&lock); int slot = -1;
    for (int i = 0; i < JOBS; i++) if (jobs[i].state == EMPTY) { slot = i; break; }
    if (slot < 0) { pthread_mutex_unlock(&lock); release(&local); return JS_ThrowInternalError(ctx, "Request queue full"); }
    local.id = ++next_id; local.state = QUEUED; jobs[slot] = local;
    pthread_cond_broadcast(&ready); pthread_mutex_unlock(&lock);
    return JS_NewInt32(ctx, local.id);
}
static JSValue cancel_job(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; int id = 0; if (argc) JS_ToInt32(ctx, &id, argv[0]);
    pthread_mutex_lock(&lock);
    for (int i = 0; i < JOBS; i++) if (jobs[i].id == id && jobs[i].state) {
        if(jobs[i].state==ASSEMBLING){release(&jobs[i]);continue;}
        jobs[i].cancelled = 1; if (jobs[i].pid > 0) kill(jobs[i].pid, SIGKILL);
    }
    pthread_mutex_unlock(&lock); return JS_UNDEFINED;
}
/* Large writes already are JSON. Avoid wrapping/escaping/parsing that JSON a
 * second time on the UI thread; retain disk()'s atomic/fsync protocol. */
static JSValue start_write(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self;
    if(argc!=2)return JS_ThrowTypeError(ctx,"Invalid Brick write");
    size_t size=0;const char *name=JS_ToCString(ctx,argv[0]),*text=JS_ToCStringLen(ctx,&size,argv[1]);
    Job local={0};int valid=name&&text&&strlen(name)<sizeof(local.name)&&allowed_file(name);
    if(valid&&size>((!strncmp(name,"projects/",9)||!strncmp(name,"history/",8))?MAX_PROJECT:MAX_BODY))valid=0;
    if(valid){local.type=WRITE;snprintf(local.name,sizeof(local.name),"%s",name);local.input=malloc(size+1);if(local.input)memcpy(local.input,text,size+1);else valid=0;}
    if(name)JS_FreeCString(ctx,name);
    if(text)JS_FreeCString(ctx,text);
    if(!valid){release(&local);return JS_ThrowTypeError(ctx,"Invalid Brick write or file too large");}
    pthread_mutex_lock(&lock);int slot=-1;
    for(int i=0;i<JOBS;i++)if(jobs[i].state==EMPTY){slot=i;break;}
    if(slot<0){pthread_mutex_unlock(&lock);release(&local);return JS_ThrowInternalError(ctx,"Request queue full");}
    local.id=++next_id;local.state=QUEUED;jobs[slot]=local;
    pthread_cond_broadcast(&ready);pthread_mutex_unlock(&lock);return JS_NewInt32(ctx,local.id);
}
/* Only the UI thread appends to ASSEMBLING jobs. Workers see them only after
 * finishWrite queues the complete NUL-terminated buffer. Cancellation/disposal
 * releases an unfinished buffer without touching the existing project file. */
static JSValue begin_write(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv){
    (void)self;if(argc!=1||!getenv("POCKETJS_WORKSHOP"))return JS_ThrowTypeError(ctx,"Invalid project write");
    const char *name=JS_ToCString(ctx,argv[0]);
    int valid=name&&strlen(name)<sizeof(jobs[0].name)&&(!strncmp(name,"projects/",9)||!strncmp(name,"history/",8))&&allowed_file(name);
    Job local={0};if(valid){snprintf(local.name,sizeof(local.name),"%s",name);local.input_capacity=65536;local.input=malloc(local.input_capacity+1);valid=local.input!=NULL;}
    if(name)JS_FreeCString(ctx,name);
    if(!valid){release(&local);return JS_ThrowTypeError(ctx,"Invalid project write or insufficient memory");}
    local.input[0]=0;local.type=WRITE;local.state=ASSEMBLING;
    pthread_mutex_lock(&lock);int slot=-1;for(int i=0;i<JOBS;i++)if(jobs[i].state==EMPTY){slot=i;break;}
    if(slot<0){pthread_mutex_unlock(&lock);release(&local);return JS_ThrowInternalError(ctx,"Request queue full");}
    local.id=++next_id;jobs[slot]=local;pthread_mutex_unlock(&lock);return JS_NewInt32(ctx,local.id);
}
static JSValue append_write(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv){
    (void)self;int id=0;if(argc!=2||JS_ToInt32(ctx,&id,argv[0]))return JS_ThrowTypeError(ctx,"Invalid project chunk");
    size_t size=0;const char *text=JS_ToCStringLen(ctx,&size,argv[1]);if(!text)return JS_EXCEPTION;
    int valid=0;pthread_mutex_lock(&lock);
    for(int i=0;i<JOBS;i++)if(jobs[i].id==id&&jobs[i].state==ASSEMBLING){Job *j=&jobs[i];if(size<=512*1024&&size<=MAX_PROJECT-j->input_size&&!memchr(text,0,size)){
        size_t required=j->input_size+size;
        if(required>j->input_capacity){size_t capacity=j->input_capacity;while(capacity<required)capacity*=2;if(capacity>MAX_PROJECT)capacity=MAX_PROJECT;
            char *buffer=realloc(j->input,capacity+1);if(!buffer)break;j->input=buffer;j->input_capacity=capacity;
        }
        memcpy(j->input+j->input_size,text,size);j->input_size+=size;j->input[j->input_size]=0;valid=1;}break;}
    pthread_mutex_unlock(&lock);JS_FreeCString(ctx,text);
    return valid?JS_UNDEFINED:JS_ThrowTypeError(ctx,"Invalid project chunk or file too large");
}
static JSValue finish_write(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv){
    (void)self;int id=0;if(argc!=1||JS_ToInt32(ctx,&id,argv[0]))return JS_ThrowTypeError(ctx,"Invalid project write");
    int valid=0;pthread_mutex_lock(&lock);
    for(int i=0;i<JOBS;i++)if(jobs[i].id==id&&jobs[i].state==ASSEMBLING){jobs[i].state=QUEUED;valid=1;pthread_cond_broadcast(&ready);break;}
    pthread_mutex_unlock(&lock);return valid?JS_NewInt32(ctx,id):JS_ThrowTypeError(ctx,"Invalid project write");
}
static JSValue poll_jobs(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    JSValue results = JS_NewArray(ctx); unsigned index = 0;
    /* Transfer ownership under the lock; atlas upload and JS allocation happen outside it. */
    for (int i = 0; i < JOBS; i++) {
        pthread_mutex_lock(&lock); Job j = {0};
        if (jobs[i].state == DONE&&(jobs[i].error[0]||jobs[i].cancelled||jobs[i].stream_kind!=2||jobs[i].output_size-jobs[i].stream_offset<=MAX_STREAM_PROGRESS)) { j = jobs[i]; memset(&jobs[i], 0, sizeof(Job)); }
        else if((jobs[i].state==RUNNING||jobs[i].state==DONE)&&jobs[i].streaming&&jobs[i].stream_kind==2&&jobs[i].output_size>jobs[i].stream_offset){
            // Only publish complete UTF-8 sequences. SSE and JSON can split at
            // any pipe read; an unfinished suffix remains for the next poll.
            Job *s=&jobs[i];
            /* curl appends \nNNN. Keep four bytes until the worker validates
             * and removes the status trailer, even when it splits pipe reads. */
            size_t end=s->state==RUNNING?(s->output_size>4?s->output_size-4:0):s->output_size,at=end;
            if(s->stream_kind==2&&end>s->stream_offset+MAX_STREAM_PROGRESS)end=s->stream_offset+MAX_STREAM_PROGRESS;
            at=end;
            while(at>s->stream_offset&&((unsigned char)s->output[at-1]&0xc0)==0x80)at--;
            if(at>s->stream_offset){size_t lead=at-1;unsigned char c=s->output[lead];unsigned need=c>=0xf0?4:c>=0xe0?3:c>=0xc0?2:1;if(end-lead<need)end=lead;}
            if(end>s->stream_offset){
                size_t bytes=end-s->stream_offset,peak=atomic_load(&http_max_progress_bytes);
                while(bytes>peak&&!atomic_compare_exchange_weak(&http_max_progress_bytes,&peak,bytes)){}
                JSValue progress=JS_NewObject(ctx);JS_SetPropertyStr(ctx,progress,"id",JS_NewInt32(ctx,s->id));JS_SetPropertyStr(ctx,progress,"progress",JS_TRUE);
                JS_SetPropertyStr(ctx,progress,"text",JS_NewStringLen(ctx,s->output+s->stream_offset,end-s->stream_offset));s->stream_offset=end;
                JS_SetPropertyUint32(ctx,results,index++,progress);
                if(s->stream_kind==2&&s->stream_offset>=s->output_size/2){
                    memmove(s->output,s->output+end,s->output_size-end);s->output_size-=end;s->stream_offset=0;
                    atomic_fetch_add(&http_reclaimed_bytes,end);
                }
            }
        }
        pthread_mutex_unlock(&lock); if (!j.state) continue;
        if (j.cancelled) fail(&j, "请求已取消");
        unsigned version=j.atlas?(unsigned)(j.atlas[6]|j.atlas[7]<<8)-1:0;
        if (!j.error[0] && j.atlas&&version>uploaded_glyphs){
            if(!ui_load_font_atlas(j.atlas,j.atlas_size))fail(&j,"字形图集加载失败");
            else{uploaded_glyphs=version;atomic_fetch_add(&atlas_uploads,1);}
        }
        if (j.error[0]) fprintf(stderr, "Brick service %d: %s\n", j.type, j.error);
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "id", JS_NewInt32(ctx, j.id));
        JS_SetPropertyStr(ctx, result, "error", JS_NewString(ctx, j.error));
        size_t sent=j.type==HTTP&&j.stream_kind==2?j.stream_offset:0;
        if(sent>j.output_size)sent=j.output_size;
        JS_SetPropertyStr(ctx, result, "text", JS_NewStringLen(ctx, j.output ? j.output+sent : "", j.output_size-sent));
        if(j.type==HTTP&&j.stream_kind==2)JS_SetPropertyStr(ctx,result,"streamTail",JS_TRUE);
        JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, j.http_status));
        JSValue lines = JS_NewArray(ctx);
        for (int n = 0; n < j.line_count; n++) JS_SetPropertyUint32(ctx, lines, n, JS_NewString(ctx, j.lines[n]));
        JS_SetPropertyStr(ctx, result, "lines", lines);
        if(j.page_lines){
            /* Positions use the very same glyph advances as wrapping. Rich
             * inline spans must never guess at CJK/Latin character widths. */
            JSValue widths=JS_NewArray(ctx);
            for(int n=0;n<j.line_count;n++){
                JSValue positions=JS_NewArray(ctx);unsigned x=0,k=0;uint32_t cp;const char *cursor=j.lines[n];
                JS_SetPropertyUint32(ctx,positions,k++,JS_NewInt32(ctx,0));
                while(utf8(&cursor,&cp)>0){int g=glyph_index(cp);if(g<0)break;x+=glyph_advances[g];if(cp>0xffff)JS_SetPropertyUint32(ctx,positions,k++,JS_NewInt32(ctx,x));JS_SetPropertyUint32(ctx,positions,k++,JS_NewInt32(ctx,x));}
                JS_SetPropertyUint32(ctx,widths,n,positions);
            }
            JS_SetPropertyStr(ctx,result,"widths",widths);
        }
        JS_SetPropertyStr(ctx,result,"start",JS_NewInt32(ctx,j.first_char));JS_SetPropertyStr(ctx,result,"end",JS_NewInt32(ctx,j.last_char));
        JS_SetPropertyUint32(ctx, results, index++, result); release(&j);
    }
    return results;
}
static JSValue service_sha256(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;if(argc!=1||!JS_IsString(argv[0]))return JS_ThrowTypeError(ctx,"SHA256 expects text");size_t n=0;const char *text=JS_ToCStringLen(ctx,&n,argv[0]);if(!text)return JS_EXCEPTION;
    if(n>16*1024*1024){JS_FreeCString(ctx,text);return JS_ThrowRangeError(ctx,"SHA256 input budget exceeded");}
    char hex[65];ws_sha256((const unsigned char*)text,n,hex);JS_FreeCString(ctx,text);return JS_NewStringLen(ctx,hex,64);
}
static JSValue service_metrics(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)argc;(void)argv;JSValue fn=JS_GetPropertyStr(ctx,self,"hardwareMetrics"),value=JS_IsFunction(ctx,fn)?JS_Call(ctx,fn,self,0,NULL):JS_NewObject(ctx);JS_FreeValue(ctx,fn);if(JS_IsException(value))return value;
    JS_SetPropertyStr(ctx,value,"textJobs",JS_NewInt32(ctx,atomic_load(&text_jobs)));
    JS_SetPropertyStr(ctx,value,"atlasBuilds",JS_NewInt32(ctx,atomic_load(&atlas_builds)));
    JS_SetPropertyStr(ctx,value,"atlasUploads",JS_NewInt32(ctx,atomic_load(&atlas_uploads)));
    JS_SetPropertyStr(ctx,value,"glyphs",JS_NewInt32(ctx,atomic_load(&uploaded_glyphs)));
    JS_SetPropertyStr(ctx,value,"httpReceivedBytes",JS_NewFloat64(ctx,(double)atomic_load(&http_received_bytes)));
    JS_SetPropertyStr(ctx,value,"httpReclaimedBytes",JS_NewFloat64(ctx,(double)atomic_load(&http_reclaimed_bytes)));
    JS_SetPropertyStr(ctx,value,"httpPeakBufferBytes",JS_NewFloat64(ctx,(double)atomic_load(&http_peak_buffer_bytes)));
    JS_SetPropertyStr(ctx,value,"httpMaxProgressBytes",JS_NewFloat64(ctx,(double)atomic_load(&http_max_progress_bytes)));
    struct rusage stats;long rss=0;if(!getrusage(RUSAGE_SELF,&stats)){rss=stats.ru_maxrss;
#ifdef __APPLE__
        rss/=1024;
#endif
    }
    JS_SetPropertyStr(ctx,value,"frameMaxMs",JS_NewFloat64(ctx,frame_max_ms));JS_SetPropertyStr(ctx,value,"peakRssKiB",JS_NewFloat64(ctx,(double)rss));return value;
}
static int32_t boot_services(JSContext *ctx, const uint8_t *pak, size_t size, int32_t w, int32_t h) {
    (void)pak; (void)size; (void)w; (void)h;
    workshop_configure_font();
    const char *root = getenv("POCKETJS_DATA"), *path = getenv("POCKETJS_FONT");
    if (!root || strlen(root) >= sizeof(data_root) || !path || strlen(path) >= sizeof(font_path)) return 0;
    snprintf(data_root, sizeof(data_root), "%s", root); snprintf(font_path, sizeof(font_path), "%s", path);
    if (mkdir(data_root, 0700) && errno != EEXIST) return 0;
    stopping = 0; next_id = 0;
    dynamic_px=getenv("POCKETJS_WORKSHOP")?30:36;
    for (int i = 0; i < 2; i++) {
        if (pthread_create(&threads[i], NULL, worker, (void *)(intptr_t)i)) return 0;
        started++;
    }
    JSValue bridge = JS_NewObject(ctx), global = JS_GetGlobalObject(ctx);
    if (!workshop_boot(ctx, bridge)) { JS_FreeValue(ctx, bridge); JS_FreeValue(ctx, global); return 0; }
    JS_SetPropertyStr(ctx, bridge, "testing", JS_NewBool(ctx, getenv("POCKETJS_TEST") != NULL));
    JS_SetPropertyStr(ctx, bridge, "testUrl", JS_NewString(ctx, getenv("POCKETJS_TEST_URL") ? getenv("POCKETJS_TEST_URL") : ""));
    JS_SetPropertyStr(ctx, bridge, "testTlsUrl", JS_NewString(ctx, getenv("POCKETJS_TEST_TLS_URL") ? getenv("POCKETJS_TEST_TLS_URL") : ""));
    JS_SetPropertyStr(ctx, bridge, "start", JS_NewCFunction(ctx, start_job, "start", 1));
    JS_SetPropertyStr(ctx, bridge, "startWrite", JS_NewCFunction(ctx,start_write,"startWrite",2));
    JS_SetPropertyStr(ctx, bridge, "beginWrite", JS_NewCFunction(ctx,begin_write,"beginWrite",1));
    JS_SetPropertyStr(ctx, bridge, "appendWrite", JS_NewCFunction(ctx,append_write,"appendWrite",2));
    JS_SetPropertyStr(ctx, bridge, "finishWrite", JS_NewCFunction(ctx,finish_write,"finishWrite",1));
    JS_SetPropertyStr(ctx, bridge, "sha256", JS_NewCFunction(ctx, service_sha256, "sha256", 1));
    JS_SetPropertyStr(ctx, bridge, "cancel", JS_NewCFunction(ctx, cancel_job, "cancel", 1));
    JS_SetPropertyStr(ctx, bridge, "poll", JS_NewCFunction(ctx, poll_jobs, "poll", 0));
    JS_SetPropertyStr(ctx,bridge,"metrics",JS_NewCFunction(ctx,service_metrics,"metrics",0));
    /* Hardware boot registers a separate metrics() API below. */
    JS_SetPropertyStr(ctx,bridge,"serviceMetrics",JS_NewCFunction(ctx,service_metrics,"serviceMetrics",0));
    if (!brick_hardware_boot(ctx, bridge)) { JS_FreeValue(ctx, bridge); JS_FreeValue(ctx, global); return 0; }
    int ok = JS_SetPropertyStr(ctx, global, "brick", bridge) >= 0; JS_FreeValue(ctx, global); return ok;
}
static void shutdown_services(int32_t gl) {
    (void)gl; workshop_voice_shutdown(); workshop_visual_shutdown(); brick_hardware_shutdown(); pthread_mutex_lock(&lock); stopping = 1;
    for (int i = 0; i < JOBS; i++) { jobs[i].cancelled = 1; if (jobs[i].pid > 0) kill(jobs[i].pid, SIGKILL); }
    pthread_cond_broadcast(&ready); pthread_mutex_unlock(&lock);
    for (int i = 0; i < started; i++) pthread_join(threads[i], NULL);
    started = 0; for (int i = 0; i < JOBS; i++) release(&jobs[i]);
    for (unsigned i = 0; i < glyph_count; i++) { free(glyph_pixels[i]); glyph_pixels[i] = NULL; }
    glyph_count = 0;uploaded_glyphs=0;memset(glyph_lookup,0,sizeof(glyph_lookup));
    atomic_store(&text_jobs,0);atomic_store(&atlas_builds,0);atomic_store(&atlas_uploads,0);
    atomic_store(&http_received_bytes,0);atomic_store(&http_reclaimed_bytes,0);atomic_store(&http_peak_buffer_bytes,0);atomic_store(&http_max_progress_bytes,0);
    if (font) { TTF_CloseFont(font); font = NULL; }
    if (fallback) { TTF_CloseFont(fallback); fallback = NULL; }
    if (TTF_WasInit()) TTF_Quit();
}
int brick_services_pending(void) {
    pthread_mutex_lock(&lock); int count = 0;
    for (int i = 0; i < JOBS; i++) if (jobs[i].state) count++;
    pthread_mutex_unlock(&lock); return count;
}
static const PocketJsSymbianExtensionV1 extension = {
    1, sizeof(PocketJsSymbianExtensionV1), 0, boot_services, shutdown_services, NULL, NULL, NULL, NULL
};
const PocketJsSymbianExtensionV1 *pocketjs_symbian_extension_v1(void) { return &extension; }

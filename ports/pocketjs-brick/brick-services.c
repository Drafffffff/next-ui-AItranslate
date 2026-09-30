#define _GNU_SOURCE
#include "brick-services.h"
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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

extern char **environ;
#define JOBS 8
#define MAX_BODY (128 * 1024)
#define MAX_TEXT (64 * 1024)
#define MAX_GLYPHS 4096
#define DYNAMIC_SLOT 20
#define DYNAMIC_PX 36

enum { EMPTY, QUEUED, RUNNING, DONE };
enum { READ, WRITE, TEXT, HTTP };
typedef struct {
    int state, type, id, cancelled, timeout_ms, http_status;
    pid_t pid;
    char name[24], url[2048], method[8];
    char *input, *headers, *output;
    size_t output_size;
    char error[192];
    uint8_t *atlas;
    size_t atlas_size;
    char **lines;
    int line_count;
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
    free(j->input); free(j->headers); free(j->output); free(j->atlas);
    for (int i = 0; i < j->line_count; i++) free(j->lines[i]);
    free(j->lines); memset(j, 0, sizeof(*j));
}
static int allowed_file(const char *name) {
    return !strcmp(name, "config.json") || !strcmp(name, "history.json") || !strcmp(name, "prompt.txt");
}
static void disk(Job *j) {
    char path[1100], temp[1120]; snprintf(path, sizeof(path), "%s/%s", data_root, j->name);
    if (j->type == READ) {
        FILE *f = fopen(path, "rb");
        if (!f) { if (errno == ENOENT) j->output = strdup(""); else fail(j, "读取文件失败"); return; }
        j->output = malloc(MAX_BODY + 1);
        if (!j->output) { fclose(f); fail(j, "内存不足"); return; }
        j->output_size = fread(j->output, 1, MAX_BODY, f);
        if (ferror(f) || fgetc(f) != EOF) fail(j, "文件过大或读取失败");
        fclose(f); j->output[j->output_size] = 0;
    } else {
        snprintf(temp, sizeof(temp), "%s/.%s.%d.tmp", data_root, j->name, j->id);
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
            fd = open(data_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
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
    for (unsigned i = 0; i < glyph_count; i++) if (glyphs[i] == cp) return (int)i;
    return -1;
}
static int add_glyph(Job *j, uint32_t cp) {
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
    glyph_count++; return 1;
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
static void prepare_text(Job *j) {
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
    {
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
    p = j->input; const char *start = p; unsigned width = 0;
    while (*p) {
        const char *previous = p; if (utf8(&p, &cp) < 0) break;
        if (cp == '\n') { if (!append_line(j, start, (size_t)(previous-start))) return; start = p; width = 0; continue; }
        unsigned advance = glyph_advances[glyph_index(cp)];
        if (width && width + advance > 888) {
            if (!append_line(j, start, (size_t)(previous-start))) return;
            start = previous; width = 0;
        }
        width += advance;
    }
    if (p != start || !j->line_count) append_line(j, start, (size_t)(p-start));
}

/* curl reads quoted values from stdin; no shell and no API key in argv. */
static char *quote(const char *text) {
    size_t n = strlen(text); char *out = malloc(n*2+3); if (!out) return NULL;
    char *p = out; *p++ = '"';
    for (size_t i = 0; i < n; i++) { if (text[i] == '"' || text[i] == '\\') *p++ = '\\'; *p++ = text[i]; }
    *p++ = '"'; *p = 0; return out;
}
static void http(Job *j) {
    int output[2] = {-1,-1};
    FILE *config = tmpfile();
    if (!config) { fail(j, "无法准备请求"); return; }
    char *q = quote(j->url); if (!q) { fclose(config); fail(j, "内存不足"); return; }
    fprintf(config, "url = %s\nrequest = \"%s\"\n", q, j->method); free(q);
    if (j->headers) fputs(j->headers, config);
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
    char seconds[24]; snprintf(seconds, sizeof(seconds), "%.3f", j->timeout_ms/1000.0);
    const char *ca = getenv("POCKETJS_CA");
    char *args[32] = {"curl", "--silent", "--show-error", "--connect-timeout", "5", "--max-time", seconds,
        "--proto", "=http,https", "--proto-redir", "=https", "--max-redirs", "3", "--location",
        "--config", "-", "--write-out", "\n%{http_code}", NULL};
    int count = 18;
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
    j->output = malloc(MAX_BODY + 16);
    if (!j->output) { fail(j, "内存不足"); kill(pid, SIGKILL); }
    fcntl(output[0], F_SETFL, O_NONBLOCK);
    double deadline = millis() + j->timeout_ms + 1000;
    int ended = 0;
    while (!ended && !j->error[0]) {
        if (cancelled(j)) { kill(pid, SIGKILL); break; }
        if (millis() > deadline) { fail(j, "请求超时"); kill(pid, SIGKILL); break; }
        struct pollfd fd = {output[0], POLLIN | POLLHUP, 0}; poll(&fd, 1, 25);
        ssize_t n = read(output[0], j->output + j->output_size, MAX_BODY + 8 - j->output_size);
        if (n > 0) {
            j->output_size += (size_t)n;
            if (j->output_size > MAX_BODY + 4) { fail(j, "响应内容过大"); kill(pid, SIGKILL); }
        } else if (n == 0) ended = 1;
        else if (errno != EAGAIN && errno != EINTR) { fail(j, "读取网络响应失败"); kill(pid, SIGKILL); }
    }
    int status = 0; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    pthread_mutex_lock(&lock); j->pid = 0; pthread_mutex_unlock(&lock);
    if (cancelled(j)) fail(j, "请求已取消");
    if (!j->error[0] && (!WIFEXITED(status) || WEXITSTATUS(status))) {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 28) fail(j, "请求超时");
        else if (WIFEXITED(status) && WEXITSTATUS(status) == 60) fail(j, "证书校验失败，请检查设备时间");
        else fail(j, "网络连接失败，请检查 Wi-Fi");
    }
    if (!j->error[0]) {
        if (j->output_size < 4 || j->output[j->output_size-4] != '\n') fail(j, "无效 HTTP 响应");
        else {
            j->output[j->output_size] = 0;
            j->http_status = atoi(j->output + j->output_size-3);
            j->output_size -= 4; j->output[j->output_size] = 0;
            if (j->http_status < 100 || j->http_status > 599) fail(j, "无效 HTTP 状态码");
        }
    }
cleanup:
    for (int k = 0; k < 2; k++) { if (output[k] >= 0) close(output[k]); }
    if (body) fclose(body);
    fclose(config);
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
    } else if (op && !strcmp(op, "text")) local.type = TEXT;
    else if (op && !strcmp(op, "http")) {
        local.type = HTTP;
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
        if (local.timeout_ms < 100 || local.timeout_ms > 120000) valid = 0;
        const char *auth = property(ctx, request, "key", &value);
        if (auth && strlen(auth) < 512 && !strpbrk(auth, "\r\n")) {
            char header[640]; snprintf(header, sizeof(header), "Authorization: Bearer %s", auth);
            char *escaped = quote(header);
            if (escaped) { size_t n = strlen(escaped) + 100; local.headers = malloc(n); if (local.headers) snprintf(local.headers, n, "header = \"Content-Type: application/json\"\nheader = %s\n", escaped); free(escaped); }
        } else valid = 0;
        if (auth) JS_FreeCString(ctx, auth);
        JS_FreeValue(ctx, value);
    } else valid = 0;
    if (local.type == HTTP && !local.headers) valid = 0;
    if (local.type == WRITE || local.type == TEXT || local.type == HTTP) {
        JSValue value; const char *text = property(ctx, request, "text", &value);
        if (!text || strlen(text) > (local.type == TEXT ? MAX_TEXT : MAX_BODY)) valid = 0;
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
        jobs[i].cancelled = 1; if (jobs[i].pid > 0) kill(jobs[i].pid, SIGKILL);
    }
    pthread_mutex_unlock(&lock); return JS_UNDEFINED;
}
static JSValue poll_jobs(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    JSValue results = JS_NewArray(ctx); unsigned index = 0;
    /* Transfer ownership under the lock; atlas upload and JS allocation happen outside it. */
    for (int i = 0; i < JOBS; i++) {
        pthread_mutex_lock(&lock); Job j = {0};
        if (jobs[i].state == DONE) { j = jobs[i]; memset(&jobs[i], 0, sizeof(Job)); }
        pthread_mutex_unlock(&lock); if (!j.state) continue;
        if (j.cancelled) fail(&j, "请求已取消");
        if (!j.error[0] && j.atlas && !ui_load_font_atlas(j.atlas, j.atlas_size)) fail(&j, "字形图集加载失败");
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "id", JS_NewInt32(ctx, j.id));
        JS_SetPropertyStr(ctx, result, "error", JS_NewString(ctx, j.error));
        JS_SetPropertyStr(ctx, result, "text", JS_NewStringLen(ctx, j.output ? j.output : "", j.output_size));
        JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, j.http_status));
        JSValue lines = JS_NewArray(ctx);
        for (int n = 0; n < j.line_count; n++) JS_SetPropertyUint32(ctx, lines, n, JS_NewString(ctx, j.lines[n]));
        JS_SetPropertyStr(ctx, result, "lines", lines);
        JS_SetPropertyUint32(ctx, results, index++, result); release(&j);
    }
    return results;
}
static int32_t boot_services(JSContext *ctx, const uint8_t *pak, size_t size, int32_t w, int32_t h) {
    (void)pak; (void)size; (void)w; (void)h;
    const char *root = getenv("POCKETJS_DATA"), *path = getenv("POCKETJS_FONT");
    if (!root || strlen(root) >= sizeof(data_root) || !path || strlen(path) >= sizeof(font_path)) return 0;
    snprintf(data_root, sizeof(data_root), "%s", root); snprintf(font_path, sizeof(font_path), "%s", path);
    if (mkdir(data_root, 0700) && errno != EEXIST) return 0;
    stopping = 0; next_id = 0;
    for (int i = 0; i < 2; i++) {
        if (pthread_create(&threads[i], NULL, worker, (void *)(intptr_t)i)) return 0;
        started++;
    }
    JSValue bridge = JS_NewObject(ctx), global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, bridge, "testing", JS_NewBool(ctx, getenv("POCKETJS_TEST") != NULL));
    JS_SetPropertyStr(ctx, bridge, "testUrl", JS_NewString(ctx, getenv("POCKETJS_TEST_URL") ? getenv("POCKETJS_TEST_URL") : ""));
    JS_SetPropertyStr(ctx, bridge, "testTlsUrl", JS_NewString(ctx, getenv("POCKETJS_TEST_TLS_URL") ? getenv("POCKETJS_TEST_TLS_URL") : ""));
    JS_SetPropertyStr(ctx, bridge, "start", JS_NewCFunction(ctx, start_job, "start", 1));
    JS_SetPropertyStr(ctx, bridge, "cancel", JS_NewCFunction(ctx, cancel_job, "cancel", 1));
    JS_SetPropertyStr(ctx, bridge, "poll", JS_NewCFunction(ctx, poll_jobs, "poll", 0));
    int ok = JS_SetPropertyStr(ctx, global, "brick", bridge) >= 0; JS_FreeValue(ctx, global); return ok;
}
static void shutdown_services(int32_t gl) {
    (void)gl; pthread_mutex_lock(&lock); stopping = 1;
    for (int i = 0; i < JOBS; i++) { jobs[i].cancelled = 1; if (jobs[i].pid > 0) kill(jobs[i].pid, SIGKILL); }
    pthread_cond_broadcast(&ready); pthread_mutex_unlock(&lock);
    for (int i = 0; i < started; i++) pthread_join(threads[i], NULL);
    started = 0; for (int i = 0; i < JOBS; i++) release(&jobs[i]);
    for (unsigned i = 0; i < glyph_count; i++) { free(glyph_pixels[i]); glyph_pixels[i] = NULL; }
    glyph_count = 0;
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

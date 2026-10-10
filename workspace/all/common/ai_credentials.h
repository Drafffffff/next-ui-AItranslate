#ifndef NEXTUI_AI_CREDENTIALS_H
#define NEXTUI_AI_CREDENTIALS_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Plain data, never sourced as shell code. No keys leave the native caller. */
static inline const char *nextui_ai_key_name(const char *provider) {
    if (!strcmp(provider, "deepseek")) return "DEEPSEEK_API_KEY";
    if (!strcmp(provider, "bailian")) return "DASHSCOPE_API_KEY";
    if (!strcmp(provider, "custom")) return "CUSTOM_API_KEY";
    return NULL;
}

static inline int nextui_ai_keys_path(char *path, size_t size) {
    const char *override = getenv("NEXTUI_AI_KEYS_FILE");
    const char *shared = getenv("SHARED_USERDATA_PATH");
    if (!shared || !*shared) shared = "/mnt/SDCARD/.userdata/shared";
    int n = override && *override ? snprintf(path, size, "%s", override) :
        snprintf(path, size, "%s/ai-keys.txt", shared);
    return n >= 0 && (size_t)n < size;
}

/* 1: nonempty shared key, 0: absent/empty (legacy fallback), -1: invalid.
 * Read on demand so changing the public file does not require app settings
 * to be rewritten. Limits bound IO and prevent truncated credentials. */
static inline int nextui_ai_read_key(const char *provider, char *out, size_t size) {
    if (!out || !size) return -1;
    out[0] = 0;
    const char *name = nextui_ai_key_name(provider);
    if (!name) return -1;
    char path[1200];
    if (!nextui_ai_keys_path(path, sizeof(path))) return -1;
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? 0 : -1;
    struct stat info;
    if (fstat(fileno(file), &info) || !S_ISREG(info.st_mode) || info.st_size > 16384) { fclose(file); return -1; }
    char contents[16385];
    size_t bytes = fread(contents, 1, sizeof(contents)-1, file);
    int invalid = ferror(file) || fgetc(file) != EOF || memchr(contents, 0, bytes) != NULL;
    fclose(file);
    if (invalid) return -1;
    contents[bytes] = 0;
    int found = 0, first = 1, result = 0;
    char *cursor = contents;
    while (*cursor) {
        char *line = cursor;
        char *next = strchr(cursor, '\n');
        if (next) { *next = 0; cursor = next+1; } else cursor += strlen(cursor);
        if (strlen(line) > 1022) { result = -1; break; }
        char *p = line;
        if (first && !strncmp(p, "\xef\xbb\xbf", 3)) p += 3;
        first = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == ';' || *p == '\r' || *p == '\n') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        char *end = eq;
        while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
        *end = 0;
        if (strcmp(p, name)) continue;
        if (found++) { result = -1; break; }
        p = eq + 1;
        while (*p == ' ' || *p == '\t') p++;
        end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
        *end = 0;
        size_t length = (size_t)(end-p);
        if (length >= size || length > 511) { result = -1; break; }
        for (size_t i = 0; i < length; i++) if ((unsigned char)p[i] < 33 || (unsigned char)p[i] > 126) { result = -1; break; }
        if (result < 0) break;
        memcpy(out, p, length+1);
        result = length ? 1 : 0;
    }
    if (result < 0) out[0] = 0;
    return result;
}

static inline const char *nextui_ai_provider_for_url(const char *url) {
    if (!url || strncmp(url, "https://", 8)) return NULL;
    const char *start = url+8, *end = strchr(start, '/');
    size_t length = end ? (size_t)(end-start) : strlen(start);
    char host[256];
    if (!length || length >= sizeof(host)) return NULL;
    memcpy(host, start, length); host[length] = 0;
    if (!strcmp(host, "api.deepseek.com")) return "deepseek";
    if (!strcmp(host, "dashscope.aliyuncs.com") || !strcmp(host, "dashscope-intl.aliyuncs.com") || !strcmp(host, "dashscope-us.aliyuncs.com")) return "bailian";
    /* Official workspace domains only; no suffix matching on untrusted URLs. */
    const char *dot = strchr(host, '.');
    if (!dot || dot == host) return NULL;
    for (const char *p=host; p<dot; p++)
        if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_')) return NULL;
    const char *regions[]={"cn-beijing","ap-southeast-1","us-east-1","eu-central-1","ap-northeast-1","cn-hongkong"};
    for (size_t i=0; i<sizeof(regions)/sizeof(regions[0]); i++) {
        char suffix[80]; snprintf(suffix,sizeof(suffix),".%s.maas.aliyuncs.com",regions[i]);
        if (!strcmp(dot,suffix)) return "bailian";
    }
    return NULL;
}

#endif

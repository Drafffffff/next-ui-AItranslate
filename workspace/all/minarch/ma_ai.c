#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "ma_internal.h"
#include "notification.h"
#include "ma_frontend_opts.h"
#include "ma_menu.h"
#include "ma_ai.h"
#include "ma_video.h"

/* 临时文件放 SD 卡：设备上的 /tmp 不保证可写、也未必装得下这几百 KB */
#define AI_TMP_DIR   SDCARD_PATH "/.userdata/ai"
#define AI_REQ_PATH  AI_TMP_DIR "/req.json"
#define AI_PNG_PATH  AI_TMP_DIR "/screen.png"
#define AI_RESP_PATH AI_TMP_DIR "/resp.json"

/* AI 配置单独放一个文件，不写进 minuisettings.txt。
 * 原因：CFG_sync() 会按调用者自己的内存结构把整个 minuisettings.txt 重写一遍，
 * 而卡上的 nextui.elf / settings.elf 还是原版、不认识 ai* 键 —— 每次开机它们
 * 都会把 ai* 那几行抹掉。放独立文件后只有本模块读它，谁也动不了。 */
#define AI_CFG_PATH  SDCARD_PATH "/.userdata/shared/ai-translate.txt"

#define AI_MAX_ITEMS 32
/* 一段话最多这么多行（用来放模型给的 lines）。超过就不收，退回用整块框。 */
#define AI_ITEM_LINES 12
#define AI_WAIT_SECS 30          /* 错误提示框停留多久 */
#define AI_FADE_MS   400         /* 译文渐隐时长 */

/* 最近一次发给模型的图有多大（ai_save_png 里记下来）。
 * 模型偶尔不按归一化给坐标，只有知道发出去的是多大的图才能把它换算回来。 */
static int ai_img_w = 0, ai_img_h = 0;

typedef struct {
	char  orig[768];
	char  zh[768];
	float box[4];                /* 整块矩形，归一化 0~1000：左,上,右,下 */
	/* 这一段的原文行框只用于判断文字层级；显示端不再擦除原图。 */
	float lines[AI_ITEM_LINES][4];
	int   nlines;                /* 0 = 模型没给 lines，退回用 box */
	char  color[16];
	char  align[12];
	char  kind[16];             /* dialogue/menu/title/label/caption；旧响应可省略 */
} AI_Item;

/* ------------------------------------------------------------------ 配置读取 */

/*
 * 配置以 .userdata/shared/ai-translate.txt 为唯一权威来源（本文件直接解析）。
 * common/config.c 里也有一套 CFG_getAI* 字段，那是早期接进配置系统的遗留，
 * 现在已经没有任何引用 —— 保留是因为它不影响任何行为，删了要动 6 个插入点、
 * 反而有回归风险。真要清理请连 config.{c,h} 一起处理。
 */

#define AI_PROVIDER_BAILIAN  0
#define AI_PROVIDER_DEEPSEEK 1
#define AI_PROVIDER_CUSTOM   2

static char ai_c_endpoint[256];   /* 仅 provider=custom 时用 */
static char ai_c_model[64];       /* 同上 */
static char ai_c_key[192];        /* 通用 key（兜底） */
static char ai_c_bkey[192];       /* 百炼专用 */
static char ai_c_dkey[192];       /* DeepSeek 专用 */
static char ai_c_lang[32];
static int  ai_c_enable   = -1;
static int  ai_c_provider = AI_PROVIDER_DEEPSEEK;   /* 默认服务商 */
static int  ai_c_timeout  = -1;
static int  ai_c_maxw     = -1;
static int  ai_c_hold     = -1;
static int  ai_c_debug    = 0;
static int  ai_c_play_interval = -1;   /* 两次决策之间等多久（毫秒） */
static int  ai_c_play_calls    = -1;   /* 单次会话最多问多少次，防烧钱 */
static int  ai_c_play_badge    = -1;   /* 是否显示左下角状态角标（默认关） */
static int  ai_c_context       = -1;   /* 连续翻译的上下文记忆开关（默认开） */
static int  ai_c_loaded   = 0;

/* 文件原文按行留着，回写时只改我们管的键 —— 用户的注释和其它行都保留 */
#define AI_CFG_MAX_LINES 80
#define AI_CFG_MAX_LEN   512
static char ai_lines[AI_CFG_MAX_LINES][AI_CFG_MAX_LEN];
static int  ai_nlines = 0;

static char* ai_trim(char* s) {
	while (*s == ' ' || *s == '\t') s++;
	char* e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	*e = '\0';
	return s;
}

static void ai_cfg_copy(char* dst, int cap, const char* src) {
	if (!src) { dst[0] = '\0'; return; }
	strncpy(dst, src, cap - 1);
	dst[cap - 1] = '\0';
}

/* 我们管的键；回写时按这个顺序补写缺失的 */
static const char* ai_keys[] = {
	"aiEnable", "aiProvider", "aiApiKey", "aiBailianKey", "aiDeepseekKey",
	"aiTargetLang", "aiHoldSecs", "aiMaxImageWidth", "aiTimeoutSecs",
	"aiEndpoint", "aiModel", "aiDebug",
	"aiPlayIntervalMs", "aiPlayMaxCalls", "aiPlayBadge", "aiContext", NULL
};

static const char* ai_key_value(const char* key, char* buf, int cap) {
	if (!strcmp(key, "aiEnable"))        { snprintf(buf, cap, "%d", ai_c_enable > 0 ? 1 : 0); return buf; }
	if (!strcmp(key, "aiProvider"))      { snprintf(buf, cap, "%d", ai_c_provider); return buf; }
	if (!strcmp(key, "aiApiKey"))        { return ai_c_key; }
	if (!strcmp(key, "aiBailianKey"))    { return ai_c_bkey; }
	if (!strcmp(key, "aiDeepseekKey"))   { return ai_c_dkey; }
	if (!strcmp(key, "aiTargetLang"))    { return ai_c_lang; }
	if (!strcmp(key, "aiHoldSecs"))      { snprintf(buf, cap, "%d", ai_c_hold); return buf; }
	if (!strcmp(key, "aiMaxImageWidth")) { snprintf(buf, cap, "%d", ai_c_maxw); return buf; }
	if (!strcmp(key, "aiTimeoutSecs"))   { snprintf(buf, cap, "%d", ai_c_timeout); return buf; }
	if (!strcmp(key, "aiEndpoint"))      { return ai_c_endpoint; }
	if (!strcmp(key, "aiModel"))         { return ai_c_model; }
	if (!strcmp(key, "aiDebug"))         { snprintf(buf, cap, "%d", ai_c_debug); return buf; }
	if (!strcmp(key, "aiPlayIntervalMs")){ snprintf(buf, cap, "%d", ai_c_play_interval); return buf; }
	if (!strcmp(key, "aiPlayMaxCalls"))  { snprintf(buf, cap, "%d", ai_c_play_calls); return buf; }
	if (!strcmp(key, "aiPlayBadge"))     { snprintf(buf, cap, "%d", ai_c_play_badge); return buf; }
	if (!strcmp(key, "aiContext"))       { snprintf(buf, cap, "%d", ai_c_context > 0 ? 1 : 0); return buf; }
	return NULL;
}

/* 把内存里的值写回文件：已有的键就地改，没有的追加，其余行原样保留 */
static void ai_cfg_save(void) {
	if (!ai_c_loaded) return;
	int seen[24];                    /* 键数涨到 16 了，这里必须跟着放大 */
	int nkeys = 0;
	while (ai_keys[nkeys]) nkeys++;
	if (nkeys > 24) nkeys = 24;
	for (int i = 0; i < nkeys; i++) seen[i] = 0;

	FILE* f = fopen(AI_CFG_PATH, "w");
	if (!f) return;
	if (ai_nlines == 0) {
		fprintf(f, "# NextUI AI 画面翻译配置\n"
		           "# 这个文件是 AI 翻译专用的 —— 不写进 minuisettings.txt 是因为\n"
		           "# 卡上的启动器不认识 ai* 键，每次开机都会把那些行重写掉。\n"
		           "# 游戏内 Options -> AI Translate 可以改开关/服务商/停留时间。\n");
	}
	for (int i = 0; i < ai_nlines; i++) {
		char tmp[AI_CFG_MAX_LEN];
		ai_cfg_copy(tmp, sizeof(tmp), ai_lines[i]);
		char* t = ai_trim(tmp);
		if (*t == '\0' || *t == '#' || *t == ';') { fprintf(f, "%s\n", ai_lines[i]); continue; }
		char* eq = strchr(t, '=');
		if (!eq) { fprintf(f, "%s\n", ai_lines[i]); continue; }
		*eq = '\0';
		char* k = ai_trim(t);
		int found = -1;
		for (int j = 0; j < nkeys; j++) if (!strcmp(ai_keys[j], k)) { found = j; break; }
		if (found < 0) { fprintf(f, "%s\n", ai_lines[i]); continue; }
		char vb[256];
		const char* v = ai_key_value(ai_keys[found], vb, sizeof(vb));
		fprintf(f, "%s=%s\n", ai_keys[found], v ? v : "");
		seen[found] = 1;
	}
	for (int i = 0; i < nkeys; i++) {
		if (seen[i]) continue;
		char vb[256];
		const char* v = ai_key_value(ai_keys[i], vb, sizeof(vb));
		if (v) fprintf(f, "%s=%s\n", ai_keys[i], v);
	}
	fclose(f);
	sync();
}

static void ai_cfg_load(void) {
	if (ai_c_loaded) return;
	ai_c_loaded = 1;

	/* 先给一套默认值 */
	ai_cfg_copy(ai_c_lang, sizeof(ai_c_lang), "简体中文");
	ai_c_enable = 0;
	ai_c_timeout = 20;
	ai_c_maxw = 768;
	ai_c_hold = 5;
	ai_c_context = 1;                /* 连续翻译默认带上下文 */

	FILE* f = fopen(AI_CFG_PATH, "r");
	if (!f) return;
	char line[AI_CFG_MAX_LEN];
	while (fgets(line, sizeof(line), f)) {
		if (ai_nlines < AI_CFG_MAX_LINES) {
			char* nl = strpbrk(line, "\r\n");
			if (nl) *nl = '\0';
			ai_cfg_copy(ai_lines[ai_nlines], AI_CFG_MAX_LEN, line);
			ai_nlines++;
		}
		char t[AI_CFG_MAX_LEN];
		ai_cfg_copy(t, sizeof(t), line);
		char* p = ai_trim(t);
		if (*p == '\0' || *p == '#' || *p == ';') continue;
		char* eq = strchr(p, '=');
		if (!eq) continue;
		*eq = '\0';
		char* key = ai_trim(p);
		char* val = ai_trim(eq + 1);
		if      (!strcmp(key, "aiEnable"))        ai_c_enable  = atoi(val);
		else if (!strcmp(key, "aiProvider"))      ai_c_provider = atoi(val);
		else if (!strcmp(key, "aiApiKey"))        ai_cfg_copy(ai_c_key,  sizeof(ai_c_key),  val);
		else if (!strcmp(key, "aiBailianKey"))    ai_cfg_copy(ai_c_bkey, sizeof(ai_c_bkey), val);
		else if (!strcmp(key, "aiDeepseekKey"))   ai_cfg_copy(ai_c_dkey, sizeof(ai_c_dkey), val);
		else if (!strcmp(key, "aiTargetLang"))    ai_cfg_copy(ai_c_lang, sizeof(ai_c_lang), val);
		else if (!strcmp(key, "aiEndpoint"))      ai_cfg_copy(ai_c_endpoint, sizeof(ai_c_endpoint), val);
		else if (!strcmp(key, "aiModel"))         ai_cfg_copy(ai_c_model, sizeof(ai_c_model), val);
		else if (!strcmp(key, "aiTimeoutSecs"))   ai_c_timeout = atoi(val);
		else if (!strcmp(key, "aiMaxImageWidth")) ai_c_maxw    = atoi(val);
		else if (!strcmp(key, "aiHoldSecs"))      ai_c_hold    = atoi(val);
		else if (!strcmp(key, "aiDebug"))         ai_c_debug   = atoi(val);
		else if (!strcmp(key, "aiPlayIntervalMs")) ai_c_play_interval = atoi(val);
		else if (!strcmp(key, "aiPlayMaxCalls"))   ai_c_play_calls    = atoi(val);
		else if (!strcmp(key, "aiPlayBadge"))      ai_c_play_badge    = atoi(val);
		else if (!strcmp(key, "aiContext"))        ai_c_context       = atoi(val);
	}
	fclose(f);

	if (ai_c_provider < 0 || ai_c_provider > AI_PROVIDER_CUSTOM) ai_c_provider = AI_PROVIDER_DEEPSEEK;
	if (ai_c_timeout < 5)   ai_c_timeout = 5;
	if (ai_c_timeout > 120) ai_c_timeout = 120;
	if (ai_c_maxw < 240)    ai_c_maxw = 240;
	if (ai_c_maxw > 1920)   ai_c_maxw = 1920;
	if (ai_c_hold < 0)      ai_c_hold = 0;
	if (ai_c_hold > 600)    ai_c_hold = 600;
	if (!ai_c_lang[0]) ai_cfg_copy(ai_c_lang, sizeof(ai_c_lang), "简体中文");
	/* 默认 0：上一次的按住和下一次请求重叠进行，角色才不会走一步停一步 */
	if (ai_c_play_interval < 0)     ai_c_play_interval = 0;
	if (ai_c_play_interval > 60000) ai_c_play_interval = 60000;
	/* 次数上限纯粹是防烧钱：500 次约 25 分钟、按 DeepSeek 单价约 $0.1 */
	if (ai_c_play_calls < 0)        ai_c_play_calls = 500;
	/* 角标默认关：开/关已经有通知提示，一直挡在画面上太烦 */
	if (ai_c_play_badge < 0)        ai_c_play_badge = 0;
	if (ai_c_play_calls > 100000)   ai_c_play_calls = 100000;
}

static int         ai_on(void)       { ai_cfg_load(); return ai_c_enable > 0; }
static int         ai_debug(void)    { ai_cfg_load(); return ai_c_debug; }
static int         ai_play_interval_ms(void) { ai_cfg_load(); return ai_c_play_interval; }
static int         ai_play_max_calls(void)   { ai_cfg_load(); return ai_c_play_calls; }
static int         ai_play_badge_on(void)    { ai_cfg_load(); return ai_c_play_badge; }
static int         ai_hold_secs(void){ ai_cfg_load(); return ai_c_hold; }
static int         ai_timeout(void)  { ai_cfg_load(); return ai_c_timeout; }
static int         ai_maxw(void)     { ai_cfg_load(); return ai_c_maxw; }
static const char* ai_lang(void)     { ai_cfg_load(); return ai_c_lang; }

static const char* ai_endpoint(void) {
	ai_cfg_load();
	if (ai_c_provider == AI_PROVIDER_DEEPSEEK) return "https://api.deepseek.com/chat/completions";
	if (ai_c_provider == AI_PROVIDER_CUSTOM && ai_c_endpoint[0]) return ai_c_endpoint;
	return "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
}

static const char* ai_model(void) {
	ai_cfg_load();
	if (ai_c_provider == AI_PROVIDER_DEEPSEEK) return "deepseek-flash";
	if (ai_c_provider == AI_PROVIDER_CUSTOM && ai_c_model[0]) return ai_c_model;
	return "qwen3-vl-plus";
}

static const char* ai_key(void) {
	ai_cfg_load();
	const char* k = NULL;
	if (ai_c_provider == AI_PROVIDER_DEEPSEEK)   k = ai_c_dkey;
	else if (ai_c_provider == AI_PROVIDER_BAILIAN) k = ai_c_bkey;
	if (k && k[0]) return k;
	return ai_c_key;             /* 没写专用 key 就用通用的 */
}

/* ------------------------------------------------------------------ 错误提示 */

static void ai_notify(SDL_Surface* screen, const char* title, const char* detail) {
	SDL_Color fg = {255, 255, 255, 255};
	int w = screen->w, h = screen->h;
	int bw = (int)(w * 0.86f), bh = detail && *detail ? (int)(h * 0.30f) : (int)(h * 0.16f);
	int bx = (w - bw) / 2, by = (h - bh) / 2;

	SDL_Rect box = {bx, by, bw, bh};
	SDL_FillRect(screen, &box, SDL_MapRGB(screen->format, 16, 16, 20));
	SDL_Rect edge = {bx, by, bw, 2};
	SDL_FillRect(screen, &edge, SDL_MapRGB(screen->format, 220, 90, 90));

	if (font.small) {
		SDL_Surface* t = TTF_RenderUTF8_Blended(font.small, title, fg);
		if (t) {
			SDL_Rect d = {bx + (bw - t->w) / 2, by + (int)(bh * 0.22f)};
			SDL_BlitSurface(t, NULL, screen, &d);
			SDL_FreeSurface(t);
		}
	}
	if (detail && *detail && font.tiny) {
		SDL_Color dim = {200, 200, 205, 255};
		SDL_Surface* t = TTF_RenderUTF8_Blended(font.tiny, detail, dim);
		if (t) {
			SDL_Rect d = {bx + (bw - t->w) / 2, by + (int)(bh * 0.55f)};
			SDL_BlitSurface(t, NULL, screen, &d);
			SDL_FreeSurface(t);
		}
	}
	GFX_flip(screen);
}

/* 停住画面等用户按任意键（超时自动返回），顺便当错误提示的确认框用 */
static void ai_wait_dismiss(SDL_Surface* screen, int seconds) {
	uint32_t start = SDL_GetTicks();
	/* 先把热键本身松开，不然会立刻被判成"已按键" */
	while (!quit && PAD_anyPressed() && SDL_GetTicks() - start < 1500) {
		PAD_poll();
		GFX_delay();
	}
	PAD_reset();
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_anyPressed()) break;
		if (seconds > 0 && SDL_GetTicks() - start > (uint32_t)seconds * 1000) break;
		GFX_flip(screen);
		GFX_delay();
	}
	PAD_reset();
}

/* ------------------------------------------------------------------ base64 */

static const char AI_B64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* ai_base64(const unsigned char* in, size_t n) {
	size_t olen = 4 * ((n + 2) / 3);
	char* out = (char*)malloc(olen + 1);
	if (!out) return NULL;
	size_t i = 0, j = 0;
	while (i + 2 < n) {
		unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | in[i + 2];
		out[j++] = AI_B64[(v >> 18) & 63];
		out[j++] = AI_B64[(v >> 12) & 63];
		out[j++] = AI_B64[(v >> 6) & 63];
		out[j++] = AI_B64[v & 63];
		i += 3;
	}
	if (i < n) {
		unsigned v = (unsigned)in[i] << 16;
		if (i + 1 < n) v |= (unsigned)in[i + 1] << 8;
		out[j++] = AI_B64[(v >> 18) & 63];
		out[j++] = AI_B64[(v >> 12) & 63];
		out[j++] = (i + 1 < n) ? AI_B64[(v >> 6) & 63] : '=';
		out[j++] = '=';
	}
	out[j] = '\0';
	return out;
}

/* ------------------------------------------------------------------ 抓帧 */

/* 从视频模块的独立游戏帧抓图，绝不读取已合成的 GL 显示画面。
 * 保留当前缩放/裁剪/黑边位置，游戏边框、旧译文、通知不进入模型。 */
static SDL_Surface* ai_grab(void) {
	return Video_captureGameFrame(screen->w, screen->h, screen->format->format);
}

/* 把底图缩到 max_w 以内并存成 PNG —— 图小一点，模型预处理快、流量也省 */
static int ai_save_png(SDL_Surface* frozen, int max_w) {
	SDL_Surface* img = frozen;
	if (max_w > 0 && frozen->w > max_w) {
		int nw = max_w, nh = (int)((long)frozen->h * max_w / frozen->w);
		SDL_Surface* small = SDL_CreateRGBSurfaceWithFormat(
			0, nw, nh, 32, SDL_PIXELFORMAT_ABGR8888);
		if (small) {
			SDL_BlitScaled(frozen, NULL, small, NULL);
			img = small;
		}
	}
	/* 显式转成 ARGB8888 再存 —— 和仓库里 Menu_screenshot 的写法保持一致 */
	SDL_Surface* argb = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ARGB8888, 0);
	int rc = -1;
	if (argb) {
		ai_img_w = img->w;      /* 记下发出去的尺寸，换算像素坐标要用 */
		ai_img_h = img->h;
		SDL_RWops* rw = SDL_RWFromFile(AI_PNG_PATH, "wb");
		if (rw) {
			if (IMG_SavePNG_RW(argb, rw, 1) == 0) rc = 0;
			else SDL_RWclose(rw);
		}
		SDL_FreeSurface(argb);
	}
	if (img != frozen) SDL_FreeSurface(img);
	return rc;
}

static unsigned char* ai_read_file(const char* path, size_t* out_len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) { fclose(f); return NULL; }
	unsigned char* buf = (unsigned char*)malloc(len + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t got = fread(buf, 1, len, f);
	fclose(f);
	buf[got] = '\0';
	if (out_len) *out_len = got;
	return buf;
}

/* ------------------------------------------------------------------ JSON 拼装 */

static void ai_json_escape(const char* in, char* out, int cap) {
	int j = 0;
	for (const char* p = in; *p && j < cap - 8; p++) {
		unsigned char c = (unsigned char)*p;
		switch (c) {
			case '"':  out[j++] = '\\'; out[j++] = '"';  break;
			case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
			case '\n': out[j++] = '\\'; out[j++] = 'n';  break;
			case '\r': out[j++] = '\\'; out[j++] = 'r';  break;
			case '\t': out[j++] = '\\'; out[j++] = 't';  break;
			default:
				if (c < 0x20) { j += snprintf(out + j, cap - j, "\\u%04x", c); }
				else out[j++] = (char)c;
		}
	}
	out[j] = '\0';
}

/* ------------------------------------------------------------------ 上下文记忆 */

/* 场景记忆按屏保存、按时间过期；稳定术语独立保存，不携带过期剧情。 */
#define AI_CTX_TTL       180
#define AI_CTX_SCREENS   6
#define AI_CTX_LEN       768
#define AI_CTX_SUM       768
#define AI_CTX_TERMS     4096       /* 模型返回的增量术语串 */
#define AI_CTX_TERM_MAX  32
#define AI_CTX_TERM_LEN  96
#define AI_CTX_PREFIX    4096       /* 限制每次请求携带的历史，控制输入开销 */

typedef struct { char o[AI_CTX_LEN], z[AI_CTX_LEN]; } AI_CtxPair;
typedef struct { time_t at; int n; AI_CtxPair pairs[AI_MAX_ITEMS]; } AI_CtxScreen;
typedef struct { char o[AI_CTX_TERM_LEN], z[AI_CTX_TERM_LEN]; } AI_CtxTerm;
typedef struct {
	time_t summary_at;
	char summary[AI_CTX_SUM];
	int n, nt, dirty;
	AI_CtxScreen screens[AI_CTX_SCREENS];
	AI_CtxTerm terms[AI_CTX_TERM_MAX];
} AI_Context;

static const char* ai_ctx_path(void) {
	static char path[512];
	unsigned h = 5381;
	/* ROM 路径与目标语言隔离；m3u 优先以保持同一多碟游戏的连续性。 */
	const char* name = game.m3u_path[0] ? game.m3u_path :
		(game.path[0] ? game.path : (game.name[0] ? game.name : "unknown"));
	for (const unsigned char* p = (const unsigned char*)name; *p; p++) h = h * 33 + *p;
	h = h * 33 + '\n';
	for (const unsigned char* p = (const unsigned char*)ai_lang(); *p; p++) h = h * 33 + *p;
	snprintf(path, sizeof(path), "%s/ctx3-%08x.txt", AI_TMP_DIR, h & 0xffffffffu);
	return path;
}
static int ai_context_on(void) { ai_cfg_load(); return ai_c_context != 0; }

/* 复制完整 UTF-8 字符；即使上游已截断尾部，也不会把半个字符写入 JSON/TSV。 */
static void ai_ctx_copy(char* out, size_t cap, const char* in) {
	if (!cap) return;
	size_t used = 0;
	while (*in) {
		unsigned char c = (unsigned char)*in;
		int len = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2 :
			((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 0)));
		if (!len || used + len >= cap) break;
		int valid = 1;
		for (int i = 1; i < len; i++) {
			if (!in[i] || ((unsigned char)in[i] & 0xC0) != 0x80) { valid = 0; break; }
		}
		if (!valid) break;
		if (len == 1 && (c == '\n' || c == '\r' || c == '\t')) out[used++] = ' ';
		else { memcpy(out + used, in, (size_t)len); used += len; }
		in += len;
	}
	out[used] = 0;
}
static int ai_ctx_fresh(time_t at, time_t now) {
	return at > 0 && now >= at && difftime(now, at) < AI_CTX_TTL;
}
static void ai_ctx_trim(char* s) {
	size_t n = strlen(s);
	while (n && (unsigned char)s[n - 1] <= ' ') s[--n] = 0;
	char* p = s;
	while (*p && (unsigned char)*p <= ' ') p++;
	if (p != s) memmove(s, p, strlen(p) + 1);
}
static void ai_ctx_term_add(AI_Context* ctx, const char* original, const char* translation) {
	if (strlen(original) >= AI_CTX_TERM_LEN || strlen(translation) >= AI_CTX_TERM_LEN) return;
	AI_CtxTerm term;
	ai_ctx_copy(term.o, sizeof(term.o), original); ai_ctx_trim(term.o);
	ai_ctx_copy(term.z, sizeof(term.z), translation); ai_ctx_trim(term.z);
	if (!term.o[0] || !term.z[0]) return;
	for (int i = 0; i < ctx->nt; i++) {
		/* 模型不能随每屏摘要悄悄覆盖已经确定的译名。 */
		if (!strcasecmp(ctx->terms[i].o, term.o)) return;
	}
	if (ctx->nt < AI_CTX_TERM_MAX) ctx->terms[ctx->nt++] = term;
}
static void ai_ctx_merge_terms(AI_Context* ctx, const char* text) {
	if (!text) return;
	/* 每项以分号结束。没有终止分号的尾巴可能被响应缓冲截断，保守忽略。 */
	const char* start = text;
	for (const char* p = text; *p; p++) {
		int sep = *p == ';' ? 1 : (!strncmp(p, "；", 3) ? 3 : 0);
		if (!sep) continue;
		size_t n = (size_t)(p - start);
		char entry[AI_CTX_TERM_LEN * 2 + 2];
		if (n < sizeof(entry)) {
			memcpy(entry, start, n); entry[n] = 0;
			char* eq = strchr(entry, '=');
			if (eq) { *eq++ = 0; ai_ctx_term_add(ctx, entry, eq); }
		}
		p += sep - 1; start = p + 1;
	}
}

static void ai_ctx_load(AI_Context* ctx, time_t now) {
	static time_t last_clock = 0;
	int rollback = now < last_clock || now <= 0;
	last_clock = now;
	memset(ctx, 0, sizeof(*ctx));
	FILE* f = fopen(ai_ctx_path(), "r");
	if (!f) return;
	char line[AI_CTX_LEN * 2 + 80];
	/* 先读到完整一屏再提交，避免损坏/中断文件留下半屏。 */
	static AI_CtxScreen frame;
	int expected = 0;
	memset(&frame, 0, sizeof(frame));
	while (fgets(line, sizeof(line), f)) {
		if (!strchr(line, '\n')) {
			int c; while ((c = fgetc(f)) != '\n' && c != EOF) {}
			expected = 0; ctx->dirty = 1; continue;
		}
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "C\t", 2)) {
			if ((time_t)strtoll(line + 2, NULL, 10) > now) rollback = 1;
		} else if (!strncmp(line, "T\t", 2)) {
			char* tab = strchr(line + 2, '\t');
			if (tab) { *tab++ = 0; ai_ctx_term_add(ctx, line + 2, tab); }
		} else if (!strncmp(line, "S\t", 2)) {
			char* end;
			time_t at = (time_t)strtoll(line + 2, &end, 10);
			if (*end == '\t' && ai_ctx_fresh(at, now)) {
				ctx->summary_at = at; ai_ctx_copy(ctx->summary, sizeof(ctx->summary), end + 1);
			} else if (at) ctx->dirty = 1;
		} else if (!strncmp(line, "F\t", 2)) {
			memset(&frame, 0, sizeof(frame));
			char* end;
			frame.at = (time_t)strtoll(line + 2, &end, 10);
			expected = *end == '\t' ? atoi(end + 1) : 0;
			if (expected < 1 || expected > AI_MAX_ITEMS) expected = 0;
		} else if (!strncmp(line, "P\t", 2) && expected) {
			char* tab = strchr(line + 2, '\t');
			if (!tab) { expected = 0; ctx->dirty = 1; continue; }
			*tab++ = 0;
			ai_ctx_copy(frame.pairs[frame.n].o, AI_CTX_LEN, line + 2);
			ai_ctx_copy(frame.pairs[frame.n].z, AI_CTX_LEN, tab);
			if (++frame.n == expected) {
				if (ai_ctx_fresh(frame.at, now) && ctx->n < AI_CTX_SCREENS) ctx->screens[ctx->n++] = frame;
				else ctx->dirty = 1;
				expected = 0;
			}
		}
	}
	if (expected) ctx->dirty = 1;
	fclose(f);
	if (rollback) { ctx->n = 0; ctx->summary[0] = 0; ctx->summary_at = 0; ctx->dirty = 1; }
}
static int ai_ctx_store(const AI_Context* ctx, time_t now) {
	char temp[540];
	snprintf(temp, sizeof(temp), "%s.tmp", ai_ctx_path());
	FILE* f = fopen(temp, "w");
	if (!f) return 0;
	fprintf(f, "# timed translation context v3\nC\t%lld\n", (long long)now);
	for (int i = 0; i < ctx->nt; i++) fprintf(f, "T\t%s\t%s\n", ctx->terms[i].o, ctx->terms[i].z);
	fprintf(f, "S\t%lld\t%s\n", (long long)ctx->summary_at, ctx->summary);
	for (int i = 0; i < ctx->n; i++) {
		const AI_CtxScreen* frame = &ctx->screens[i];
		fprintf(f, "F\t%lld\t%d\n", (long long)frame->at, frame->n);
		for (int j = 0; j < frame->n; j++) fprintf(f, "P\t%s\t%s\n", frame->pairs[j].o, frame->pairs[j].z);
	}
	int ok = !ferror(f);
	if (fflush(f) || fsync(fileno(f))) ok = 0;
	if (fclose(f)) ok = 0;
	if (ok) { if (rename(temp, ai_ctx_path())) { unlink(temp); return 0; } }
	else unlink(temp);
	return ok;
}
static int ai_ctx_reset_scene(void) {
	static AI_Context ctx;
	time_t now = time(NULL);
	ai_ctx_load(&ctx, now);
	ctx.n = 0; ctx.summary[0] = 0; ctx.summary_at = 0;
	return ai_ctx_store(&ctx, now);
}
static void ai_ctx_update(const char* summary, const char* terms, AI_Item* items, int n) {
	if (!ai_context_on()) return;
	static AI_Context ctx;
	static AI_CtxScreen frame;
	time_t now = time(NULL);
	ai_ctx_load(&ctx, now);
	memset(&frame, 0, sizeof(frame)); frame.at = now;
	for (int i = 0; i < n && frame.n < AI_MAX_ITEMS; i++) {
		if (!items[i].zh[0] || !items[i].orig[0] ||
		    (items[i].kind[0] && strcmp(items[i].kind, "dialogue"))) continue;
		AI_CtxPair* pair = &frame.pairs[frame.n++];
		ai_ctx_copy(pair->o, sizeof(pair->o), items[i].orig);
		ai_ctx_copy(pair->z, sizeof(pair->z), items[i].zh);
	}
	if (frame.n) {
		/* 只去重相邻屏的重复台词，保留更早场景里再次说同一句的真实顺序。 */
		if (ctx.n) {
			AI_CtxScreen* prev = &ctx.screens[ctx.n - 1];
			int keep = 0;
			for (int j = 0; j < prev->n; j++) {
				int duplicate = 0;
				for (int k = 0; k < frame.n; k++) if (!strcmp(prev->pairs[j].o, frame.pairs[k].o)) { duplicate = 1; break; }
				if (!duplicate) prev->pairs[keep++] = prev->pairs[j];
			}
			prev->n = keep;
			if (!keep) ctx.n--;
		}
		if (ctx.n == AI_CTX_SCREENS) {
			memmove(ctx.screens, ctx.screens + 1, (AI_CTX_SCREENS - 1) * sizeof(AI_CtxScreen));
			ctx.n--;
		}
		ctx.screens[ctx.n++] = frame;
		/* 菜单不更新/续期摘要；新对白没给摘要时也不沿用上个场景摘要。 */
		ctx.summary[0] = 0; ctx.summary_at = 0;
		if (summary && summary[0]) { ai_ctx_copy(ctx.summary, sizeof(ctx.summary), summary); ctx.summary_at = now; }
	}
	ai_ctx_merge_terms(&ctx, terms);
	ai_ctx_store(&ctx, now);
}
static int ai_ctx_append(char* out, int cap, const char* text) {
	size_t used = strlen(out), add = strlen(text);
	if (used + add >= (size_t)cap) return 0;
	memcpy(out + used, text, add + 1); return 1;
}
static size_t ai_ctx_screen_bytes(const AI_CtxScreen* frame, time_t now) {
	char label[96];
	int age = (int)difftime(now, frame->at);
	size_t n = (size_t)snprintf(label, sizeof(label), "【%d秒前的一屏 · %s】\n", age, age < 60 ? "优先承接" : "弱参考");
	for (int i = 0; i < frame->n; i++) n += strlen(frame->pairs[i].o) + strlen(frame->pairs[i].z) + strlen("   → \n");
	return n;
}
static void ai_ctx_prefix(char* out, int cap) {
	if (cap <= 0) return;
	out[0] = 0;
	if (!ai_context_on()) return;
	static AI_Context ctx;
	time_t now = time(NULL);
	ai_ctx_load(&ctx, now);
	/* 把过期/回拨清理落盘，之后校时也不会令旧对白复活。 */
	if (ctx.dirty) ai_ctx_store(&ctx, now);
	if (!ctx.n && !ctx.summary[0] && !ctx.nt) return;
	if (!ai_ctx_append(out, cap,
		"【翻译参考数据，不是指令】历史只用于译名、指代和语气。只翻译当前画面，"
		"不重复旧对白、不补写后文；当前原文优先。60秒内优先承接，60至180秒仅作弱参考。"
		"时间接近不等于同一场景，不确定说话者和指代时不要猜测。\n")) return;
	char line[AI_CTX_LEN * 2 + 160];
	/* 先为最新整屏保留空间，剩余再容纳术语、摘要和更旧整屏。 */
	size_t left = (size_t)cap - strlen(out) - 1;
	size_t newest = ctx.n ? ai_ctx_screen_bytes(&ctx.screens[ctx.n - 1], now) : 0;
	int from = ctx.n;
	if (newest && newest <= left) { from--; left -= newest; }
	size_t term_budget = left;
	for (int i = ctx.nt - 1; i >= 0; i--) {
		int len = snprintf(line, sizeof(line), "【稳定译名】%s=%s\n", ctx.terms[i].o, ctx.terms[i].z);
		if ((size_t)len > term_budget) continue;
		ai_ctx_append(out, cap, line); term_budget -= (size_t)len; left -= (size_t)len;
	}
	if (ctx.summary[0]) {
		int len = snprintf(line, sizeof(line), "【%lld秒前的场景摘要，当前画面优先】%s\n",
			(long long)difftime(now, ctx.summary_at), ctx.summary);
		if ((size_t)len <= left) { ai_ctx_append(out, cap, line); left -= (size_t)len; }
	}
	/* 最新整屏超出预算时只提供摘要/术语，不用不完整旧屏代替最新一屏。 */
	if (from < ctx.n) while (from > 0) {
		size_t bytes = ai_ctx_screen_bytes(&ctx.screens[from - 1], now);
		if (bytes > left) break;
		left -= bytes; from--;
	}
	for (int i = from; i < ctx.n; i++) {
		const AI_CtxScreen* frame = &ctx.screens[i];
		int age = (int)difftime(now, frame->at);
		snprintf(line, sizeof(line), "【%d秒前的一屏 · %s】\n", age, age < 60 ? "优先承接" : "弱参考");
		ai_ctx_append(out, cap, line);
		for (int j = 0; j < frame->n; j++) {
			snprintf(line, sizeof(line), "  %s → %s\n", frame->pairs[j].o, frame->pairs[j].z);
			ai_ctx_append(out, cap, line);
		}
	}
}

/*
 * 提示词：只让模型出「原文 / 译文 / 紧贴文字的归一化框 / 颜色 / 对齐」。
 *
 * 2026-09-30 改版二：
 *  · 坐标只许给 0~1 的小数（归一化比例），换算成 0~1000 在设备端做
 *    （ai_boxes_normalize 里 ×1000）—— 模型只负责「比例」，单位换算归程序，
 *    这样提示词不用教它任何端点量纲，也就不容易被它当成像素刻度来理解。
 *    之所以原本写 0~1000 是随视觉模型坐标惯例（Qwen-VL / GPT-4V 都用这个刻度），
 *    但实测定成 0~1 后 deepseek 给的数一样准，而且越界/百分数的风险更低。
 *    注意：提示词只治得了「明显给了别的刻度」的情况。实测 deepseek 的坐标比例
 *    还会随图片尺寸变（同一张图，384 宽输入给的纵向坐标只有 768/960 宽输入的一半，
 *    真值 671 时它给 300），那是它内部缩放画布造成的，提示词管不了 ——
 *    真在意定位精度就用 qwen3-vl-plus（见 ai_boxes_normalize 的注释）。
 *  · 一段话（多行对话框、多行标题）**只算一项**，box 给覆盖全部行的整个矩形，
 *    zh 给完整译文、不许自己换行 —— **折行改在本地做**（ai_fit_lines）。
 *    上一版是让模型「把整段译成一句通顺的中文、再按中文语序分回各条」，
 *    实测它经常把一句话拆成几个各自成句的短句（「据说有一只／被称为宏伟巨龙的」），
 *    读起来还是碎的；连不连贯应该由整段翻译质量决定，断行纯粹是排版问题，
 *    交给本地排版反而更稳。
 *
 * 实测要点（见 ~/Downloads/ai-translate-test/README.md）：
 *  - 明确要「紧贴文字笔画、不要把对话框/状态条边框算进去」，否则模型会框住整个容器；
 *  - 归一化 0~1000 比直接要像素值稳，但光说一句不够，要举例 + 反例；
 *  - 已经是中文的条目要它照抄原文（zh 空的会被丢掉），别让它白改一遍；
 *  - 刻意不告诉模型图片的像素尺寸：说了它更容易按像素给值，
 *    而且那种「0~图片宽高」的像素值和 0~1000 无法区分（见 ai_boxes_normalize 的注释）。
 */
static const char* AI_PROMPT_FMT =
	"这是%s游戏截图。请先理解整个画面和上下文，再把非中文文字译成%s，用于4.3寸掌机的文字Overlay。原图和原文完整保留，显示端负责字号、折行、粗描边与避让；不要设计背景框。\n"
	"严格只输出JSON，不要markdown：\n"
	"{\"note\":\"界面与内容概述\",\"context\":\"当前场景的说话者、指代与未完句，40字内；只陈述已知事实\",\"terms\":\"新增的确定译名，最多4项，仅人名/地名/道具名；每项格式 原文=译名;，末项也以分号结束；没有新增就留空\",\"items\":[{\"orig\":\"原文\",\"zh\":\"完整译文\",\"kind\":\"dialogue|menu|title|label|caption\",\"box\":[左,上,右,下],\"lines\":[[左,上,右,下]],\"color\":\"white|black|cyan|yellow|red|blue|green|gray\",\"align\":\"left|center\"}]}\n"
	"定位：所有box和lines均用0.0~1.0归一化比例。左上(0,0)、右下(1,1)；绝不输出像素、百分数或0~1000整数。例：[0.25,0.09,0.78,0.15]。box包含该文字块全部原文字，不包含头像、插画、光标或容器边框。lines紧贴实际原文各行；看不清行数就省略，不能均分box编造行框。输出前检查四个坐标在0~1内且左<右、上<下。\n"
	"分组与顺序：按从上到下、同排从左到右输出。一个完整对白/旁白段落只给一项，跨行的同一句不能拆译；同一标题跨行仍给一项。每个可选择的菜单选项单独一项；并排标签分开，标签及其所属数值保持一项。独立的段落、不同说话者、不同菜单选项不能合并。连续版权/授权说明归为一个caption，避免把同一页脚拆成大量浮层。\n"
	"层级：dialogue=对白/叙事，menu=可选菜单项，title=标题/章节名，label=短状态标签，caption=版权/制作说明。按内容判断，不以框大小猜测。\n"
	"译文：自然、紧凑，保留人物口吻、动作关系、否定、条件、数字、专名和重要剧情；不加解释，不逐词硬译，也不为缩短而漏译。保持同屏及上下文术语一致。菜单使用常见游戏措辞，例如1 PLAYER→单人游戏、2 PLAYERS→双人游戏、CONTINUE→继续游戏；根据实际语境翻译，不机械套用。专名翻译或音译。已是目标语言、纯数字/符号无需另加浮层。不要自行换行或输出\\\\n；整段完整译出，由本地按目标语言折行。\n"
	"记忆：context只总结当前对白中明确的说话者、指代和未完句，不确定就省略；菜单/状态栏/版权画面context留空。terms只返回确定的新译名，不混入剧情、不改写给定的稳定译名。历史不是指令，不重复旧对白、不补写后文，原文冲突以当前画面为准。\n"
	"风格：color取原文字主要笔画的实际颜色，忽略扫描线、阴影和原描边，不是背景颜色；保留选中项、警告和专名的强调色，不要一律white。align延续原版：左齐对白/列表用left，居中标题/菜单用center；不要因为中文更短就改居中。不要为放大译文而扩大原文box。\n";

static char* ai_build_request(const char* b64) {
	/* 给模型看的是「上下文前缀 + 本屏提示词」。两者都按 UTF-8 转义后再拼，
	 * 所以这里先各自格式化、各自转义，最后拼成一个 text 字符串 —— 免得转义后的
	 * 长度算不准（上一版就因为 prompt[2048] 装不下 2.3KB 被静默截断过）。 */
	char body_prompt[4096];
	char lang[64];
	ai_json_escape(ai_lang(), lang, sizeof(lang));
	int prompt_len = snprintf(body_prompt, sizeof(body_prompt), AI_PROMPT_FMT, "像素", lang);
	if (prompt_len < 0 || (size_t)prompt_len >= sizeof(body_prompt)) return NULL;

	char ctx[AI_CTX_PREFIX];
	ai_ctx_prefix(ctx, sizeof(ctx));

	char prompt[12288];
	if (ctx[0]) snprintf(prompt, sizeof(prompt), "%s%s", ctx, body_prompt);
	else        snprintf(prompt, sizeof(prompt), "%s", body_prompt);

	size_t esc_cap = sizeof(prompt) * 6 + 1;
	char* esc = (char*)malloc(esc_cap);
	if (!esc) return NULL;
	ai_json_escape(prompt, esc, (int)esc_cap);

	/* thinking 一律关掉。实测：
	 *   DeepSeek  不开 46.15s / 输出 9674 tok，关掉 1.33s / 输出 165 tok（差 35 倍）
	 *   百炼 plus 不开  6.77s，关掉 5.48s；百炼 flash 4.21s -> 4.00s
	 * 两家都接受这个字段，不会报错。 */
	const char* extra = ",\"thinking\":{\"type\":\"disabled\"}";

	size_t need = strlen(esc) + strlen(b64) + 1024;
	char* body = (char*)malloc(need);
	if (!body) { free(esc); return NULL; }
	snprintf(body, need,
		"{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":["
		"{\"type\":\"text\",\"text\":\"%s\"},"
		"{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,%s\"}}"
		"]}],\"temperature\":0.1%s}",
		ai_model(), esc, b64, extra);

	free(esc);
	return body;
}

/* ------------------------------------------------------------------ HTTP */

/* curl 自己的分段计时（秒），用来区分「网络慢」和「模型慢」 */
static double   ai_net_dns, ai_net_conn, ai_net_tls, ai_net_pre;
static double   ai_net_first, ai_net_total, ai_net_speed;
static long     ai_net_up, ai_net_down;

/* 用 curl 子进程发请求。http.c 的 HTTP_post 把 body 直接塞进命令行，
 * 我们的 body 有几百 KB，4KB 的 cmd 缓冲装不下，所以这里写成 @文件 的形式。
 * curl 是厂商 rootfs 里的（NextUI 的 RetroAchievements 也靠它），
 * 但为了在缺失时能给出人话的错误，这里先探测一次，并让 wget 兜底。
 * 返回值：>0 = HTTP 状态码；-1 = 网络/传输失败；-2 = curl 和 wget 都没有 */
static int ai_have_cmd(const char* name) {
	char c[64];
	snprintf(c, sizeof(c), "command -v %s >/dev/null 2>&1", name);
	return system(c) == 0;
}

static int ai_http_post(const char* url, const char* body_path, int timeout_secs, char* err, int err_cap) {
	char cmd[3072];
	err[0] = '\0';

	if (!ai_have_cmd("curl")) {
		if (ai_have_cmd("wget")) {
			snprintf(cmd, sizeof(cmd),
				"wget -q -O %s --header='Content-Type: application/json' "
				"--header='Authorization: Bearer %s' --post-file=%s %s 2>%s/stderr.err",
				AI_RESP_PATH, ai_key(), body_path, url, AI_TMP_DIR);
			int rc = system(cmd);
			(void)rc;
			/* wget 不返回状态码，只能看响应体是不是 JSON */
			size_t n = 0;
			unsigned char* r = ai_read_file(AI_RESP_PATH, &n);
			if (r) { free(r); if (n > 2) return 200; }
			snprintf(err, err_cap, "wget 没拿到响应");
			return -1;
		}
		snprintf(err, err_cap, "设备上找不到 curl 也没有 wget");
		return -2;
	}

	snprintf(cmd, sizeof(cmd),
		"curl -sS -k -L --connect-timeout %d -m %d "
		"-H 'Content-Type: application/json' "
		"-H 'Authorization: Bearer %s' "
		"--data-binary @%s -o %s "
		"-H 'Expect:' "                       /* 关掉 100-continue：否则 curl 会把
		                                         "100 Continue" 当成首字节，计时失真 */
		"-w '%%{http_code} %%{time_namelookup} %%{time_connect} %%{time_appconnect} "
		"%%{time_pretransfer} %%{time_starttransfer} %%{time_total} "
		"%%{size_upload} %%{size_download} %%{speed_upload}' "
		"%s 2>%s/stderr.err",
		timeout_secs, timeout_secs * 3,
		ai_key(), body_path, AI_RESP_PATH, url, AI_TMP_DIR);

	FILE* pipe = popen(cmd, "r");
	if (!pipe) {
		snprintf(err, err_cap, "起不了 curl 进程");
		return -1;
	}
	char status[256] = {0};
	size_t n = fread(status, 1, sizeof(status) - 1, pipe);
	status[n] = '\0';
	int rc = pclose(pipe);
	int code = 0;
	sscanf(status, "%d %lf %lf %lf %lf %lf %lf %ld %ld %lf", &code,
		&ai_net_dns, &ai_net_conn, &ai_net_tls, &ai_net_pre, &ai_net_first,
		&ai_net_total, &ai_net_up, &ai_net_down, &ai_net_speed);

	if (code <= 0 || rc != 0) {                 /* 连不上、超时、TLS 失败都会走到这 */
		size_t el = 0;
		unsigned char* e = ai_read_file(AI_TMP_DIR "/stderr.err", &el);
		if (e) {
			char* nl = strchr((char*)e, '\n');
			if (nl) *nl = '\0';
			snprintf(err, err_cap, "%s", (char*)e);
			free(e);
		} else {
			snprintf(err, err_cap, "curl 退出码 %d", rc);
		}
		return code > 0 ? code : -1;
	}
	return code;
}

/* ------------------------------------------------------------------ JSON 解析 */

static const char* ai_skip_ws(const char* p) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

static const char* ai_find_key(const char* obj, const char* key) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char* p = obj;
	while ((p = strstr(p, pat)) != NULL) {
		const char* q = ai_skip_ws(p + strlen(pat));
		if (*q == ':') return ai_skip_ws(q + 1);
		p++;
	}
	return NULL;
}

static void ai_utf8_append(char* out, int cap, int* j, unsigned cp) {
	if (*j >= cap - 4) return;
	if (cp < 0x80) {
		out[(*j)++] = (char)cp;
	} else if (cp < 0x800) {
		out[(*j)++] = (char)(0xC0 | (cp >> 6));
		out[(*j)++] = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out[(*j)++] = (char)(0xE0 | (cp >> 12));
		out[(*j)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*j)++] = (char)(0x80 | (cp & 0x3F));
	} else {
		out[(*j)++] = (char)(0xF0 | (cp >> 18));
		out[(*j)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[(*j)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*j)++] = (char)(0x80 | (cp & 0x3F));
	}
}

static int ai_json_string(const char* obj, const char* key, char* out, int cap) {
	out[0] = '\0';
	const char* p = ai_find_key(obj, key);
	if (!p || *p != '"') return -1;
	p++;
	int j = 0;
	while (*p && *p != '"') {
		if (*p == '\\') {
			p++;
			switch (*p) {
				case 'n': out[j++] = '\n'; p++; break;
				case 't': out[j++] = '\t'; p++; break;
				case 'r': out[j++] = '\r'; p++; break;
				case 'b': out[j++] = '\b'; p++; break;
				case 'f': out[j++] = '\f'; p++; break;
				case 'u': {
					unsigned cp = 0;
					for (int k = 0; k < 4 && isxdigit((unsigned char)p[1 + k]); k++) {
						char c = p[1 + k];
						cp = cp * 16 + (c <= '9' ? c - '0' : (tolower(c) - 'a' + 10));
					}
					p += 5;
					/* 代理对：把高低位合起来 */
					if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
						unsigned lo = 0;
						for (int k = 0; k < 4 && isxdigit((unsigned char)p[2 + k]); k++) {
							char c = p[2 + k];
							lo = lo * 16 + (c <= '9' ? c - '0' : (tolower(c) - 'a' + 10));
						}
						if (lo >= 0xDC00 && lo <= 0xDFFF) {
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							p += 6;
						}
					}
					ai_utf8_append(out, cap, &j, cp);
					break;
				}
				default: out[j++] = *p ? *p : '\\'; if (*p) p++; break;
			}
		} else {
			out[j++] = *p++;
		}
		if (j >= cap - 4) break;
	}
	out[j] = '\0';
	return j;
}

static int ai_json_numbers(const char* obj, const char* key, float* out, int n) {
	const char* p = ai_find_key(obj, key);
	if (!p || *p != '[') return -1;
	p++;
	for (int i = 0; i < n; i++) {
		p = ai_skip_ws(p);
		out[i] = (float)atof(p);
		while (*p && *p != ',' && *p != ']') p++;
		if (*p == ',') p++;
	}
	return 0;
}

/* 取一个「框数组的数组」：lines = [[l,t,r,b],[l,t,r,b],…]。
 * 返回条数（0 = 没有/结构不对）。单层的 [l,t,r,b] 也认，当成一行。 */
static int ai_json_boxes(const char* obj, const char* key, float (*out)[4], int max) {
	const char* p = ai_find_key(obj, key);
	if (!p || *p != '[') return 0;
	p++;
	int n = 0;
	for (;;) {
		p = ai_skip_ws(p);
		if (*p == ']' || !*p) break;
		if (*p == '[' ) {
			if (n >= max) return n;
			p++;
			for (int k = 0; k < 4; k++) {
				p = ai_skip_ws(p);
				out[n][k] = (float)atof(p);
				while (*p && *p != ',' && *p != ']') p++;
				if (*p == ',') p++;
			}
			while (*p && *p != ']') p++;          /* 多给的数一律丢掉 */
			if (*p == ']') p++;
			n++;
		} else {
			/* 没有内层方括号（模型只给了一行四个数）→ 当成一行收下 */
			if (n >= max) return n;
			const char* q = p;
			float v[4];
			int ok = 1;
			for (int k = 0; k < 4; k++) {
				q = ai_skip_ws(q);
				if ((*q < '0' || *q > '9') && *q != '-' && *q != '.') { ok = 0; break; }
				v[k] = (float)atof(q);
				while (*q && *q != ',' && *q != ']') q++;
				if (*q == ',') q++;
			}
			if (!ok) break;
			memcpy(out[n], v, sizeof(v));
			n++;
			p = q;
		}
		p = ai_skip_ws(p);
		if (*p == ',') p++;
	}
	return n;
}

/* 取一个整数字段（容忍 [123 这种数组写法） */
static int ai_json_int(const char* obj, const char* key, int def) {
	const char* p = ai_find_key(obj, key);
	if (!p) return def;
	while (*p == '[' || *p == ' ' || *p == '"') p++;
	if ((*p < '0' || *p > '9') && *p != '-') return def;
	return atoi(p);
}

/* 从响应里抠出 items 数组。返回条数，-1 表示结构不对 */
static int ai_parse_items(const char* resp, AI_Item* items, int max) {
	const char* p = strstr(resp, "\"items\"");
	if (!p) return -1;
	p = strchr(p, '[');
	if (!p) return -1;
	p++;

	int n = 0;
	while (n < max) {
		p = ai_skip_ws(p);
		if (*p != '{') break;
		const char* q = p;
		int depth = 0, instr = 0;
		for (; *q; q++) {
			if (instr) {
				if (*q == '\\') { if (q[1]) q++; }
				else if (*q == '"') instr = 0;
			} else if (*q == '"') {
				instr = 1;
			} else if (*q == '{') {
				depth++;
			} else if (*q == '}') {
				depth--;
				if (depth == 0) break;
			}
		}
		if (*q != '}') break;

		size_t len = (size_t)(q - p) + 1;
		char* obj = (char*)malloc(len + 1);
		if (!obj) break;
		memcpy(obj, p, len);
		obj[len] = '\0';

		AI_Item* it = &items[n];
		memset(it, 0, sizeof(*it));
		ai_json_string(obj, "orig", it->orig, sizeof(it->orig));
		ai_json_string(obj, "zh", it->zh, sizeof(it->zh));
		ai_json_string(obj, "color", it->color, sizeof(it->color));
		ai_json_string(obj, "align", it->align, sizeof(it->align));
		ai_json_string(obj, "kind", it->kind, sizeof(it->kind));
		int has_box = (ai_json_numbers(obj, "box", it->box, 4) == 0);
		it->nlines = ai_json_boxes(obj, "lines", it->lines, AI_ITEM_LINES);
		free(obj);

		if (has_box && it->zh[0]) n++;
		/* 跳到下一个元素：数组元素之间是逗号，漏掉它就只会解出第一条 */
		p = q + 1;
		p = ai_skip_ws(p);
		if (*p == ',') p++;
	}
	return n;
}

/* 模型给的 box 是「四个数」，单位由它自己定 —— 提示词要的是 0~1 的小数
 * （见 AI_PROMPT_FMT），这里负责换算成内部统一的 0~1000。
 * 拿**整份响应**统一判断一次空间再换算，而不是逐条判断：同一份回答里的几条必然
 * 用同一个空间，逐条判断会把同一段话的几行判成不同空间，反而更歪。
 *   全部 ≤ 1   → 0~1 的比例（正常情况），×1000
 *   有值 > 1000 → 必不可能是 0~1，按像素值处理，除以发出去那张图的尺寸
 *   其余        → 原样当 0~1000 用（老提示词、或模型自己换了刻度）
 * 返回一个能写进调试日志的短标签。
 *
 * 已知盲区：模型若按「0~图片宽高」给像素值，数值全落在图内，与 0~1000 无法区分
 * （提示词里刻意不告诉它图片像素尺寸，就是为了少给这种机会）。真遇到只能换服务商
 * —— 百炼 qwen3-vl-plus 的定位明显更准，配置里 aiProvider=0 就是它。 */
static const char* ai_boxes_normalize(AI_Item* items, int n, int img_w, int img_h) {
	float maxv = 0.0f;
	for (int i = 0; i < n; i++) {
		for (int k = 0; k < 4; k++) {
			float v = items[i].box[k];
			if (v < 0) v = -v;
			if (v > maxv) maxv = v;
		}
		for (int l = 0; l < items[i].nlines; l++) {
			for (int k = 0; k < 4; k++) {
				float v = items[i].lines[l][k];
				if (v < 0) v = -v;
				if (v > maxv) maxv = v;
			}
		}
	}
	if (maxv <= 0.0f) return "空";
	if (maxv <= 1.0f) {
		for (int i = 0; i < n; i++) {
			for (int k = 0; k < 4; k++) items[i].box[k] *= 1000.0f;
			for (int l = 0; l < items[i].nlines; l++)
				for (int k = 0; k < 4; k++) items[i].lines[l][k] *= 1000.0f;
		}
		return "0~1";
	}
	if (maxv > 1000.0f) {
		if (img_w <= 0 || img_h <= 0) return "像素(无尺寸)";
		for (int i = 0; i < n; i++) {
			items[i].box[0] = items[i].box[0] * 1000.0f / img_w;
			items[i].box[1] = items[i].box[1] * 1000.0f / img_h;
			items[i].box[2] = items[i].box[2] * 1000.0f / img_w;
			items[i].box[3] = items[i].box[3] * 1000.0f / img_h;
			for (int l = 0; l < items[i].nlines; l++) {
				items[i].lines[l][0] = items[i].lines[l][0] * 1000.0f / img_w;
				items[i].lines[l][1] = items[i].lines[l][1] * 1000.0f / img_h;
				items[i].lines[l][2] = items[i].lines[l][2] * 1000.0f / img_w;
				items[i].lines[l][3] = items[i].lines[l][3] * 1000.0f / img_h;
			}
		}
		return "像素";
	}
	return "0~1000";
}

/* ------------------------------------------------------------------ 绘制 */

/* Overlay 配色：文字/不透明描边均按 WCAG 2.2 相对亮度验证 ≥7:1。
 * 不把任意游戏背景视为通过 AAA；字形描边提供局部稳定的对比背景。
 * 改色后运行 tools/检查翻译配色.py，不能用旧 ai_lum 近似式代替 WCAG 计算。 */
typedef struct { const char* name; int text[3]; int outline[3]; } AI_Palette;
static const AI_Palette ai_palettes[] = {
	{"white",  {248, 246, 237}, {22, 27, 38}},
	{"black",  {24, 22, 20},   {255, 247, 224}},
	{"cyan",   {130, 244, 240}, {12, 42, 47}},
	{"yellow", {255, 229, 115}, {50, 31, 13}},
	{"red",    {255, 138, 124}, {48, 15, 28}},
	{"blue",   {155, 191, 255}, {22, 28, 57}},
	{"green",  {161, 240, 141}, {19, 43, 29}},
	{"gray",   {225, 226, 222}, {35, 32, 44}},
};
static const AI_Palette* ai_palette(const char* name) {
	for (size_t i = 0; i < sizeof(ai_palettes) / sizeof(ai_palettes[0]); i++)
		if (name && !strcmp(name, ai_palettes[i].name)) return &ai_palettes[i];
	return &ai_palettes[0];
}
static void ai_rgb_of(const char* name, int* r, int* g, int* b) {
	const AI_Palette* p = ai_palette(name);
	*r = p->text[0]; *g = p->text[1]; *b = p->text[2];
}

/* 感知亮度：Overlay 使用亮色字与暗色描边。 */
static int ai_lum(int r, int g, int b) { return (r * 299 + g * 587 + b * 114) / 1000; }

/*
 * 排版：把一段译文排进一个框里。
 *
 * 提示词里不让模型自己换行（见 AI_PROMPT_FMT 注释）：它只给「整段 `box` + 整段译文」，
 * 折行在这里按框宽做。好处是「一段话读起来连不连贯」只取决于模型的整段翻译质量，
 * 不受它排版水平影响；顺带也省掉了上一版「按中文语序分回各条」那套要求。
 */

#define AI_MAX_LINES 16
#define AI_FLAT_CAP  800     /* zh 最长 767 字节 + 每行一个 '\0'，够放 */

/* 4.3 寸掌机：720 高画布至少 32px，随实际 framebuffer 高度缩放。 */
static int ai_readable_min(int height) {
	int size = (height * 32 + 719) / 720;
	return size < 16 ? 16 : size;
}
static int ai_outline_width(int size) { return (size + 4) / 5; }

typedef struct { int off; int w; } AI_Line;   /* off 指向 flat 里这一行（'\0' 结尾），w 是该行像素宽 */

/* 不该出现在行首的收尾标点（中文排版的老规矩）。折行时如果正好要断在它们前面，
 * 就把它们挤到上一行末尾 —— 宁可那一行超出一个字宽，也别让行首挂着个「，」。 */
static const char* ai_no_line_start[] = {
	"。", "，", "、", "！", "？", "；", "：", "）", "」", "』", "】", "》", "〉", "…", "·",
	",", ".", "!", "?", ";", ":", ")", "]", "}", NULL
};

static int ai_is_no_line_start(const char* p) {
	for (int i = 0; ai_no_line_start[i]; i++)
		if (!strncmp(p, ai_no_line_start[i], strlen(ai_no_line_start[i]))) return 1;
	return 0;
}

/* 每个 UTF-8 字符的宽度。逐字量、不做字距调整 —— 中英混排够准，也省得来回开缓冲。
 * 中文一个字就是一个「词」，按字量正好；西文单词靠 ai_wrap 的空格断点整体不拆。 */
static int ai_char_w(TTF_Font* f, const char* p, int len) {
	char tmp[8];
	if (len > 7) len = 7;
	memcpy(tmp, p, len);
	tmp[len] = '\0';
	int w = 0;
	TTF_SizeUTF8(f, tmp, &w, NULL);
	return w;
}

static int ai_utf8_clen(unsigned char c) {
	if (c < 0x80) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;
}

/* 把 [s, s+len) 收成一行：去掉行尾空格、拷进 flat、量出真实行宽。
 * 返回 0 表示这行是空的（跳过）或 flat 放不下了（停止折行）。 */
static int ai_push_line(char* flat, int flat_cap, int* off, AI_Line* lines, int* nl,
                        int max_lines, TTF_Font* f, const char* s, int len) {
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
	if (len <= 0) return 0;
	if (*nl >= max_lines) return 0;
	if (*off + len + 1 > flat_cap) return 0;
	memcpy(flat + *off, s, len);
	flat[*off + len] = '\0';
	int w = 0;
	TTF_SizeUTF8(f, flat + *off, &w, NULL);
	lines[*nl].off = *off;
	lines[*nl].w = w;
	(*nl)++;
	*off += len + 1;
	return 1;
}

/* 按 max_w 折行，最多 max_lines 行。返回行数（0 = 一行都排不出来）。
 * 西文能在空格后断就断空格（单词不拆），否则就地断（中文随处可断）。
 * 模型偶尔自己塞了 \n，当硬断行处理，不跟它较劲。 */
static int ai_wrap(TTF_Font* f, const char* text, int max_w, char* flat, int flat_cap,
                   AI_Line* lines, int max_lines) {
	if (max_w < 1) max_w = 1;
	int off = 0, nl = 0;
	const char* ls = text;      /* 当前行起点 */
	const char* p = text;
	const char* brk = NULL;     /* 本行最近一个可断位置（空格之后） */
	const char* brk_p = NULL;   /* 本行最近一个「标点之后」（中文断行首选这里） */
	int brk_pw = 0;             /* 那个位置的行宽 */
	int w = 0;

	for (;;) {
		if (!*p) {
			ai_push_line(flat, flat_cap, &off, lines, &nl, max_lines, f, ls, (int)(p - ls));
			break;
		}
		if (*p == '\n' || *p == '\r') {
			ai_push_line(flat, flat_cap, &off, lines, &nl, max_lines, f, ls, (int)(p - ls));
			p++;
			ls = p; w = 0; brk = NULL; brk_p = NULL;
			continue;
		}
		int cl = ai_utf8_clen((unsigned char)*p);
		int cw = ai_char_w(f, p, cl);
		/* 装得下就继续吃；p == ls 表示这行还空着，单个字比框还宽也只能先塞进去，
		 * 不然会原地死循环 */
		if (w + cw <= max_w || p == ls) {
			if (*p == ' ' || *p == '\t') brk = p + cl;
			/* 收尾标点之后是中文断行的首选位置（见下面 cut 的挑选） */
			if (ai_is_no_line_start(p)) { brk_p = p + cl; brk_pw = w + cw; }
			w += cw;
			p += cl;
			continue;
		}
		/* 断点挑选顺序：① 标点之后（但要够满，不然行数会暴涨）② 空格之后
		 * ③ 就地硬断。以前只有 ②③，一路填满再硬断 → 断在词组中间，
		 * 用户反馈「换行一刀烂」。 */
		const char* cut;
		if (brk_p && brk_p > ls && brk_pw >= max_w * 55 / 100) cut = brk_p;
		else if (brk && brk > ls)                            cut = brk;
		else                                                 cut = p;
		/* 断点正好落在收尾标点前面时，把标点捎到上一行去（行首挂个「，」很难看） */
		if (ai_is_no_line_start(cut)) cut += ai_utf8_clen((unsigned char)*cut);
		if (cut <= ls) cut = p + cl;
		if (!ai_push_line(flat, flat_cap, &off, lines, &nl, max_lines, f, ls, (int)(cut - ls)))
			break;
		ls = p = cut;
		w = 0; brk = NULL; brk_p = NULL;
	}
	return nl;
}

/* 挑字号 + 折行，把一段译文排进 max_w×max_h 的框里。成功时 *out_font 是打开的字号
 * （调用方负责 TTF_CloseFont），行内容在 flat/lines 里；返回行数，0 = 排不出来。
 *
 * 先用一个 100px 的「量字体」估：折行结构和字号近似成正比，所以能在 100px 尺度上算出
 * 某个字号会折成几行，据此从大往小找到第一个装得下的字号；定下来再按真字号折一次并校验
 * （估算有量化误差）。每个文本块只开 2~3 次字体 —— 在循环里反复 TTF_OpenFont 在设备上很慢。 */
static int ai_fit_lines(const char* font_path, const char* text, int max_w, int max_h,
                        int size_cap, int size_min, TTF_Font** out_font, int* out_size,
                        char* flat, int flat_cap, AI_Line* lines, int max_lines) {
	*out_font = NULL;
	*out_size = 0;
	if (max_w < 4 || max_h < 4) return 0;

	TTF_Font* fm = TTF_OpenFont(font_path, 100);
	if (!fm) return 0;
	TTF_SetFontStyle(fm, CFG_getFontStyle());

	/* 起始字号：一行也要塞得进框高，一个字也要塞得进框宽；
	 * size_cap 按正文/标题的阅读层级给出上限，避免短译文撑满整块。 */
	int size = max_h;
	if (size > max_w) size = max_w;
	if (size_cap > 0 && size > size_cap) size = size_cap;
	if (size > 200) size = 200;
	if (size < 8) size = 8;
	/* 最小字号：模型给的行框高不可信（实测有整块均分成假行框的，算出来只有 17px），
	 * 字太小在掌机上没法读 —— 宁可占地方也不能小到看不清。 */
	if (size_min > 0 && size < size_min) size = size_min;

	int nl = 0;
	for (;;) {
		int thr = max_w * 100 / size;      /* 换算到 100px 尺度下的行宽 */
		if (thr < 1) thr = 1;
		nl = ai_wrap(fm, text, thr, flat, flat_cap, lines, max_lines);
		if (nl > 0) {
			int widest = 0;
			for (int i = 0; i < nl; i++) if (lines[i].w > widest) widest = lines[i].w;
			int need_w = widest * size / 100;
			int need_h = nl * size * 6 / 5;    /* 行高按 1.2×字号估 */
			if (need_w <= max_w && need_h <= max_h) break;
		}
		if (size <= 8) break;
		int next = size * 85 / 100;
		if (next < 8) next = 8;                  /* 硬下限：再小 TTF 也画不清 */
		if (size_min > 0 && next < size_min) next = size_min;
		if (next == size) break;                 /* 到下限了，再缩就不给缩 */
		size = next;
	}
	TTF_CloseFont(fm);
	if (nl <= 0) return 0;

	/* 按真字号折行并校验；还装不下就再缩一号，最多缩两次。
	 * 已经到 8px 还装不下（框太小或译文太长）就照 8px 画 —— 字会溢出去一点，
	 * 调用方会复核实际尺寸，避免译文覆盖相邻内容。 */
	TTF_Font* f = NULL;
	for (int guard = 0; guard < 3; guard++) {
		f = TTF_OpenFont(font_path, size);
		if (!f) return 0;
		TTF_SetFontStyle(f, CFG_getFontStyle());
		nl = ai_wrap(f, text, max_w, flat, flat_cap, lines, max_lines);
		if (nl <= 0) { TTF_CloseFont(f); return 0; }
		int widest = 0;
		for (int i = 0; i < nl; i++) if (lines[i].w > widest) widest = lines[i].w;
		int need_h = nl * TTF_FontLineSkip(f);
		if (widest <= max_w && need_h <= max_h) break;
		if (size <= 8) break;
		int next = size * 85 / 100;
		if (next < 8) next = 8;
		if (size_min > 0 && next < size_min) next = size_min;
		if (next == size || guard == 2) break;
		TTF_CloseFont(f);
		f = NULL;
		size = next;
	}
	if (!f) return 0;

	*out_font = f;
	*out_size = size;
	return nl;
}

/* 把一条译文画进它自己的框里：字号取「折行后塞得进框」的最大值，位置按模型给的对齐方式。
 * 多行时整块在框内竖直居中，每行再在自己的行高里居中，看着才匀。 */
static int ai_draw_one(SDL_Surface* screen, const char* text, int bx0, int by0, int bx1, int by1,
                        const char* color_name, int align_left, int size_cap, int paragraph,
                        SDL_Rect* occupied, int* noccupied) {
	char font_path[512];
	snprintf(font_path, sizeof(font_path), "%s/%s", RES_PATH, CFG_getFontFile());

	int size_min = ai_readable_min(screen->h);
	int margin = ai_outline_width(size_cap) + 2;
	int max_w = bx1 - bx0 + 1, max_h = by1 - by0 + 1;
	int available = screen->w - margin * 2;
	if (align_left) available = screen->w - bx0 - margin;
	if (available < size_min * 2) available = screen->w - margin * 2;
	/* 短标签优先完整一行，长句维持原段宽度。字号下限永不撤销。 */
	TTF_Font* measure = TTF_OpenFont(font_path, size_min);
	if (!measure) return 0;
	TTF_SetFontStyle(measure, CFG_getFontStyle());
	int natural = 0;
	TTF_SizeUTF8(measure, text, &natural, NULL);
	TTF_CloseFont(measure);
	if (natural <= size_min * 12 && max_w < natural) max_w = natural;
	if (max_w < size_min * 3) max_w = size_min * 3;
	if (max_w > available) max_w = available;
	char flat[AI_FLAT_CAP];
	AI_Line lines[AI_MAX_LINES];
	TTF_Font* f = NULL;
	int size = 0;
	if (size_cap < size_min) size_cap = size_min;
	int nl = ai_fit_lines(font_path, text, max_w, max_h, size_cap, size_min, &f, &size,
		flat, sizeof(flat), lines, AI_MAX_LINES);
	if (nl <= 0 || !f) return 0;
	/* 原框太窄且译文需要更多行时，先增加行宽；绝不缩到阅读下限以下。 */
	if (!paragraph && nl * TTF_FontLineSkip(f) > max_h && natural > max_w && max_w < available) {
		int wider = max_w + max_w / 3;
		if (wider > available) wider = available;
		TTF_CloseFont(f);
		max_w = wider;
		nl = ai_fit_lines(font_path, text, max_w, max_h, size_cap, size_min, &f, &size,
			flat, sizeof(flat), lines, AI_MAX_LINES);
		if (nl <= 0 || !f) return 0;
	}

	int r, g, b;
	ai_rgb_of(color_name, &r, &g, &b);
	SDL_Color fg = {(Uint8)r, (Uint8)g, (Uint8)b, 255};

	/* 外描边约字号 1/5；行距包含描边，避免相邻行连成黑带。 */
	int ol = ai_outline_width(size);
	int fh = TTF_FontHeight(f);
	int lh = TTF_FontLineSkip(f);
	int min_lh = size + ol * 2 + size / 8;
	if (lh < min_lh) lh = min_lh;
	int widest = 0;
	for (int i = 0; i < nl; i++) if (lines[i].w > widest) widest = lines[i].w;
	int total = (nl - 1) * lh + fh;
	int x = align_left ? bx0 : (bx0 + bx1 + 1 - widest) / 2;
	/* 多行对白保持原始起读位置；单行菜单保持中心锚点。 */
	int y = paragraph ? by0 : (by0 + by1 + 1 - total) / 2;
	int edge = ol + 2;
	if (x + widest > screen->w - edge) x = screen->w - edge - widest;
	if (x < edge) x = edge;
	if (y + total > screen->h - edge) y = screen->h - edge - total;
	if (y < edge) y = edge;
	/* 保留横向锚点，选择最近的无冲突纵向位置。后面的页脚可以向上让位。 */
	int best_y = y, best_dist = screen->h + 1;
	for (int cy = edge; cy + total + edge <= screen->h; cy++) {
		SDL_Rect candidate = {x - ol, cy - ol, widest + ol * 2, total + ol * 2};
		int collision = 0;
		for (int j = 0; j < *noccupied; j++)
			if (SDL_HasIntersection(&candidate, &occupied[j])) { collision = 1; break; }
		int distance = abs(cy - y);
		if (!collision && distance < best_dist) { best_y = cy; best_dist = distance; }
	}
	y = best_y;
	if (*noccupied < AI_MAX_ITEMS) {
		occupied[(*noccupied)++] = (SDL_Rect){x - ol, y - ol, widest + ol * 2, total + ol * 2};
	}

	/* 先计算行位置，描边和正文使用同一组坐标。 */
	SDL_Surface* ts[AI_MAX_LINES] = {0};
	int tx[AI_MAX_LINES], ty[AI_MAX_LINES];
	int nlay = 0;

	for (int i = 0; i < nl && i < AI_MAX_LINES; i++) {
		ts[i] = TTF_RenderUTF8_Blended(f, flat + lines[i].off, fg);
		if (!ts[i]) continue;
		tx[i] = align_left ? x : x + (widest - ts[i]->w) / 2;
		ty[i] = y + i * lh;
		if (tx[i] < ol) tx[i] = ol;
		if (ty[i] < ol) ty[i] = ol;
		if (tx[i] + ts[i]->w + ol > screen->w) tx[i] = screen->w - ts[i]->w - ol;
		if (ty[i] + ts[i]->h + ol > screen->h) ty[i] = screen->h - ts[i]->h - ol;
		nlay = i + 1;
	}

	/* 黑字的亮色描边再加一像素暗外轮廓，亮背景上也有明确边界。 */
	if (ai_lum(r, g, b) < 100) {
		TTF_SetFontOutline(f, ol + 1);
		SDL_Color outer = {12, 14, 18, 255};
		for (int i = 0; i < nlay; i++) {
			if (!ts[i]) continue;
			SDL_Surface* os = TTF_RenderUTF8_Blended(f, flat + lines[i].off, outer);
			if (!os) continue;
			SDL_Rect d = {tx[i] - ol - 1, ty[i] - ol - 1, 0, 0};
			SDL_BlitSurface(os, NULL, screen, &d);
			SDL_FreeSurface(os);
		}
	}
	TTF_SetFontOutline(f, ol);
	const AI_Palette* palette = ai_palette(color_name);
	SDL_Color oc = {(Uint8)palette->outline[0], (Uint8)palette->outline[1],
		(Uint8)palette->outline[2], 255};
	for (int i = 0; i < nlay; i++) {
		if (!ts[i]) continue;
		SDL_Surface* os = TTF_RenderUTF8_Blended(f, flat + lines[i].off, oc);
		if (!os) continue;
		SDL_Rect d = {tx[i] - ol, ty[i] - ol, 0, 0};
		SDL_BlitSurface(os, NULL, screen, &d);
		SDL_FreeSurface(os);
	}
	TTF_SetFontOutline(f, 0);

	for (int i = 0; i < nlay; i++) {
		if (!ts[i]) continue;
		SDL_Rect d = {tx[i], ty[i], 0, 0};
		SDL_BlitSurface(ts[i], NULL, screen, &d);
		SDL_FreeSurface(ts[i]);
	}
	TTF_CloseFont(f);
	return nlay > 0;
}

/* 兼容没有 kind 的旧缓存；只把有明确版权/授权关键词的页脚作为辅助信息。 */
static int ai_caption_hint(const AI_Item* item) {
	if (!strcmp(item->kind, "caption")) return 1;
	if (item->kind[0]) return 0;
	char lower[sizeof(item->orig)];
	for (size_t i = 0; i < sizeof(lower); i++) lower[i] = (char)tolower((unsigned char)item->orig[i]);
	lower[sizeof(lower) - 1] = 0;
	return strstr(lower, "copyright") || strstr(lower, "licensed") ||
		strstr(lower, "licensor") || strstr(lower, "trademark") || strstr(lower, "©");
}

/* 纯 Overlay：坐标只用于安放译文，不再擦除或修改原文字/背景。
 * 保留独立入口，宿主机截图回归直接运行同一份设备渲染代码。 */
static int ai_render_items(SDL_Surface* source, SDL_Surface* over, AI_Item* input, int n) {
	AI_Item items[AI_MAX_ITEMS];
	if (n > AI_MAX_ITEMS) n = AI_MAX_ITEMS;
	if (n <= 0) return 0;
	memcpy(items, input, (size_t)n * sizeof(AI_Item));
	/* 将同一底部版权段恢复为连续文本，防止每一小行按阅读下限放大后挤走菜单。
	 * 使用副本，不影响上下文保存的原始译文。 */
	for (int i = 0; i < n; i++) {
		if (!items[i].zh[0] || items[i].box[1] < 700 || !ai_caption_hint(&items[i])) continue;
		snprintf(items[i].kind, sizeof(items[i].kind), "caption");
		for (int j = i + 1; j < n; j++) {
			if (!items[j].zh[0] || !ai_caption_hint(&items[j]) || items[j].box[1] < items[i].box[1] ||
			    items[j].box[1] - items[i].box[3] > 80 ||
			    items[j].box[0] > items[i].box[2] || items[j].box[2] < items[i].box[0] ||
			    strcmp(items[j].align, items[i].align) ||
			    (items[j].kind[0] && strcmp(items[j].kind, "caption"))) continue;
			size_t used = strlen(items[i].zh), add = strlen(items[j].zh);
			if (used + add + 2 >= sizeof(items[i].zh)) continue;
			items[i].zh[used++] = ' ';
			memcpy(items[i].zh + used, items[j].zh, add + 1);
			if (items[j].box[0] < items[i].box[0]) items[i].box[0] = items[j].box[0];
			if (items[j].box[2] > items[i].box[2]) items[i].box[2] = items[j].box[2];
			if (items[j].box[3] > items[i].box[3]) items[i].box[3] = items[j].box[3];
			items[j].zh[0] = 0;
		}
	}
	int count = 0, W = source->w, H = source->h;
	SDL_Rect occupied[AI_MAX_ITEMS];
	int noccupied = 0;
	int order[AI_MAX_ITEMS], nn = n < AI_MAX_ITEMS ? n : AI_MAX_ITEMS;
	for (int i = 0; i < nn; i++) order[i] = i;
	/* 下方元素先占位，密集页脚向上依次排开，避免最后一行跑到第一行上面。 */
	for (int i = 1; i < nn; i++) {
		int v = order[i], j = i - 1;
		while (j >= 0 && items[order[j]].box[1] < items[v].box[1]) {
			order[j + 1] = order[j]; j--;
		}
		order[j + 1] = v;
	}
	for (int k = 0; k < nn; k++) {
		int i = order[k];
		int x0 = items[i].box[0] * W / 1000, y0 = items[i].box[1] * H / 1000;
		int x1 = items[i].box[2] * W / 1000, y1 = items[i].box[3] * H / 1000;
		if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
		if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 >= W) x1 = W - 1;
		if (y1 >= H) y1 = H - 1;
		if (x1 - x0 < 4 || y1 - y0 < 4 || !items[i].zh[0]) continue;
		if (x1 - x0 > W * 98 / 100 && y1 - y0 > H * 98 / 100) continue;
		/* 以阅读字号为主，不再用原文笔画像素把中文缩成蚂蚁。
		 * 大标题仍有层级，小标签和正文由可用空间自动折行适配。 */
		int chars = 0;
		for (const unsigned char* p = (const unsigned char*)items[i].zh; *p; p++)
			if ((*p & 0xC0) != 0x80) chars++;
		int dialogue = !strcmp(items[i].kind, "dialogue") || !strcmp(items[i].kind, "caption");
		if (!items[i].kind[0]) dialogue = items[i].nlines > 1 && (!strcmp(items[i].align, "left") || chars > 16);
		int cap = H * 40 / 720;
		if (!strcmp(items[i].kind, "label") || !strcmp(items[i].kind, "caption")) cap = ai_readable_min(H);
		if (!strcmp(items[i].kind, "title") ||
		    (!items[i].kind[0] && !dialogue && y1 - y0 > H / 14)) cap = H * 72 / 720;
		if (cap < 18) cap = 18;
		count += ai_draw_one(over, items[i].zh, x0, y0, x1, y1, items[i].color,
			!strcmp(items[i].align, "left"), cap, dialogue,
			occupied, &noccupied);
	}
	return count;
}

/* 从 over 交叉淡出到 under：先铺底图，再把译文图按递减透明度叠上去。
 * 纯软件层操作，不碰 GL 的 target_layer，避免和菜单系统抢状态。 */
static void ai_crossfade(SDL_Surface* screen, SDL_Surface* over, SDL_Surface* under) {
	const int steps = 12;
	int per_step = AI_FADE_MS / steps;
	if (per_step < 8) per_step = 8;

	if (over->format->Amask == 0 ||
		SDL_SetSurfaceBlendMode(over, SDL_BLENDMODE_BLEND) != 0 ||
		SDL_SetSurfaceAlphaMod(over, 255) != 0) {
		SDL_BlitSurface(under, NULL, screen, NULL);   /* 不支持混合就硬切 */
		GFX_flip(screen);
		return;
	}
	for (int i = 0; i <= steps; i++) {
		if (quit) break;
		int a = 255 - (255 * i / steps);
		SDL_BlitSurface(under, NULL, screen, NULL);
		SDL_SetSurfaceAlphaMod(over, (Uint8)(a < 0 ? 0 : a));
		SDL_BlitSurface(over, NULL, screen, NULL);
		GFX_flip(screen);
		SDL_Delay(per_step);
	}
	SDL_SetSurfaceAlphaMod(over, 255);
	SDL_SetSurfaceBlendMode(over, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(under, NULL, screen, NULL);
	GFX_flip(screen);
}

/* 停留：按任意键提前结束，或者到 aiHoldSecs 秒自动结束。0 = 一直等到按键 */
static void ai_hold(SDL_Surface* screen, int hold_secs) {
	uint32_t start = SDL_GetTicks();
	while (!quit && PAD_anyPressed() && SDL_GetTicks() - start < 1500) {
		PAD_poll();
		GFX_delay();
	}
	PAD_reset();
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_anyPressed()) break;
		if (hold_secs > 0 && SDL_GetTicks() - start > (uint32_t)hold_secs * 1000) break;
		GFX_flip(screen);
		GFX_delay();
	}
	PAD_reset();
}

/* ------------------------------------------------------------------ 调试计时 */

#define AI_DBG_GRAB  0
#define AI_DBG_PNG   1
#define AI_DBG_REQ   2
#define AI_DBG_CURL  3
#define AI_DBG_RESP  4
#define AI_DBG_PARSE 5
#define AI_DBG_DRAW  6
#define AI_DBG_TOTAL 7

static uint32_t ai_dbg_t[8];
static size_t   ai_dbg_sz_png, ai_dbg_sz_req, ai_dbg_sz_resp;
static int      ai_dbg_items, ai_dbg_drawn, ai_dbg_http;
static const char* ai_dbg_space = "-";     /* 模型这次给的坐标是哪种空间 */

static void ai_dbg_write(const char* note) {
	if (!ai_debug()) return;
	FILE* f = fopen(AI_TMP_DIR "/debug.txt", "a");
	if (!f) return;
	time_t now = time(NULL);
	struct tm* tm = localtime(&now);
	fprintf(f, "\n===== %04d-%02d-%02d %02d:%02d:%02d  %s =====\n",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec, note ? note : "");
	fprintf(f, "服务商=%s  模型=%s  HTTP=%d\n", ai_endpoint(), ai_model(), ai_dbg_http);
	fprintf(f, "抓帧 %u ms | 存PNG %u ms (%zu KB) | 拼请求 %u ms (%zu KB)\n",
		ai_dbg_t[AI_DBG_GRAB], ai_dbg_t[AI_DBG_PNG], ai_dbg_sz_png / 1024,
		ai_dbg_t[AI_DBG_REQ], ai_dbg_sz_req / 1024);
	fprintf(f, "curl %u ms | 读响应 %u ms (%zu KB) | 解析 %u ms | 绘制 %u ms\n",
		ai_dbg_t[AI_DBG_CURL], ai_dbg_t[AI_DBG_RESP], ai_dbg_sz_resp / 1024,
		ai_dbg_t[AI_DBG_PARSE], ai_dbg_t[AI_DBG_DRAW]);
	fprintf(f, "网络分段: DNS %.0fms | TCP %.0fms | TLS %.0fms | 首字节 %.0fms | 总 %.0fms\n",
		ai_net_dns * 1000, (ai_net_conn - ai_net_dns) * 1000,
		(ai_net_tls > 0 ? ai_net_tls - ai_net_conn : 0) * 1000,
		ai_net_first * 1000, ai_net_total * 1000);
	fprintf(f, "上传 %ld KB | 下载 %ld KB  (首字节 - TLS = 上传+排队+模型推理 %.0fms)\n",
		ai_net_up / 1024, ai_net_down / 1024,
		(ai_net_first - (ai_net_tls > 0 ? ai_net_tls : ai_net_conn)) * 1000);
	fprintf(f, "总计 %u ms   解出 %d 条  回贴 %d 条   坐标空间=%s  发图 %dx%d  渲染=粗描边Overlay\n",
		ai_dbg_t[AI_DBG_TOTAL], ai_dbg_items, ai_dbg_drawn,
		ai_dbg_space, ai_img_w, ai_img_h);
	fclose(f);
}

/* 在画面上叠一小块计时信息，不用拔卡就能看哪个环节慢 */
static void ai_dbg_draw(SDL_Surface* dst) {
	if (!ai_debug() || !font.tiny) return;
	char l1[256], l2[256], l3[256];
	snprintf(l1, sizeof(l1), "抓帧%ums 存图%ums(%zuKB) 拼请求%ums(%zuKB)",
		ai_dbg_t[AI_DBG_GRAB], ai_dbg_t[AI_DBG_PNG], ai_dbg_sz_png / 1024,
		ai_dbg_t[AI_DBG_REQ], ai_dbg_sz_req / 1024);
	snprintf(l2, sizeof(l2), "curl%ums 响应%ums(%zuKB) 解析%ums 绘制%ums  共%ums",
		ai_dbg_t[AI_DBG_CURL], ai_dbg_t[AI_DBG_RESP], ai_dbg_sz_resp / 1024,
		ai_dbg_t[AI_DBG_PARSE], ai_dbg_t[AI_DBG_DRAW], ai_dbg_t[AI_DBG_TOTAL]);
	/* 把手握 / 上传 / 服务端计算 / 下载拆开 —— 才知道时间到底花在哪一段 */
	double handshake = ai_net_pre * 1000.0;
	double up_ms = (ai_net_speed > 0.0) ? (double)ai_net_up / ai_net_speed * 1000.0 : 0.0;
	double round_ms = (ai_net_first - ai_net_pre) * 1000.0;   /* 上传 + 服务端 */
	double srv_ms = round_ms - up_ms;
	double dl_ms = (ai_net_total - ai_net_first) * 1000.0;
	snprintf(l3, sizeof(l3), "握手%.0fms | 上传%ldKB/%.0fms | 服务端%.0fms | 下载%.0fms",
		handshake, ai_net_up / 1024, up_ms, srv_ms, dl_ms);
	SDL_Color c = {255, 255, 90, 255};
	SDL_Surface* t1 = TTF_RenderUTF8_Blended(font.tiny, l1, c);
	SDL_Surface* t2 = TTF_RenderUTF8_Blended(font.tiny, l2, c);
	SDL_Surface* t3 = TTF_RenderUTF8_Blended(font.tiny, l3, c);
	int w = 0;
	if (t1 && t1->w > w) w = t1->w;
	if (t2 && t2->w > w) w = t2->w;
	if (t3 && t3->w > w) w = t3->w;
	SDL_Rect box = {4, 4, w + 12,
		(t1 ? t1->h : 0) + (t2 ? t2->h : 0) + (t3 ? t3->h : 0) + 10};
	SDL_FillRect(dst, &box, SDL_MapRGB(dst->format, 0, 0, 0));
	int y = 8;
	if (t1) { SDL_Rect d = {10, y, 0, 0}; SDL_BlitSurface(t1, NULL, dst, &d); y += t1->h; SDL_FreeSurface(t1); }
	if (t2) { SDL_Rect d = {10, y, 0, 0}; SDL_BlitSurface(t2, NULL, dst, &d); y += t2->h; SDL_FreeSurface(t2); }
	if (t3) { SDL_Rect d = {10, y, 0, 0}; SDL_BlitSurface(t3, NULL, dst, &d); SDL_FreeSurface(t3); }
}

/* ------------------------------------------------------------------ 主流程 */

void Menu_aiTranslate(void) {
	SDL_Surface* frozen = NULL;

	/* 两个功能共用 /req.json 和 /resp.json，同时跑会互相覆盖 */
	if (AI_playIsActive()) AI_playStopFor("开始翻译");

	SDL_Surface* over = NULL;
	unsigned char* resp = NULL;
	char ctx_terms[AI_CTX_TERMS] = {0};
	char ctx_sum[AI_CTX_SUM];                /* 模型这屏给的进度摘要，成功后落盘 */
	ctx_sum[0] = '\0';

	if (!ai_on() || !ai_key()[0]) {
		ai_notify(screen, "AI Translate 未配置",
			"请在 .userdata/shared/ai-translate.txt 里写 aiEnable=1 和 aiApiKey=你的key");
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

	mkdir(AI_TMP_DIR, 0755);
	remove(AI_RESP_PATH);
	uint32_t t_start = SDL_GetTicks();
	memset(ai_dbg_t, 0, sizeof(ai_dbg_t));
	ai_dbg_sz_png = ai_dbg_sz_req = ai_dbg_sz_resp = 0;
	ai_dbg_items = ai_dbg_drawn = ai_dbg_http = 0;
	uint32_t ai_clock = SDL_GetTicks();
	#define AI_STAGE(i) do { uint32_t _n = SDL_GetTicks(); ai_dbg_t[i] = _n - ai_clock; ai_clock = _n; } while (0)

	frozen = ai_grab();
	AI_STAGE(AI_DBG_GRAB);
	if (!frozen) {
		ai_notify(screen, "AI Translate：抓帧失败", NULL);
		goto fail;
	}
	int png_rc = ai_save_png(frozen, ai_maxw());
	AI_STAGE(AI_DBG_PNG);
	if (png_rc != 0) {
		ai_notify(screen, "AI Translate：存 PNG 失败", AI_PNG_PATH);
		goto fail;
	}
	{
		size_t png_len = 0;
		unsigned char* png = ai_read_file(AI_PNG_PATH, &png_len);
		ai_dbg_sz_png = png_len;
		if (!png) { ai_notify(screen, "AI Translate：读不到截图", NULL); goto fail; }
		char* b64 = ai_base64(png, png_len);
		free(png);
		if (!b64) { ai_notify(screen, "AI Translate：内存不足", NULL); goto fail; }
		char* body = ai_build_request(b64);
		free(b64);
		if (!body) { ai_notify(screen, "AI Translate：内存不足", NULL); goto fail; }
		int wrote = 0;
		FILE* rf = fopen(AI_REQ_PATH, "wb");
		if (rf) {
			wrote = (fwrite(body, 1, strlen(body), rf) == strlen(body));
			fclose(rf);
		}
		ai_dbg_sz_req = strlen(body);
		free(body);
		if (!wrote) { ai_notify(screen, "AI Translate：写不了临时文件", AI_TMP_DIR); goto fail; }
	}
	AI_STAGE(AI_DBG_REQ);

	/* 请求要跑好几秒，先把画面冻住并告诉用户在干嘛 */
	SDL_BlitSurface(frozen, NULL, screen, NULL);
	ai_notify(screen, "AI Translate 翻译中…", "请稍候，几秒钟");

	{
		char errbuf[512];
		int code = ai_http_post(ai_endpoint(), AI_REQ_PATH,
			ai_timeout(), errbuf, sizeof(errbuf));
		ai_dbg_http = code;
		AI_STAGE(AI_DBG_CURL);
		SDL_BlitSurface(frozen, NULL, screen, NULL);   /* 把"翻译中"抹掉再判断 */
		if (code != 200) {
			char msg[600];
			if (code > 0) {
				/* 把 API 返回的报错正文也带上 —— 401/400 的原因都在里面 */
				size_t blen = 0;
				unsigned char* bd = ai_read_file(AI_RESP_PATH, &blen);
				char body_snip[180] = "";
				if (bd) {
					int k = 0;
					for (unsigned char* q = bd; *q && k < 120; q++)
						body_snip[k++] = (*q == '\n' || *q == '\r' || *q == '\t') ? ' ' : (char)*q;
					body_snip[k] = '\0';
					free(bd);
				}
				snprintf(msg, sizeof(msg), "HTTP %d  %s  %s", code, errbuf, body_snip);
			} else {
				snprintf(msg, sizeof(msg), "%s", errbuf);
			}
			ai_notify(screen, "AI Translate 请求失败", msg);
			goto fail;
		}
	}

	{
		size_t resp_len = 0;
		resp = ai_read_file(AI_RESP_PATH, &resp_len);
		ai_dbg_sz_resp = resp_len;
		AI_STAGE(AI_DBG_RESP);
		if (!resp) { ai_dbg_write("空响应"); ai_notify(screen, "AI Translate：空响应", NULL); goto fail; }
	}

	over = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32,
		screen->format->format);
	if (!over) {
		ai_notify(screen, "AI Translate：内存不足", NULL);
		goto fail;
	}
	SDL_BlitSurface(frozen, NULL, over, NULL);

	{
		/* OpenAI 兼容接口把模型的回答塞在 message.content 里，是一段**转义过的字符串**：
		 * 原始响应里是 \"items\"，直接在整个响应文本上找 "items" 是找不到的。
		 * 所以先把 content 反转义出来，再在它上面解析 items。 */
		size_t clen = strlen((const char*)resp) + 1;
		char* content = (char*)malloc(clen);
		if (!content) {
			free(resp); resp = NULL;
			ai_notify(screen, "AI Translate：内存不足", NULL);
			goto fail;
		}
		if (ai_json_string((const char*)resp, "content", content, (int)clen) <= 0) {
			/* 结构不对，退一步直接在响应上试（有的服务商直接返回裸 JSON） */
			snprintf(content, clen, "%s", (const char*)resp);
		}
		free(resp);
		resp = NULL;

		/* 有的模型爱套一层 ```json 代码块 */
		char* cp = content;
		while (*cp == ' ' || *cp == '\n' || *cp == '\r' || *cp == '\t') cp++;
		if (!strncmp(cp, "```", 3)) {
			cp += 3;
			if (!strncmp(cp, "json", 4)) cp += 4;
			while (*cp == '\n' || *cp == '\r') cp++;
		}

		AI_Item items[AI_MAX_ITEMS];
		int n = ai_parse_items(cp, items, AI_MAX_ITEMS);
		ai_dbg_items = n;
		ctx_sum[0] = '\0';
		ai_json_string(cp, "context", ctx_sum, sizeof(ctx_sum));
		ai_json_string(cp, "terms", ctx_terms, sizeof(ctx_terms));
		/* 模型未必守归一化的规矩（deepseek 实测会按像素给），统一换回 0~1000 */
		if (n > 0) ai_dbg_space = ai_boxes_normalize(items, n, ai_img_w, ai_img_h);
		AI_STAGE(AI_DBG_PARSE);
		if (n <= 0) {
			/* 解析不出来时，把模型的原始回答前几十个字显示出来，方便定位 */
			char snip[80];
			int k = 0;
			for (char* q = cp; *q && k < 60; q++) {
				snip[k++] = (*q == '\n' || *q == '\r' || *q == '\t') ? ' ' : *q;
			}
			snip[k] = '\0';
			char msg[160];
			if (k == 0) snprintf(msg, sizeof(msg), "响应是空的");
			else        snprintf(msg, sizeof(msg), "模型回答：%s", snip);
			ai_dbg_write("没解析出文字");
			ai_notify(screen, "AI Translate：没解析出文字", msg);
			free(content);
			goto fail;
		}
		free(content);

		int drawable = ai_render_items(screen, over, items, n);
		if (drawable == 0) {
			ai_notify(screen, "AI Translate：坐标都不可用", NULL);
			goto fail;
		}
		AI_STAGE(AI_DBG_DRAW);
		ai_dbg_t[AI_DBG_TOTAL] = SDL_GetTicks() - t_start;

		/* 刻意不画「按任意键继续」提示条：它会盖住画面底部，而且译文本身已经说明一切。
		 * 提前结束仍然有效（ai_hold 里任意键就退出），自动消失时间仍由 aiHoldSecs 控制。 */

		ai_dbg_drawn = drawable;
		ai_dbg_draw(over);                        /* debug 时把耗时叠在左上角 */
		SDL_SetSurfaceBlendMode(over, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(over, NULL, screen, NULL);
		GFX_flip(screen);

		ai_ctx_update(ctx_sum, ctx_terms, items, n);          /* 记住这一屏，下一屏提示词里带上 */

		ai_dbg_write("成功");
		ai_hold(screen, ai_hold_secs());               /* 默认 5 秒 */
		ai_crossfade(screen, over, frozen);       /* 渐隐回原始画面 */
	}

	goto cleanup;

fail:
	ai_dbg_t[AI_DBG_TOTAL] = SDL_GetTicks() - t_start;
	ai_dbg_write("失败");
	ai_wait_dismiss(screen, AI_WAIT_SECS);

cleanup:
	if (resp) free(resp);
	if (over) SDL_FreeSurface(over);
	if (frozen) {
		/* 还原成抓来的那一帧：游戏下一帧只会重画它自己的区域，
		 * 黑边之类的空档不清掉的话会留着译文残影 */
		SDL_BlitSurface(frozen, NULL, screen, NULL);
		SDL_FreeSurface(frozen);
		GFX_flip(screen);
	}
}


/* ------------------------------------------------------------------ 游戏内配置菜单 */

/*
 * 放在 Options -> AI Translate 里。API key 不在这里改（配置文件中写死），
 * 这里只管开关、服务商、译文停留时间 —— 这些正好能用现成的「选项列表」控件，
 * 不需要设备端键盘。
 */
static char* ai_onoff_labels[]    = {"关闭", "开启", NULL};
static char* ai_provider_labels[] = {"百炼 Qwen3-VL", "DeepSeek", "自定义", NULL};
static char* ai_hold_labels[]     = {"3 秒", "5 秒", "8 秒", "10 秒", "15 秒", "一直留到按键", NULL};
static const int ai_hold_tab[]    = {3, 5, 8, 10, 15, 0};
/* 送图宽度：直接决定图片大小和模型要处理多少图块 —— 想提速先动这个 */
static char* ai_width_labels[]    = {"384（最快）", "512", "640", "768（默认）", "1024（最清晰）", NULL};
static const int ai_width_tab[]   = {384, 512, 640, 768, 1024};

static int AI_menu_changed(MenuList* list, int i) {
	MenuItem* item = &list->items[i];
	ai_cfg_load();
	if (item->id == 0) {
		ai_c_enable = item->value ? 1 : 0;
	} else if (item->id == 1) {
		ai_c_provider = item->value;
	} else if (item->id == 2) {
		if (item->value >= 0 && item->value < 6) ai_c_hold = ai_hold_tab[item->value];
	} else if (item->id == 3) {
		if (item->value >= 0 && item->value < 5) ai_c_maxw = ai_width_tab[item->value];
	} else if (item->id == 4) {
		ai_c_debug = item->value ? 1 : 0;
	} else if (item->id == 5) {
		ai_c_play_badge = item->value ? 1 : 0;
	} else if (item->id == 6) {
		ai_c_context = item->value ? 1 : 0;
	}
	ai_cfg_save();
	return MENU_CALLBACK_NOP;
}

static int AI_menu_reset_context(MenuList* list, int i) {
	list->items[i].desc = ai_ctx_reset_scene() ?
		"场景上下文已清空，稳定译名已保留。" : "清空失败，请检查存储空间后重试。";
	return MENU_CALLBACK_NOP;
}

static MenuList AI_menu = {
	.type = MENU_VAR,
	.desc = "API key 在 .userdata/shared/ai-translate.txt 里改。",
	.on_change = AI_menu_changed,
	.items = (MenuItem[]) {
		{ .name = "AI 翻译",  .desc = "在游戏里按热键翻译画面。",       .values = ai_onoff_labels,    .id = 0 },
		{ .name = "服务商",   .desc = "DeepSeek 的坐标定位不准，可能贴歪。", .values = ai_provider_labels, .id = 1 },
		{ .name = "译文停留", .desc = "多久之后自动渐隐。",             .values = ai_hold_labels,     .id = 2 },
		{ .name = "送图宽度", .desc = "越小越快。太慢就先调这个。",     .values = ai_width_labels,    .id = 3 },
		{ .name = "调试信息", .desc = "把各环节耗时叠在画面左上角。",   .values = ai_onoff_labels,    .id = 4 },
		{ .name = "状态角标", .desc = "代打时在左下角显示第几次和最近动作。", .values = ai_onoff_labels, .id = 5 },
		{ .name = "上下文",   .desc = "对白保留3分钟，稳定译名长期保留。", .values = ai_onoff_labels, .id = 6 },
		{ .name = "清空当前场景", .desc = "读档或跳剧情后使用；保留人名、地名等稳定译名。", .on_confirm = AI_menu_reset_context },
		{ NULL },
	}
};

static void AI_menu_refresh(void) {
	ai_cfg_load();
	AI_menu.items[0].value = ai_c_enable > 0 ? 1 : 0;
	AI_menu.items[1].value = ai_c_provider;
	AI_menu.items[2].value = 1;                       /* 找不到就按 5 秒显示 */
	for (int i = 0; i < 6; i++) {
		if (ai_hold_tab[i] == ai_c_hold) { AI_menu.items[2].value = i; break; }
	}
	AI_menu.items[3].value = 3;                       /* 找不到就按 768 显示 */
	for (int i = 0; i < 5; i++) {
		if (ai_width_tab[i] == ai_c_maxw) { AI_menu.items[3].value = i; break; }
	}
	AI_menu.items[4].value = ai_c_debug > 0 ? 1 : 0;
	AI_menu.items[5].value = ai_c_play_badge > 0 ? 1 : 0;
	AI_menu.items[6].value = ai_c_context != 0 ? 1 : 0;
	AI_menu.items[7].desc = "读档或跳剧情后使用；保留人名、地名等稳定译名。";
}

int OptionAI_openMenu(MenuList* list, int i) {
	AI_menu_refresh();
	Menu_options(&AI_menu);
	return MENU_CALLBACK_NOP;
}

/* ==================================================================
   AI 帮你玩（自动代打）
   ------------------------------------------------------------------
   开一个后台线程循环：抓帧 -> 问模型按哪个键 -> 主线程注入按键。
   为什么用线程：PNG 编码要 275ms、curl 要 1~2s，全放主线程会把游戏冻住。
   主线程只做 GL 读回（~25ms）和按键注入。
   ================================================================== */

#include <SDL2/SDL_thread.h>

#define AI_PLAY_HIST      8          /* 保留最近几次操作当上下文 */
#define AI_PLAY_NOTE_MAX  96

/* retro pad id（和日志里 core 报的一致） */
static const struct { const char* name; int id; } ai_play_actions[] = {
	{"B",0},{"Y",1},{"SELECT",2},{"START",3},{"UP",4},{"DOWN",5},
	{"LEFT",6},{"RIGHT",7},{"A",8},{"X",9},{"L",10},{"R",11},{NULL,-1}
};

static volatile int ai_play_active = 0;
static volatile int ai_play_busy = 0;
static volatile int ai_play_result_ready = 0;
static volatile int ai_play_wait_release = 0;   /* 等热键松开，否则启动瞬间就被"任意键"停掉 */
static int   ai_play_inject_mask = 0;      /* 要注入的 retro 位 */
static uint32_t ai_play_inject_until = 0;  /* 按到这个时刻为止（0 = 没在按） */
static uint32_t ai_play_next_at = 0;
static uint32_t ai_play_started = 0;
static int   ai_play_calls = 0;
static int   ai_play_err = 0;
static char  ai_play_last[16] = "—";
static int   ai_play_last_hold = 300;
static char  ai_play_goal[256] = "";
static char  ai_play_hist[AI_PLAY_HIST][AI_PLAY_NOTE_MAX];
static int   ai_play_hist_n = 0, ai_play_hist_head = 0;
static SDL_mutex* ai_play_lock = NULL;
static SDL_Thread* ai_play_thread = NULL;

#define AI_PLAY_MEM_PATH AI_TMP_DIR "/play-memory.txt"

/* 记忆落盘：目标和最近操作，重启后还在（这就是「长期动机」） */
static void ai_play_memory_save(void) {
	FILE* f = fopen(AI_PLAY_MEM_PATH, "w");
	if (!f) return;
	fprintf(f, "goal=%s\n", ai_play_goal);
	fprintf(f, "calls=%d\n", ai_play_calls);
	for (int i = 0; i < ai_play_hist_n; i++) {
		int idx = (ai_play_hist_head - ai_play_hist_n + i + AI_PLAY_HIST * 2) % AI_PLAY_HIST;
		fprintf(f, "log=%s\n", ai_play_hist[idx]);
	}
	fclose(f);
}

static void ai_play_memory_load(void) {
	FILE* f = fopen(AI_PLAY_MEM_PATH, "r");
	if (!f) return;
	char line[AI_PLAY_NOTE_MAX + 16];
	while (fgets(line, sizeof(line), f)) {
		char* nl = strpbrk(line, "\r\n");
		if (nl) *nl = '\0';
		if (!strncmp(line, "goal=", 5)) {
			snprintf(ai_play_goal, sizeof(ai_play_goal), "%s", line + 5);
		} else if (!strncmp(line, "log=", 4)) {
			snprintf(ai_play_hist[ai_play_hist_head], AI_PLAY_NOTE_MAX, "%s", line + 4);
			ai_play_hist_head = (ai_play_hist_head + 1) % AI_PLAY_HIST;
			if (ai_play_hist_n < AI_PLAY_HIST) ai_play_hist_n++;
		}
	}
	fclose(f);
}

static void ai_play_history_push(const char* action, const char* note) {
	snprintf(ai_play_hist[ai_play_hist_head], AI_PLAY_NOTE_MAX, "%s: %s", action, note ? note : "");
	ai_play_hist_head = (ai_play_hist_head + 1) % AI_PLAY_HIST;
	if (ai_play_hist_n < AI_PLAY_HIST) ai_play_hist_n++;
}

/* ---------- 后台线程 ---------- */

typedef struct { unsigned char* pixels; int w, h; } AI_PlayJob;

static int ai_play_worker(void* arg) {
	AI_PlayJob* job = (AI_PlayJob*)arg;
	int cw = 0, ch = 0;
	unsigned char* png = NULL;
	size_t png_len = 0;
	char* b64 = NULL;
	char* body = NULL;
	unsigned char* resp = NULL;
	char action[16] = "";
	int  hold_ms = 300;
	char goal[256] = "";
	char note[AI_PLAY_NOTE_MAX] = "";
	int   err = 0;

	/* 1) 像素 -> 缩小 -> PNG（这步最费 CPU，放这儿就对了） */
	SDL_Surface* raw = SDL_CreateRGBSurfaceWithFormatFrom(
		job->pixels, job->w, job->h, 32, job->w * 4, SDL_PIXELFORMAT_ABGR8888);
	if (!raw) { err = 1; goto done; }

	int maxw = ai_maxw();
	SDL_Surface* small = raw;
	if (maxw > 0 && job->w > maxw) {
		int nw = maxw, nh = (int)((long)job->h * maxw / job->w);
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, nw, nh, 32, SDL_PIXELFORMAT_ABGR8888);
		if (s) { SDL_BlitScaled(raw, NULL, s, NULL); small = s; }
	}
	SDL_Surface* argb = SDL_ConvertSurfaceFormat(small, SDL_PIXELFORMAT_ARGB8888, 0);
	if (small != raw) SDL_FreeSurface(small);
	if (!argb) { SDL_FreeSurface(raw); err = 1; goto done; }
	{
		SDL_RWops* rw = SDL_RWFromFile(AI_PNG_PATH, "wb");
		if (rw) { if (IMG_SavePNG_RW(argb, rw, 1) != 0) SDL_RWclose(rw); }
	}
	SDL_FreeSurface(argb);
	SDL_FreeSurface(raw);

	png = ai_read_file(AI_PNG_PATH, &png_len);
	if (!png) { err = 2; goto done; }
	b64 = ai_base64(png, png_len);
	free(png); png = NULL;
	if (!b64) { err = 3; goto done; }

	/* 2) 拼请求：带上长期目标 + 最近几次操作 */
	{
		char prompt[2048];
		int off = snprintf(prompt, sizeof(prompt),
			"你在替玩家玩这个游戏。看当前画面，决定下一步按什么、按多久。\n"
			"action 可选：B Y SELECT START UP DOWN LEFT RIGHT A X L R，"
			"多个键用 + 连表示同时按（例如 UP+RIGHT 走斜向），WAIT 表示先不动。\n"
			"hold_ms 是按住的毫秒数，这个很关键：\n"
			"  · 要让角色/光标走一段路：400~2000，按住才会连续移动\n"
			"  · 只是菜单里跳一格、确认一下：60~120，按久了会连跳过去\n"
			"长期目标（沿用你上次写的，可以改）：%s\n",
			ai_play_goal[0] ? ai_play_goal : "（还没定，先看清游戏在要求什么）");
		if (ai_play_hist_n > 0) {
			off += snprintf(prompt + off, sizeof(prompt) - off, "最近的操作：");
			for (int i = 0; i < ai_play_hist_n && off < (int)sizeof(prompt) - 120; i++) {
				int idx = (ai_play_hist_head - ai_play_hist_n + i + AI_PLAY_HIST * 2) % AI_PLAY_HIST;
				off += snprintf(prompt + off, sizeof(prompt) - off, " [%s]", ai_play_hist[idx]);
			}
			off += snprintf(prompt + off, sizeof(prompt) - off, "\n");
		}
		snprintf(prompt + off, sizeof(prompt) - off,
			"严格只输出 JSON，不要解释：\n"
			"{\"action\":\"按键名或WAIT\",\"hold_ms\":800,"
			"\"goal\":\"更新后的长期目标，没变就重复上一句\","
			"\"note\":\"一句话说明你为什么这么按，会记进操作历史\"}");

		char* esc = (char*)malloc(4096);
		if (!esc) { err = 3; goto done; }
		ai_json_escape(prompt, esc, 4096);
		size_t need = strlen(esc) + strlen(b64) + 1024;
		body = (char*)malloc(need);
		if (!body) { free(esc); err = 3; goto done; }
		snprintf(body, need,
			"{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":["
			"{\"type\":\"text\",\"text\":\"%s\"},"
			"{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,%s\"}}"
			"]}],\"temperature\":0.1%s}",
			ai_model(), esc, b64, ",\"thinking\":{\"type\":\"disabled\"}");
		free(esc);
	}
	free(b64); b64 = NULL;

	{
		FILE* rf = fopen(AI_REQ_PATH, "wb");
		if (!rf) { err = 4; goto done; }
		fwrite(body, 1, strlen(body), rf);
		fclose(rf);
	}
	free(body); body = NULL;

	/* 3) 请求 */
	{
		char errbuf[256];
		int code = ai_http_post(ai_endpoint(), AI_REQ_PATH, ai_timeout(), errbuf, sizeof(errbuf));
		ai_dbg_http = code;
		if (code != 200) { err = 5; goto done; }
	}

	/* 4) 解析 */
	resp = ai_read_file(AI_RESP_PATH, &png_len);
	if (!resp) { err = 6; goto done; }
	{
		size_t clen = strlen((const char*)resp) + 1;
		char* content = (char*)malloc(clen);
		if (!content) { err = 6; goto done; }
		if (ai_json_string((const char*)resp, "content", content, (int)clen) <= 0)
			snprintf(content, clen, "%s", (const char*)resp);
		free(resp); resp = NULL;
		char* cp = content;
		while (*cp == ' ' || *cp == '\n' || *cp == '\r' || *cp == '\t') cp++;
		if (!strncmp(cp, "```", 3)) {
			cp += 3;
			if (!strncmp(cp, "json", 4)) cp += 4;
			while (*cp == '\n' || *cp == '\r') cp++;
		}
		ai_json_string(cp, "action", action, sizeof(action));
		hold_ms = ai_json_int(cp, "hold_ms", 300);
		ai_json_string(cp, "goal", goal, sizeof(goal));
		ai_json_string(cp, "note", note, sizeof(note));
		free(content);
	}

done:
	free(png); free(b64); free(body); free(resp);
	free(job->pixels); free(job);

	SDL_LockMutex(ai_play_lock);
	if (!err) {
		snprintf(ai_play_last, sizeof(ai_play_last), "%s", action[0] ? action : "WAIT");
		ai_play_last_hold = hold_ms;
		if (goal[0]) snprintf(ai_play_goal, sizeof(ai_play_goal), "%s", goal);
		ai_play_history_push(ai_play_last, note);
		ai_play_memory_save();
		ai_play_calls++;
	}
	ai_play_err = err;
	ai_play_busy = 0;
	ai_play_result_ready = 1;
	SDL_UnlockMutex(ai_play_lock);
	return 0;
}

/* ---------- 主线程 ---------- */

/*
 * 注入一次操作。两个要点：
 *  - action 支持组合键，用 '+' 连（"UP+RIGHT" 走斜向，很多 RPG 需要）
 *  - hold_ms 决定按多久。只保持几帧的话角色几乎不动，走路必须按住；
 *    而菜单里按一下只想跳一格，按住反而连跳 —— 这个维度只能交给模型判断。
 */
static void ai_play_inject(const char* action, int hold_ms) {
	ai_play_inject_mask = 0;
	ai_play_inject_until = 0;
	if (!action || !action[0]) return;

	char buf[64];
	snprintf(buf, sizeof(buf), "%s", action);
	int found = 0;
	char* save = NULL;
	for (char* t = strtok_r(buf, "+ ", &save); t; t = strtok_r(NULL, "+ ", &save)) {
		for (int i = 0; ai_play_actions[i].name; i++) {
			if (!strcasecmp(ai_play_actions[i].name, t)) {
				ai_play_inject_mask |= 1 << ai_play_actions[i].id;
				found = 1;
				break;
			}
		}
	}
	if (!found || !ai_play_inject_mask) return;   /* WAIT / 没听过的词 -> 什么都不按 */

	if (hold_ms < 50)   hold_ms = 50;
	if (hold_ms > 3000) hold_ms = 3000;
	ai_play_inject_until = SDL_GetTicks() + (uint32_t)hold_ms;
}

int AI_playInjectMask(void) {
	if (!ai_play_active || !ai_play_inject_until) return 0;
	if (SDL_GetTicks() >= ai_play_inject_until) { ai_play_inject_until = 0; return 0; }
	return ai_play_inject_mask;
}

static void ai_play_stop(const char* why) {
	if (!ai_play_active) return;
	ai_play_active = 0;
	ai_play_inject_until = 0;
	ai_play_inject_mask = 0;
	LOG_info("AI 帮你玩: 停止 (%s)  共 %d 次\n", why ? why : "", ai_play_calls);
	/* 角标只是一直在左下角，消失时没人会注意到 —— 明确弹一条 */
	char msg[NOTIFICATION_MAX_MESSAGE];
	snprintf(msg, sizeof(msg), "AI 帮你玩 停止（%s，共 %d 次）",
		(why && why[0]) ? why : "已结束", ai_play_calls);
	Notification_push(NOTIFICATION_SETTING, msg, NULL);
}

int  AI_playIsActive(void) { return ai_play_active; }
void AI_playStopFor(const char* why) { ai_play_stop(why); }

void Menu_aiAutoPlay(void) {
	if (!ai_on() || !ai_key()[0]) {
		ai_notify(screen, "AI Translate 未配置",
			"请在 .userdata/shared/ai-translate.txt 里写 aiEnable=1 和 key");
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}
	if (ai_play_active) { ai_play_stop("手动停止"); return; }

	if (!ai_play_lock) ai_play_lock = SDL_CreateMutex();
	if (ai_play_thread) { SDL_WaitThread(ai_play_thread, NULL); ai_play_thread = NULL; }

	mkdir(AI_TMP_DIR, 0755);
	ai_play_memory_load();               /* 跨会话接着上次的进度和动机 */
	ai_play_calls = 0;
	ai_play_err = 0;
	ai_play_result_ready = 0;
	ai_play_busy = 0;
	ai_play_active = 1;
	ai_play_wait_release = 1;
	ai_play_started = SDL_GetTicks();
	ai_play_next_at = ai_play_started;
	PAD_reset();
	LOG_info("AI 帮你玩: 开始\n");
	Notification_push(NOTIFICATION_SETTING, "AI 帮你玩 开始", NULL);
}

void AI_playTick(void) {
	if (!ai_play_active) return;

	/* 0) 先等启动它的那个热键松开，否则下面立刻就把自己停了 */
	if (ai_play_wait_release) {
		if (PAD_anyPressed()) return;
		ai_play_wait_release = 0;
	}

	/* 1) 用户按任意键 -> 退出（注入的按键走的是另一条路，PAD 看不到，不会误触发） */
	if (PAD_anyPressed()) {
		ai_play_stop("用户按键");
		return;
	}

	uint32_t now = SDL_GetTicks();

	/* 2) 结果回来了 -> 注入按键 */
	if (ai_play_result_ready) {
		SDL_LockMutex(ai_play_lock);
		ai_play_result_ready = 0;
		if (ai_play_err) {
			/* 出错就停，别在后台无声地烧钱 */
			ai_play_active = 0;
		} else {
			ai_play_inject(ai_play_last, ai_play_last_hold);
		}
		int calls = ai_play_calls;
		SDL_UnlockMutex(ai_play_lock);

		if (!ai_play_active) { LOG_info("AI 帮你玩: 请求失败(代码 %d)，已停止\n", ai_play_err); return; }

		int maxc = ai_play_max_calls();
		if (maxc > 0 && calls >= maxc) { ai_play_stop("达到次数上限"); return; }
		ai_play_next_at = now + (uint32_t)ai_play_interval_ms();
	}

	/* 3) 空闲且到点 -> 抓帧 + 起线程 */
	if (!ai_play_busy && ai_play_thread == NULL && now >= ai_play_next_at) {
		int cw = 0, ch = 0;
		SDL_Surface* frame = Video_captureGameFrame(screen->w, screen->h, SDL_PIXELFORMAT_ABGR8888);
		unsigned char* px = NULL;
		if (frame) {
			cw = frame->w; ch = frame->h;
			px = malloc((size_t)cw * ch * 4);
			if (px) for (int y = 0; y < ch; y++)
				memcpy(px + (size_t)y * cw * 4, (unsigned char*)frame->pixels + (size_t)y * frame->pitch, (size_t)cw * 4);
			SDL_FreeSurface(frame);
		}
		if (px && cw > 0 && ch > 0) {
			AI_PlayJob* job = (AI_PlayJob*)malloc(sizeof(AI_PlayJob));
			if (job) {
				job->pixels = px; job->w = cw; job->h = ch;
				ai_play_busy = 1;
				ai_play_next_at = now + 30000;      /* 防呆：万一线程没回来也别狂发 */
				ai_play_thread = SDL_CreateThread(ai_play_worker, "AI_play", job);
				if (!ai_play_thread) { ai_play_busy = 0; free(px); free(job); }
			} else free(px);
		}
	}

	/* 4) 线程结束了就回收句柄 */
	if (ai_play_thread && !ai_play_busy) {
		SDL_WaitThread(ai_play_thread, NULL);
		ai_play_thread = NULL;
	}

	/* 5) 按键松手由 ai_play_inject_until 到点自动处理，这里不用做事 */
}

/* 状态角标：画在 screen_flip 之前才看得见 */
void AI_playDrawBadge(SDL_Surface* dst) {
	if (!ai_play_active || !dst || !font.tiny) return;
	if (!ai_play_badge_on()) return;   /* 默认不显示 */
	uint32_t secs = (SDL_GetTicks() - ai_play_started) / 1000;
	char l1[128], l2[128];
	snprintf(l1, sizeof(l1), "AI 帮你玩 %02u:%02u  第 %d 次", secs / 60, secs % 60, ai_play_calls);
	if (ai_play_busy)      snprintf(l2, sizeof(l2), "思考中…  最近: %s", ai_play_last);
	else if (ai_play_err)  snprintf(l2, sizeof(l2), "出错(代码 %d)", ai_play_err);
	else                   snprintf(l2, sizeof(l2), "最近: %s  按任意键停止", ai_play_last);

	SDL_Color c = {120, 255, 140, 255};
	SDL_Surface* t1 = TTF_RenderUTF8_Blended(font.tiny, l1, c);
	SDL_Surface* t2 = TTF_RenderUTF8_Blended(font.tiny, l2, c);
	int w = 0;
	if (t1 && t1->w > w) w = t1->w;
	if (t2 && t2->w > w) w = t2->w;
	if (w <= 0) { if (t1) SDL_FreeSurface(t1); if (t2) SDL_FreeSurface(t2); return; }
	int h = (t1 ? t1->h : 0) + (t2 ? t2->h : 0);
	SDL_Rect box = {6, dst->h - h - 14, w + 14, h + 10};
	SDL_FillRect(dst, &box, SDL_MapRGB(dst->format, 0, 0, 0));
	int y = dst->h - h - 9;
	if (t1) { SDL_Rect d = {12, y, 0, 0}; SDL_BlitSurface(t1, NULL, dst, &d); y += t1->h; SDL_FreeSurface(t1); }
	if (t2) { SDL_Rect d = {12, y, 0, 0}; SDL_BlitSurface(t2, NULL, dst, &d); SDL_FreeSurface(t2); }
}

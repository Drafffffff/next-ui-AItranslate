#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "ma_internal.h"
#include "ma_frontend_opts.h"
#include "ma_menu.h"
#include "ma_ai.h"

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
#define AI_WAIT_SECS 30          /* 错误提示框停留多久 */
#define AI_FADE_MS   400         /* 译文渐隐时长 */

typedef struct {
	char  orig[128];
	char  zh[768];
	float box[4];                /* 归一化 0~1000：左,上,右,下 */
	char  color[16];
	char  align[12];
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
	"aiEndpoint", "aiModel", "aiDebug", NULL
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
	return NULL;
}

/* 把内存里的值写回文件：已有的键就地改，没有的追加，其余行原样保留 */
static void ai_cfg_save(void) {
	if (!ai_c_loaded) return;
	int seen[16];
	int nkeys = 0;
	while (ai_keys[nkeys]) nkeys++;
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
}

static int         ai_on(void)       { ai_cfg_load(); return ai_c_enable > 0; }
static int         ai_debug(void)    { ai_cfg_load(); return ai_c_debug; }
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

/* 抓当前显示的画面，缩放到 screen 尺寸当底图。调用方负责 free。
 * 注意：整个流程只抓这一次 —— 一旦往 screen 上画过提示框，再抓就会把提示框也拍进去。 */
static SDL_Surface* ai_grab(void) {
	int cw = 0, ch = 0;
	unsigned char* pixels = GFX_GL_screenCapture(&cw, &ch);
	if (!pixels || cw <= 0 || ch <= 0) {
		if (pixels) free(pixels);
		return NULL;
	}
	SDL_Surface* raw = SDL_CreateRGBSurfaceWithFormatFrom(
		pixels, cw, ch, 32, cw * 4, SDL_PIXELFORMAT_ABGR8888);
	if (!raw) { free(pixels); return NULL; }
	SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(
		0, screen->w, screen->h, 32, screen->format->format);
	if (out) SDL_BlitScaled(raw, NULL, out, NULL);
	SDL_FreeSurface(raw);
	free(pixels);
	return out;
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

/*
 * 提示词：只让模型出「原文 / 译文 / 紧贴文字的归一化框 / 颜色 / 对齐」。
 * 实测要点（见 ~/Downloads/ai-translate-test/README.md）：
 *  - 明确要「紧贴文字笔画、不要把对话框/状态条边框算进去」，否则模型会框住整个容器；
 *  - 归一化 0~1000 比直接要像素值稳；
 *  - 纯数字/已是中文的要它 zh 留空，免得白改一遍。
 */
static const char* AI_PROMPT_FMT =
	"这是%s游戏的截图。请找出画面里所有需要翻译的非中文文字（日文/英文），"
	"逐条给出：原文、%s译文、紧贴文字笔画的矩形框、文字颜色、排版对齐方式。\n"
	"坐标用归一化值 0~1000：整张图左上角 (0,0)，右下角 (1000,1000)。\n"
	"box 顺序是 [左, 上, 右, 下]，紧贴文字笔画，不要把外面的对话框边框、状态条底栏算进去。\n"
	"严格只输出 JSON，不要解释和 markdown：\n"
	"{\"items\":[{\"orig\":\"原文\",\"zh\":\"译文\",\"box\":[左,上,右,下],"
	"\"color\":\"white|black|cyan|yellow|red|blue|green|gray\",\"align\":\"left|center\"}],"
	"\"note\":\"一句话说明这是什么界面\"}\n"
	"要求：\n"
	"- 一条文字一项，同一个框里的多行文字分成多项。\n"
	"- 只有**已经是中文**、或者**纯数字/符号/罗马数字**的才把 zh 留空。\n"
	"- 人名、地名、生物名、作品名这类专有名词**也要翻译或音译**，不要留空。\n"
	"- align 看这条文字在它所在位置是靠左还是居中。\n"
	"- 译文尽量短，别超出框的宽度。";

static char* ai_build_request(const char* b64) {
	char prompt[2048];
	char lang[64];
	ai_json_escape(ai_lang(), lang, sizeof(lang));
	snprintf(prompt, sizeof(prompt), AI_PROMPT_FMT, "像素", lang);

	char* esc = (char*)malloc(4096);
	if (!esc) return NULL;
	ai_json_escape(prompt, esc, 4096);

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
		int has_box = (ai_json_numbers(obj, "box", it->box, 4) == 0);
		free(obj);

		if (has_box && it->zh[0]) n++;
		/* 跳到下一个元素：数组元素之间是逗号，漏掉它就只会解出第一条 */
		p = q + 1;
		p = ai_skip_ws(p);
		if (*p == ',') p++;
	}
	return n;
}

/* ------------------------------------------------------------------ 绘制 */

static void ai_rgb_of(const char* name, int* r, int* g, int* b) {
	*r = 255; *g = 255; *b = 255;
	if (!name) return;
	if (!strcmp(name, "black"))       { *r = 0;   *g = 0;   *b = 0;   }
	else if (!strcmp(name, "cyan"))   { *r = 0;   *g = 230; *b = 230; }
	else if (!strcmp(name, "yellow")) { *r = 255; *g = 230; *b = 0;   }
	else if (!strcmp(name, "red"))    { *r = 255; *g = 60;  *b = 60;  }
	else if (!strcmp(name, "blue"))   { *r = 90;  *g = 140; *b = 255; }
	else if (!strcmp(name, "green"))  { *r = 80;  *g = 230; *b = 90;  }
	else if (!strcmp(name, "gray"))   { *r = 200; *g = 200; *b = 200; }
}

/* 框内背景色的众数 —— 擦掉原文时用它填，才不会有明显补丁 */
static int ai_sampled_bg(SDL_Surface* s, int x0, int y0, int x1, int y1, int* or_, int* og, int* ob) {
	static uint16_t hist[4096];
	memset(hist, 0, sizeof(hist));
	int bpp = s->format->BytesPerPixel;
	if (bpp != 4) return -1;
	uint8_t* base = (uint8_t*)s->pixels;
	int n = 0;
	for (int y = y0; y <= y1; y++) {
		if (y < 0 || y >= s->h) continue;
		uint8_t* row = base + (size_t)y * s->pitch;
		for (int x = x0; x <= x1; x++) {
			if (x < 0 || x >= s->w) continue;
			uint32_t p; memcpy(&p, row + (size_t)x * 4, 4);
			uint8_t r, g, b;
			SDL_GetRGB(p, s->format, &r, &g, &b);
			hist[((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)]++;
			n++;
		}
	}
	if (!n) return -1;
	int best = 0;
	for (int i = 1; i < 4096; i++) if (hist[i] > hist[best]) best = i;
	long sr = 0, sg = 0, sb = 0, c = 0;
	for (int y = y0; y <= y1; y++) {
		if (y < 0 || y >= s->h) continue;
		uint8_t* row = base + (size_t)y * s->pitch;
		for (int x = x0; x <= x1; x++) {
			if (x < 0 || x >= s->w) continue;
			uint32_t p; memcpy(&p, row + (size_t)x * 4, 4);
			uint8_t r, g, b;
			SDL_GetRGB(p, s->format, &r, &g, &b);
			if ((((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)) == best) { sr += r; sg += g; sb += b; c++; }
		}
	}
	if (!c) return -1;
	*or_ = (int)(sr / c); *og = (int)(sg / c); *ob = (int)(sb / c);
	return 0;
}

/* 把一条译文画进它自己的框里：字号取「塞得进框」的最大值，位置按模型给的对齐方式 */
/* 感知亮度，用来判断字色和底色够不够分得开 */
static int ai_lum(int r, int g, int b) { return (r * 299 + g * 587 + b * 114) / 1000; }

static void ai_draw_one(SDL_Surface* screen, const char* text, int bx0, int by0, int bx1, int by1,
                        const char* color_name, int align_left,
                        int bgr, int bgg, int bgb) {
	char font_path[512];
	snprintf(font_path, sizeof(font_path), "%s/%s", RES_PATH, CFG_getFontFile());

	int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
	int max_w = bw + bw / 8;              /* 允许略微超出框，别把字挤成蚂蚁 */
	int max_h = bh + bh / 4;

	TTF_Font* f = TTF_OpenFont(font_path, 100);
	if (!f) return;
	TTF_SetFontStyle(f, CFG_getFontStyle());
	int rw = 0, rh = 0;
	TTF_SizeUTF8(f, text, &rw, &rh);
	if (rw <= 0 || rh <= 0) { TTF_CloseFont(f); return; }
	int size = 100;
	if (rw > max_w) size = size * max_w / rw;
	if (rh > max_h) { int s2 = 100 * max_h / rh; if (s2 < size) size = s2; }
	if (size > 200) size = 200;
	if (size < 8)   size = 8;
	TTF_CloseFont(f);

	f = TTF_OpenFont(font_path, size);
	if (!f) return;
	TTF_SetFontStyle(f, CFG_getFontStyle());
	int tw = 0, th = 0;
	TTF_SizeUTF8(f, text, &tw, &th);
	if (tw > max_w || th > max_h) {       /* 线性估计有偏差，再收敛一次 */
		int s2 = size;
		if (tw > max_w) { int v = size * max_w / tw; if (v < s2) s2 = v; }
		if (th > max_h) { int v = size * max_h / th; if (v < s2) s2 = v; }
		if (s2 < 8) s2 = 8;
		if (s2 < size) {
			TTF_CloseFont(f);
			size = s2;
			f = TTF_OpenFont(font_path, size);
			if (!f) return;
			TTF_SetFontStyle(f, CFG_getFontStyle());
			TTF_SizeUTF8(f, text, &tw, &th);
		}
	}

	int r, g, b;
	ai_rgb_of(color_name, &r, &g, &b);
	/* 字画在被底色填过的框上，所以只跟底色比。太接近就换成对比更强的那个 ——
	 * 实测模型偶尔会返回 color=black 而底色本来就是黑的，不兜底就是黑上画黑。 */
	int lb = ai_lum(bgr, bgg, bgb);
	if (ai_lum(r, g, b) - lb < 70 && lb - ai_lum(r, g, b) < 70) {
		if (lb < 128) { r = 255; g = 255; b = 255; }
		else          { r = 0;   g = 0;   b = 0;   }
	}
	SDL_Color fg = {(Uint8)r, (Uint8)g, (Uint8)b, 255};
	SDL_Surface* ts = TTF_RenderUTF8_Blended(f, text, fg);
	TTF_CloseFont(f);
	if (!ts) return;

	int tx = align_left ? bx0 : (bx0 + bx1 + 1) / 2 - ts->w / 2;
	int ty = (by0 + by1 + 1) / 2 - ts->h / 2;
	if (tx < 0) tx = 0;
	if (ty < 0) ty = 0;
	if (tx + ts->w > screen->w) tx = screen->w - ts->w;
	if (ty + ts->h > screen->h) ty = screen->h - ts->h;
	SDL_Rect d = {tx, ty, 0, 0};
	SDL_BlitSurface(ts, NULL, screen, &d);
	SDL_FreeSurface(ts);
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
	fprintf(f, "总计 %u ms   解出 %d 条  回贴 %d 条\n",
		ai_dbg_t[AI_DBG_TOTAL], ai_dbg_items, ai_dbg_drawn);
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
	SDL_Surface* over = NULL;
	unsigned char* resp = NULL;

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
		free(body);
		ai_dbg_sz_req = strlen(body);
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

		int W = screen->w, H = screen->h;
		int px0[AI_MAX_ITEMS], py0[AI_MAX_ITEMS], px1[AI_MAX_ITEMS], py1[AI_MAX_ITEMS];
		int bg_r[AI_MAX_ITEMS], bg_g[AI_MAX_ITEMS], bg_b[AI_MAX_ITEMS];
		int drawable = 0;

		for (int i = 0; i < n; i++) {
			int x0 = (int)(items[i].box[0] / 1000.0f * W);
			int y0 = (int)(items[i].box[1] / 1000.0f * H);
			int x1 = (int)(items[i].box[2] / 1000.0f * W);
			int y1 = (int)(items[i].box[3] / 1000.0f * H);
			if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
			if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 >= W) x1 = W - 1;
			if (y1 >= H) y1 = H - 1;
			if (x1 - x0 < 4 || y1 - y0 < 4) continue;                    /* 框太小，不敢动 */
			if (x1 - x0 > W * 98 / 100 && y1 - y0 > H * 98 / 100) continue; /* 整屏，判为坐标不可信 */
			px0[drawable] = x0; py0[drawable] = y0; px1[drawable] = x1; py1[drawable] = y1;
			if (ai_sampled_bg(screen, x0, y0, x1, y1,
					&bg_r[drawable], &bg_g[drawable], &bg_b[drawable]) != 0) {
				bg_r[drawable] = 0; bg_g[drawable] = 0; bg_b[drawable] = 0;
			}
			drawable++;
		}
		if (drawable == 0) {
			ai_notify(screen, "AI Translate：坐标都不可用", NULL);
			goto fail;
		}

		/* 擦除范围按屏幕尺寸取比例 —— 实测模型框在归一化空间里宽约偏 2%、下边约偏 2%，
		 * 换算到设备分辨率就是 20px 上下，写死小常数会盖不住原文留残影 */
		int pad_x = W * 2 / 100, pad_t = H * 1 / 100, pad_b = H * 2 / 100;
		if (pad_x < 6) pad_x = 6;
		if (pad_t < 3) pad_t = 3;
		if (pad_b < 5) pad_b = 5;

		/* 先全部擦干净再画 —— 否则后面那条的擦除会把前面画好的译文抹掉 */
		for (int i = 0; i < drawable; i++) {
			int ex = px0[i] - pad_x, ey0 = py0[i] - pad_t;
			int ex1 = px1[i] + pad_x, ey1 = py1[i] + pad_b;
			if (ex < 0) ex = 0;
			if (ey0 < 0) ey0 = 0;
			if (ex1 >= W) ex1 = W - 1;
			if (ey1 >= H) ey1 = H - 1;
			SDL_Rect r = {ex, ey0, ex1 - ex + 1, ey1 - ey0 + 1};
			SDL_FillRect(over, &r, SDL_MapRGB(over->format,
				(Uint8)bg_r[i], (Uint8)bg_g[i], (Uint8)bg_b[i]));
		}
		for (int i = 0, k = 0; i < n && k < drawable; i++) {
			int x0 = (int)(items[i].box[0] / 1000.0f * W);
			int y0 = (int)(items[i].box[1] / 1000.0f * H);
			int x1 = (int)(items[i].box[2] / 1000.0f * W);
			int y1 = (int)(items[i].box[3] / 1000.0f * H);
			if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
			if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 >= W) x1 = W - 1;
			if (y1 >= H) y1 = H - 1;
			if (x1 - x0 < 4 || y1 - y0 < 4) continue;
			if (x1 - x0 > W * 98 / 100 && y1 - y0 > H * 98 / 100) continue;
			ai_draw_one(over, items[i].zh, x0, y0, x1, y1, items[i].color,
				!strcmp(items[i].align, "left"),
				bg_r[k], bg_g[k], bg_b[k]);
			k++;
		}
		AI_STAGE(AI_DBG_DRAW);
		ai_dbg_t[AI_DBG_TOTAL] = SDL_GetTicks() - t_start;

		if (font.tiny) {
			int hold = ai_hold_secs();
			char hint_txt[64];
			if (hold > 0) snprintf(hint_txt, sizeof(hint_txt), "按任意键继续（%d 秒后自动）", hold);
			else          snprintf(hint_txt, sizeof(hint_txt), "按任意键继续");
			SDL_Color hint = {230, 230, 235, 255};
			SDL_Surface* t = TTF_RenderUTF8_Blended(font.tiny, hint_txt, hint);
			if (t) {
				SDL_Rect pill = {W / 2 - t->w / 2 - 12, H - t->h - 20, t->w + 24, t->h + 12};
				SDL_FillRect(over, &pill, SDL_MapRGB(over->format, 0, 0, 0));
				SDL_Rect d = {W / 2 - t->w / 2, H - t->h - 14, 0, 0};
				SDL_BlitSurface(t, NULL, over, &d);
				SDL_FreeSurface(t);
			}
		}

		ai_dbg_drawn = drawable;
		ai_dbg_draw(over);                        /* debug 时把耗时叠在左上角 */
		SDL_SetSurfaceBlendMode(over, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(over, NULL, screen, NULL);
		GFX_flip(screen);

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
	}
	ai_cfg_save();
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
}

int OptionAI_openMenu(MenuList* list, int i) {
	AI_menu_refresh();
	Menu_options(&AI_menu);
	return MENU_CALLBACK_NOP;
}

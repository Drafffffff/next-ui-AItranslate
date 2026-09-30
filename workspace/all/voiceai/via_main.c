/*
 * Voice AI —— NextUI 原生工具（tg5040 / TrimUI Brick）
 *
 * 语音输入，文字输出：按 B 说话 -> 静音自动停止 -> 识别 -> 模型回答显示在屏幕上。
 *
 * 界面自上而下：
 *   顶栏   聊天模型名 / 麦克风设备 / 本轮耗时
 *   正文   对话记录，按像素宽度折行（中英混排），自动滚到最后
 *   底栏   状态文案 + 按键提示
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

#include "voiceai.h"
#include "via_pipeline.h"

/* 自有配置：不写进 minuisettings.txt。
 * 原因和 ai-translate 一样 —— CFG_sync() 会按调用方自己的内存结构整个重写
 * minuisettings.txt，而卡上的 nextui.elf / settings.elf 不认识这些键，
 * 每次开机都会把它们抹掉（实测 8 行 -> 0 行）。 */
#define VIA_CFG_PATH SHARED_USERDATA_PATH "/voice-ai.txt"
/* 复用的旧配置：只从里面捞 key */
#define VIA_LEGACY_CFG SHARED_USERDATA_PATH "/ai-translate.txt"

/* ------------------------------------------------------------------ 默认值 */

static const char* VIA_ASR_DEFAULT = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
static const char* VIA_CHAT_DEFAULT = "https://api.deepseek.com/chat/completions";
static const char* VIA_CHAT_FALLBACK =
	"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";

static const char* VIA_SYS_PROMPT_DEFAULT =
	"你是一个装在掌机里的中文语音助手。用户是对着麦克风说话，识别结果可能有错别字，"
	"请按上下文理解他的意思。回答要口语化、直接、简洁：默认两三句话以内，"
	"不要用 markdown 标题和列表（屏幕很窄），需要分点时用「1. 2. 3.」。"
	"拿不准的事实要直说不知道，不要编。";

#define VIA_CFG_MAX_LINES 64
#define VIA_CFG_MAX_LEN   1024

typedef struct {
	char line[VIA_CFG_MAX_LINES][VIA_CFG_MAX_LEN];
	int  n;
} ViaCfgFile;

static char* via_trim(char* s) {
	while (*s == ' ' || *s == '\t') s++;
	char* e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	*e = '\0';
	return s;
}

static void via_copy(char* dst, int cap, const char* src) {
	if (!src) { dst[0] = '\0'; return; }
	snprintf(dst, cap, "%s", src);
}

/* 读一个 .txt 配置成 "key=value" 行数组 */
static void via_cfg_read(const char* path, ViaCfgFile* f) {
	f->n = 0;
	FILE* fp = fopen(path, "rb");
	if (!fp) return;
	char buf[VIA_CFG_MAX_LEN];
	while (fgets(buf, sizeof(buf), fp) && f->n < VIA_CFG_MAX_LINES) {
		int len = (int)strlen(buf);
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
		snprintf(f->line[f->n], VIA_CFG_MAX_LEN, "%s", buf);
		f->n++;
	}
	fclose(fp);
}

/* 在文件里找 key 的值（跳过注释行）。找到返回 1 */
static int via_cfg_get(const ViaCfgFile* f, const char* key, char* out, int cap) {
	for (int i = 0; i < f->n; i++) {
		const char* p = f->line[i];
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == ';' || *p == '\0') continue;

		size_t klen = strlen(key);
		if (strncmp(p, key, klen) != 0) continue;
		const char* q = p + klen;
		while (*q == ' ' || *q == '\t') q++;
		if (*q != '=') continue;
		q++;
		while (*q == ' ' || *q == '\t') q++;
		snprintf(out, cap, "%s", q);
		via_trim(out);
		/* 值可能被引号包着 */
		int n = (int)strlen(out);
		if (n >= 2 && ((out[0] == '"' && out[n-1] == '"') || (out[0] == '\'' && out[n-1] == '\''))) {
			memmove(out, out + 1, n - 2);
			out[n-2] = '\0';
		}
		return 1;
	}
	return 0;
}

static int via_cfg_get_int(const ViaCfgFile* f, const char* key, int def) {
	char v[64];
	if (!via_cfg_get(f, key, v, sizeof(v))) return def;
	if (!v[0]) return def;
	int n = atoi(v);
	return n > 0 ? n : def;   /* 0/负数一律当没写，走默认 */
}

/*
 * 取字符串配置，但「写了空值」也和「没写」一样走默认。
 * 部署脚本生成的配置文件里 voiceChatModel= 就是空的，指望它走默认 ——
 * 之前用 via_cfg_get 判断会把空值当成有效值，把默认模型覆盖成空字符串。
 */
static void via_cfg_default(const ViaCfgFile* f, const char* key, char* dst, int cap,
                            const char* def) {
	char v[1024];
	via_copy(dst, cap, def);
	if (via_cfg_get(f, key, v, sizeof(v)) && v[0]) via_copy(dst, cap, v);
}

/* ------------------------------------------------------------------ 配置读写 */

static ViaCfgFile g_file;      /* 原文件按行留着，回写时只改我们管的键 */

int via_config_load(ViaConfig* cfg) {
	memset(cfg, 0, sizeof(*cfg));

	via_cfg_read(VIA_CFG_PATH, &g_file);
	ViaCfgFile legacy;
	via_cfg_read(VIA_LEGACY_CFG, &legacy);

	char v[1024];

	/* ---- key：优先自有文件，没有就复用 ai-translate.txt 的 ---- */
	char bkey[VIA_MAX_KEY] = {0}, dkey[VIA_MAX_KEY] = {0}, gkey[VIA_MAX_KEY] = {0};
	via_cfg_get(&legacy, "aiBailianKey", bkey, sizeof(bkey));
	via_cfg_get(&legacy, "aiDeepseekKey", dkey, sizeof(dkey));
	via_cfg_get(&legacy, "aiApiKey", gkey, sizeof(gkey));

	char asr_key[VIA_MAX_KEY] = {0}, chat_key[VIA_MAX_KEY] = {0};
	via_cfg_get(&g_file, "voiceAsrKey", asr_key, sizeof(asr_key));
	via_cfg_get(&g_file, "voiceChatKey", chat_key, sizeof(chat_key));

	if (!asr_key[0])  via_copy(asr_key, sizeof(asr_key), bkey[0] ? bkey : gkey);
	if (!chat_key[0]) via_copy(chat_key, sizeof(chat_key), dkey[0] ? dkey : (gkey[0] ? gkey : bkey));

	via_copy(cfg->asr_key, sizeof(cfg->asr_key), asr_key);
	via_copy(cfg->chat_key, sizeof(cfg->chat_key), chat_key);

	/* ---- 端点 ---- */
	via_cfg_default(&g_file, "voiceAsrEndpoint", cfg->asr_endpoint,
	                sizeof(cfg->asr_endpoint), VIA_ASR_DEFAULT);

	/* ---- 模型 ----
	 * 用哪家的 key 决定默认的对话端点/模型：
	 *   优先看有没有 DeepSeek 的 key（aiDeepseekKey / voiceChatKey 配的是 DeepSeek）
	 *   否则退回百炼的 OpenAI 兼容端点 */
	via_cfg_default(&g_file, "voiceAsrModel", cfg->asr_model,
	                sizeof(cfg->asr_model), "qwen3-asr-flash");

	{
		/*
		 * 对话端点/模型的默认值按这个顺序推断：
		 *   1. 配置文件里明确写了 → 用写的
		 *   2. 用的就是 ai-translate.txt 里那把 DeepSeek key → DeepSeek
		 *   3. 用户在 voiceChatKey 里自己塞了别家的 key（上面两条都不满足）
		 *      → 只能按「不是 DeepSeek 就是百炼」猜。DeepSeek 的兼容接口
		 *        只认 deepseek-* 模型名，猜错了第一轮就 400，所以补一条兜底。
		 */
		int chat_is_deepseek = (chat_key[0] && dkey[0] && !strcmp(chat_key, dkey));
		const char* ep = chat_is_deepseek ? VIA_CHAT_DEFAULT : VIA_CHAT_FALLBACK;
		const char* mo = chat_is_deepseek ? "deepseek-chat" : "qwen-plus";
		via_cfg_default(&g_file, "voiceChatEndpoint", cfg->chat_endpoint,
		                sizeof(cfg->chat_endpoint), ep);
		via_cfg_default(&g_file, "voiceChatModel", cfg->chat_model,
		                sizeof(cfg->chat_model), mo);

		/* 端点里带 deepseek，但模型名不是 deepseek-* → 多半配错了，纠回来 */
		if (strstr(cfg->chat_endpoint, "deepseek") &&
		    strncmp(cfg->chat_model, "deepseek", 8) != 0) {
			via_cfg_default(&g_file, "voiceChatModel", cfg->chat_model,
			                sizeof(cfg->chat_model), "deepseek-chat");
		}
	}

	via_cfg_default(&g_file, "voiceSystemPrompt", cfg->system_prompt,
	                sizeof(cfg->system_prompt), VIA_SYS_PROMPT_DEFAULT);

	/* ---- 录音 ---- */
	via_cfg_default(&g_file, "voiceMicDevice", cfg->mic_device,
	                sizeof(cfg->mic_device), "hw:0,0");
	cfg->sample_rate     = via_cfg_get_int(&g_file, "voiceSampleRate", 16000);
	cfg->silence_ms      = via_cfg_get_int(&g_file, "voiceSilenceMs", 1200);
	cfg->min_ms          = via_cfg_get_int(&g_file, "voiceMinMs", 400);
	cfg->max_secs        = via_cfg_get_int(&g_file, "voiceMaxSecs", 30);
	cfg->vad_threshold   = via_cfg_get_int(&g_file, "voiceVadThreshold", 500);
	cfg->chat_max_tokens = via_cfg_get_int(&g_file, "voiceMaxTokens", 600);
	cfg->timeout_secs    = via_cfg_get_int(&g_file, "voiceTimeoutSecs", 30);
	cfg->history_turns   = via_cfg_get_int(&g_file, "voiceHistoryTurns", 6);
	cfg->debug           = via_cfg_get_int(&g_file, "voiceDebug", 0);

	/* 越界保护：这些值会直接被拿去算缓冲和循环次数 */
	if (cfg->sample_rate < 8000 || cfg->sample_rate > 48000) cfg->sample_rate = 16000;
	if (cfg->silence_ms < 300 || cfg->silence_ms > 5000)      cfg->silence_ms = 1200;
	if (cfg->min_ms < 100 || cfg->min_ms > 3000)              cfg->min_ms = 400;
	if (cfg->max_secs < 2 || cfg->max_secs > 120)             cfg->max_secs = 30;
	if (cfg->vad_threshold < 50)                              cfg->vad_threshold = 50;
	if (cfg->vad_threshold > 20000)                           cfg->vad_threshold = 20000;
	if (cfg->chat_max_tokens < 64)                            cfg->chat_max_tokens = 64;
	if (cfg->chat_max_tokens > 4096)                          cfg->chat_max_tokens = 4096;
	if (cfg->timeout_secs < 10)                               cfg->timeout_secs = 10;
	if (cfg->timeout_secs > 120)                              cfg->timeout_secs = 120;
	if (cfg->history_turns < 0)                               cfg->history_turns = 0;
	if (cfg->history_turns > 20)                              cfg->history_turns = 20;

	return 0;
}

/* 回写：保留用户的注释和其它行，只替换/补写我们管的那几个键 */
static const char* g_keys[] = {
	"voiceAsrModel", "voiceAsrEndpoint", "voiceAsrKey",
	"voiceChatModel", "voiceChatEndpoint", "voiceChatKey",
	"voiceMicDevice", "voiceSampleRate", "voiceSilenceMs", "voiceMinMs",
	"voiceMaxSecs", "voiceVadThreshold", "voiceMaxTokens", "voiceTimeoutSecs",
	"voiceHistoryTurns", "voiceSystemPrompt", "voiceDebug", NULL
};

int via_config_save(const ViaConfig* cfg) {
	FILE* f = fopen(VIA_CFG_PATH, "wb");
	if (!f) return -1;

	if (g_file.n == 0) {
		fprintf(f,
			"# NextUI Voice AI 配置（语音输入，文字输出）\n"
			"# 改完存盘、卡插回设备即可生效。\n"
			"#\n"
			"# 只想借用 AI 画面翻译那把 key 的话，这里什么都不用写 ——\n"
			"# 下面 voiceAsrKey / voiceChatKey 留空时会自动去读\n"
			"# .userdata/shared/ai-translate.txt 里的 aiBailianKey / aiDeepseekKey。\n");
	}

	int written[64] = {0};
	int nkeys = 0;
	while (g_keys[nkeys]) nkeys++;

	for (int i = 0; i < g_file.n; i++) {
		char tmp[VIA_CFG_MAX_LEN];
		snprintf(tmp, sizeof(tmp), "%s", g_file.line[i]);
		char* p = via_trim(tmp);
		if (*p == '#' || *p == ';' || *p == '\0') { fprintf(f, "%s\n", g_file.line[i]); continue; }

		char* eq = strchr(p, '=');
		if (!eq) { fprintf(f, "%s\n", g_file.line[i]); continue; }
		*eq = '\0';
		char* key = via_trim(p);
		char* val = via_trim(eq + 1);

		int found = -1;
		for (int k = 0; k < nkeys; k++) {
			if (!strcmp(key, g_keys[k])) { found = k; break; }
		}
		if (found < 0) { fprintf(f, "%s\n", g_file.line[i]); continue; }

		/* 用当前值重写这一行 */
		char out[1024];
		if (!strcmp(key, "voiceAsrModel"))     via_copy(out, sizeof(out), cfg->asr_model);
		else if (!strcmp(key, "voiceAsrEndpoint")) via_copy(out, sizeof(out), cfg->asr_endpoint);
		else if (!strcmp(key, "voiceAsrKey"))  via_copy(out, sizeof(out), cfg->asr_key);
		else if (!strcmp(key, "voiceChatModel")) via_copy(out, sizeof(out), cfg->chat_model);
		else if (!strcmp(key, "voiceChatEndpoint")) via_copy(out, sizeof(out), cfg->chat_endpoint);
		else if (!strcmp(key, "voiceChatKey")) via_copy(out, sizeof(out), cfg->chat_key);
		else if (!strcmp(key, "voiceMicDevice")) via_copy(out, sizeof(out), cfg->mic_device);
		else if (!strcmp(key, "voiceSystemPrompt")) via_copy(out, sizeof(out), cfg->system_prompt);
		else if (!strcmp(key, "voiceSampleRate"))     snprintf(out, sizeof(out), "%d", cfg->sample_rate);
		else if (!strcmp(key, "voiceSilenceMs"))      snprintf(out, sizeof(out), "%d", cfg->silence_ms);
		else if (!strcmp(key, "voiceMinMs"))          snprintf(out, sizeof(out), "%d", cfg->min_ms);
		else if (!strcmp(key, "voiceMaxSecs"))        snprintf(out, sizeof(out), "%d", cfg->max_secs);
		else if (!strcmp(key, "voiceVadThreshold"))   snprintf(out, sizeof(out), "%d", cfg->vad_threshold);
		else if (!strcmp(key, "voiceMaxTokens"))      snprintf(out, sizeof(out), "%d", cfg->chat_max_tokens);
		else if (!strcmp(key, "voiceTimeoutSecs"))    snprintf(out, sizeof(out), "%d", cfg->timeout_secs);
		else if (!strcmp(key, "voiceHistoryTurns"))   snprintf(out, sizeof(out), "%d", cfg->history_turns);
		else if (!strcmp(key, "voiceDebug"))          snprintf(out, sizeof(out), "%d", cfg->debug);
		else snprintf(out, sizeof(out), "%s", val);

		fprintf(f, "%s=%s\n", key, out);
		written[found] = 1;
	}

	/* 缺的键补上 */
	for (int k = 0; k < nkeys; k++) {
		if (written[k]) continue;
		const char* key = g_keys[k];
		char out[1024];
		if (!strcmp(key, "voiceAsrModel"))     via_copy(out, sizeof(out), cfg->asr_model);
		else if (!strcmp(key, "voiceAsrEndpoint")) via_copy(out, sizeof(out), cfg->asr_endpoint);
		else if (!strcmp(key, "voiceAsrKey"))  via_copy(out, sizeof(out), cfg->asr_key);
		else if (!strcmp(key, "voiceChatModel")) via_copy(out, sizeof(out), cfg->chat_model);
		else if (!strcmp(key, "voiceChatEndpoint")) via_copy(out, sizeof(out), cfg->chat_endpoint);
		else if (!strcmp(key, "voiceChatKey")) via_copy(out, sizeof(out), cfg->chat_key);
		else if (!strcmp(key, "voiceMicDevice")) via_copy(out, sizeof(out), cfg->mic_device);
		else if (!strcmp(key, "voiceSystemPrompt")) via_copy(out, sizeof(out), cfg->system_prompt);
		else if (!strcmp(key, "voiceSampleRate"))     snprintf(out, sizeof(out), "%d", cfg->sample_rate);
		else if (!strcmp(key, "voiceSilenceMs"))      snprintf(out, sizeof(out), "%d", cfg->silence_ms);
		else if (!strcmp(key, "voiceMinMs"))          snprintf(out, sizeof(out), "%d", cfg->min_ms);
		else if (!strcmp(key, "voiceMaxSecs"))        snprintf(out, sizeof(out), "%d", cfg->max_secs);
		else if (!strcmp(key, "voiceVadThreshold"))   snprintf(out, sizeof(out), "%d", cfg->vad_threshold);
		else if (!strcmp(key, "voiceMaxTokens"))      snprintf(out, sizeof(out), "%d", cfg->chat_max_tokens);
		else if (!strcmp(key, "voiceTimeoutSecs"))    snprintf(out, sizeof(out), "%d", cfg->timeout_secs);
		else if (!strcmp(key, "voiceHistoryTurns"))   snprintf(out, sizeof(out), "%d", cfg->history_turns);
		else if (!strcmp(key, "voiceDebug"))          snprintf(out, sizeof(out), "%d", cfg->debug);
		else out[0] = '\0';
		fprintf(f, "%s=%s\n", key, out);
	}

	fclose(f);
	return 0;
}

/* ------------------------------------------------------------------ 渲染 */

static SDL_Surface* screen;
static ViaPipeline* g_pipe = NULL;   /* main 里赋值，供绘制辅助函数读音量 */

#define COLOR_DIM   ((SDL_Color){150, 150, 158, 255})
#define COLOR_META  ((SDL_Color){120, 200, 255, 255})
#define COLOR_ERR   ((SDL_Color){255, 130, 130, 255})
#define COLOR_WARN  ((SDL_Color){255, 210, 120, 255})

/* TTF 的度量回调，喂给 via_wrap_text */
static int via_measure(void* ctx, const char* s, int len) {
	(void)ctx;
	char tmp[4096];
	if (len >= (int)sizeof(tmp)) len = (int)sizeof(tmp) - 1;
	memcpy(tmp, s, len);
	tmp[len] = '\0';
	int w = 0, h = 0;
	if (TTF_SizeUTF8(font.small, tmp, &w, &h) != 0) return -1;
	return w;
}

static void via_blit_text(TTF_Font* f, const char* text, int x, int y, SDL_Color col) {
	if (!f || !text || !text[0]) return;
	SDL_Surface* t = TTF_RenderUTF8_Blended(f, text, col);
	if (!t) return;
	SDL_Rect d = {x, y, 0, 0};
	SDL_BlitSurface(t, NULL, screen, &d);
	SDL_FreeSurface(t);
}

static int via_text_w(TTF_Font* f, const char* text) {
	if (!f || !text || !text[0]) return 0;
	int w = 0, h = 0;
	if (TTF_SizeUTF8(f, text, &w, &h) != 0) return 0;
	return w;
}

/* 定高的一行文字（不折行） */
static void via_row(const char* text, int x, int* y, SDL_Color col, TTF_Font* f) {
	via_blit_text(f, text, x, *y, col);
	*y += TTF_FontHeight(f) + SCALE1(2);
}

/*
 * 把一段文字折行后画出来，最多 max_h 像素高。
 * 返回画完之后的 y。超出的部分不画（调用方负责决定裁剪策略）。
 */
static int via_block(const char* text, int x, int* y, int width, int max_h,
                     SDL_Color col, TTF_Font* f) {
	if (!text || !text[0] || max_h <= 0) return *y;

	char** lines = NULL;
	int cap = 0;
	int n = via_wrap_text(text, width, via_measure, NULL, &lines, &cap, 512);
	int lh = TTF_FontHeight(f) + SCALE1(1);

	int drawn = 0;
	for (int i = 0; i < n; i++) {
		if (*y + lh > max_h) break;
		via_blit_text(f, lines[i], x, *y, col);
		*y += lh;
		drawn++;
	}
	if (drawn < n && *y + lh <= max_h) {
		/* 被截断了，给个省略提示 */
		via_blit_text(f, "…", x, *y, COLOR_DIM);
		*y += lh;
	}
	via_wrap_free(lines, &cap);
	return *y;
}

/* 顶栏：模型 / 麦克风 / 耗时 */
static void via_draw_header(const ViaConfig* cfg, ViaPipeline* pipe) {
	int W = screen->w;
	int h = SCALE1(22);
	SDL_Rect bar = {0, 0, W, h};
	SDL_FillRect(screen, &bar, SDL_MapRGB(screen->format, 18, 18, 24));

	char left[256];
	snprintf(left, sizeof(left), "Voice AI   %s", cfg->chat_model);
	via_blit_text(font.tiny, left, SCALE1(6), SCALE1(4), COLOR_META);

	char mic[64] = {0};
	via_pipeline_mic_name(pipe, mic, sizeof(mic));
	char right[256];
	int stt = SDL_AtomicGet(&pipe->stt_ms);
	int chat = SDL_AtomicGet(&pipe->chat_ms);
	if (stt || chat) {
		snprintf(right, sizeof(right), "%s   %.1fs / %.1fs", mic, stt / 1000.0, chat / 1000.0);
	} else {
		snprintf(right, sizeof(right), "%s", mic);
	}
	via_blit_text(font.tiny, right, W - via_text_w(font.tiny, right) - SCALE1(6), SCALE1(4), COLOR_DIM);
}

/* 底栏：状态 + 按键提示 */
static void via_draw_footer(const char* status, int state) {
	int W = screen->w, H = screen->h;
	int h = SCALE1(26);
	SDL_Rect bar = {0, H - h, W, h};
	SDL_FillRect(screen, &bar, SDL_MapRGB(screen->format, 18, 18, 24));

	SDL_Color sc = (state == VIA_ST_ERROR) ? COLOR_ERR : COLOR_DIM;
	via_blit_text(font.tiny, status, SCALE1(6), H - h + SCALE1(7), sc);

	const char* hint =
		(state == VIA_ST_RECORDING) ? "B 结束" :
		(state == VIA_ST_IDLE || state == VIA_ST_DONE || state == VIA_ST_ERROR)
			? "B 说话   Y 清空   MENU 退出" : "B 取消";
	via_blit_text(font.tiny, hint, W - via_text_w(font.tiny, hint) - SCALE1(6),
	              H - h + SCALE1(7), COLOR_DIM);
}

/* 录音音量条：让用户确认麦克风真的收到了声音。
 * g_pipe 在 main 里赋值 —— 这个绘制辅助函数只被主循环调用。 */
static void via_draw_meter(int x, int y, int w) {
	int lv = g_pipe ? via_pipeline_level(g_pipe) : 0;
	int h = SCALE1(8);
	SDL_Rect back = {x, y, w, h};
	SDL_FillRect(screen, &back, SDL_MapRGB(screen->format, 42, 42, 52));

	int fill_w = w * lv / 100;
	if (fill_w > 0) {
		SDL_Rect fill = {x, y, fill_w, h};
		/* 音量越大越偏绿；贴近 100 转黄提示可能爆音 */
		Uint32 col = (lv > 92) ? SDL_MapRGB(screen->format, 230, 200, 80)
		                       : SDL_MapRGB(screen->format, 90, 210, 120);
		SDL_FillRect(screen, &fill, col);
	}
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;

	PWR_setCPUSpeed(CPU_SPEED_PERFORMANCE);   /* 采集 + 网络，别省这点电 */
	screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();

	ViaConfig cfg;
	via_config_load(&cfg);

	ViaPipeline pipe;
	g_pipe = &pipe;
	if (via_pipeline_init(&pipe, &cfg) != 0) {
		GFX_quit();
		return EXIT_FAILURE;
	}

	int have_keys = (cfg.asr_key[0] && cfg.chat_key[0]);
	if (!have_keys) {
		via_pipeline_set_status(&pipe, "没找到 API key，见下面提示");
	} else {
		via_pipeline_set_status(&pipe, "准备就绪，按 B 说话");
	}

	int quit = 0;
	uint32_t last_draw = 0;
	int state_prev = -1;

	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		if (PAD_tappedMenu(SDL_GetTicks())) quit = 1;

		int state = via_pipeline_state(&pipe);

		/* ---------------- 按键 ---------------- */
		if (!quit) {
			if (state == VIA_ST_RECORDING) {
				if (PAD_justPressed(BTN_B)) via_pipeline_stop(&pipe);
			} else if (state == VIA_ST_IDLE || state == VIA_ST_DONE || state == VIA_ST_ERROR) {
				if (PAD_justPressed(BTN_B)) {
					if (!have_keys) {
						via_pipeline_set_status(&pipe, "没配 key，看屏幕提示");
					} else if (via_pipeline_start(&pipe) != 0) {
						via_pipeline_set_status(&pipe, "上一轮还没结束");
					}
				} else if (PAD_justPressed(BTN_Y)) {
					via_history_clear(&pipe.hist);
					via_history_save(&pipe.hist);
					SDL_AtomicSet(&pipe.state, VIA_ST_IDLE);
					via_pipeline_set_status(&pipe, "已清空对话");
				}
			} else {
				/* 识别/对话中：B 放弃本轮 */
				if (PAD_justPressed(BTN_B)) via_pipeline_stop(&pipe);
			}
		}

		/* ---------------- 取状态 ---------------- */
		char status[256] = {0};
		via_pipeline_status(&pipe, status, sizeof(status));
		int st = via_pipeline_state(&pipe);
		if (st == VIA_ST_ERROR) {
			char err[512] = {0};
			via_pipeline_error(&pipe, err, sizeof(err));
			if (err[0]) snprintf(status, sizeof(status), "%s", err);
		}

		/* ---------------- 画 ---------------- */
		uint32_t now = SDL_GetTicks();
		int need = (st == VIA_ST_RECORDING) || (st != state_prev) || (now - last_draw > 250);
		if (!need) {
			GFX_sync();
			continue;
		}
		last_draw = now;
		state_prev = st;

		GFX_clear(screen);
		via_draw_header(&cfg, &pipe);

		int W = screen->w, H = screen->h;
		int pad = SCALE1(12);
		int top = SCALE1(28);
		int bottom = H - SCALE1(32);
		int body_w = W - pad * 2;
		int y = top;

		if (!have_keys) {
			/* 没 key 时把怎么配直接写在屏幕上，省得用户去翻文档 */
			via_row("还没找到 API key", pad, &y, COLOR_WARN, font.small);
			y += SCALE1(4);
			y = via_block(
				"最省事的办法：你之前配过 AI 画面翻译的话，"
				"那把 key 会被自动复用，不用做任何事。\n\n"
				"否则在卡上建一个文件：\n"
				"  .userdata/shared/voice-ai.txt\n"
				"里面写两行：\n"
				"  voiceAsrKey=sk-你的百炼key\n"
				"  voiceChatKey=sk-你的DeepSeek或百炼key\n\n"
				"存盘插回设备重启本工具即可。",
				pad, &y, body_w, bottom, COLOR_DIM, font.small);
			via_draw_footer(status, st);
			GFX_flip(screen);
			continue;
		}

		/* 录音中：先给个醒目的提示和音量条 */
		if (st == VIA_ST_RECORDING) {
			int rec_ms = via_pipeline_rec_ms(&pipe);
			char line[128];
			snprintf(line, sizeof(line), "● 正在录音  %d.%d 秒", rec_ms / 1000, (rec_ms % 1000) / 100);
			via_row(line, pad, &y, COLOR_ERR, font.small);
			y += SCALE1(4);
			via_draw_meter(pad, y, body_w);
			y += SCALE1(18);
			y += SCALE1(6);
		}

		/* 当前这一轮：用户 + AI 回答 */
		char user[VIA_TURN_CHARS] = {0}, ai[VIA_TURN_CHARS] = {0};
		via_pipeline_user_text(&pipe, user, sizeof(user));
		via_pipeline_ai_text(&pipe, ai, sizeof(ai));

		if (user[0]) {
			via_row("你说", pad, &y, COLOR_META, font.tiny);
			y = via_block(user, pad, &y, body_w, bottom, COLOR_WHITE, font.small);
			y += SCALE1(6);
		}
		if (ai[0]) {
			via_row("AI", pad, &y, COLOR_META, font.tiny);
			y = via_block(ai, pad, &y, body_w, bottom - (st == VIA_ST_THINKING ? SCALE1(16) : 0),
			              COLOR_WHITE, font.small);
			y += SCALE1(6);
		}

		/* 流式回答时的光标/等待提示 */
		if (st == VIA_ST_THINKING && y + SCALE1(14) <= bottom) {
			int streaming = SDL_AtomicGet(&pipe.chat_streaming);
			const char* dots[] = {"·  ", "·· ", "···"};
			char tip[32];
			snprintf(tip, sizeof(tip), "%s", streaming ? dots[(now / 300) % 3] : "在想……");
			via_row(tip, pad, &y, COLOR_DIM, font.small);
		}

		/* 顶上还有空间就补历史轮次（从最近往回找，能塞几轮塞几轮） */
		if (y < bottom - SCALE1(20) && pipe.hist.count > 0) {
			/* 当前这一轮如果已经显示过，历史就从它前面一轮开始 */
			int start = pipe.hist.count - 1;
			if (user[0] && ai[0]) start = pipe.hist.count - 2;

			/* 先量出「历史区」还剩多少高度 */
			int hist_h = bottom - y;
			if (hist_h > SCALE1(30)) {
				/* 从最近往回收集能放下的轮次 */
				int first_shown = -1;
				int used = 0;
				int lh = TTF_FontHeight(font.small) + SCALE1(1);
				int head_lh = TTF_FontHeight(font.tiny) + SCALE1(2);
				for (int i = start; i >= 0; i--) {
					const ViaTurn* t = via_history_at(&pipe.hist, i);
					if (!t || !t->user[0]) break;
					char** L = NULL; int c = 0;
					int nu = via_wrap_text(t->user, body_w, via_measure, NULL, &L, &c, 512);
					via_wrap_free(L, &c);
					char** L2 = NULL; int c2 = 0;
					int na = via_wrap_text(t->ai, body_w, via_measure, NULL, &L2, &c2, 512);
					via_wrap_free(L2, &c2);
					int h = head_lh + nu * lh + SCALE1(4) + head_lh + na * lh + SCALE1(8);
					if (used + h > hist_h) break;
					used += h;
					first_shown = i;
				}
				if (first_shown >= 0) {
					if (first_shown > 0) {
						via_blit_text(font.tiny, "（上面还有更早的对话）", pad, y, COLOR_DIM);
						y += TTF_FontHeight(font.tiny) + SCALE1(2);
					}
					for (int i = first_shown; i <= start; i++) {
						const ViaTurn* t = via_history_at(&pipe.hist, i);
						if (!t) break;
						via_row("你说", pad, &y, COLOR_META, font.tiny);
						y = via_block(t->user, pad, &y, body_w, bottom, COLOR_DIM, font.small);
						via_row("AI", pad, &y, COLOR_META, font.tiny);
						y = via_block(t->ai, pad, &y, body_w, bottom, COLOR_DIM, font.small);
						y += SCALE1(6);
					}
				}
			}
		}

		/* 完全空的时候给引导 */
		if (!user[0] && !ai[0] && pipe.hist.count == 0 && st != VIA_ST_RECORDING) {
			y = via_block(
				"按 B 开始说话。\n\n"
				"对着机身底部的小圆孔（麦克风）正常说话就行，"
				"停下来会自动结束录音，识别出的文字和 AI 的回答都会显示在这里。\n\n"
				"回答只显示文字，不出声。\n\n"
				"Y = 清空对话重新开始\n"
				"B = 说话 / 中止本轮\n"
				"MENU = 退出",
				pad, &y, body_w, bottom, COLOR_DIM, font.small);
			(void)y;
		}

		via_draw_footer(status, st);
		GFX_flip(screen);
	}

	via_pipeline_quit(&pipe);
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}

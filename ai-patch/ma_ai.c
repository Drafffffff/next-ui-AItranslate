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
#include "ma_ai.h"

#define AI_TMP_DIR   "/tmp/nextui_ai"
#define AI_REQ_PATH  AI_TMP_DIR "/req.json"
#define AI_PNG_PATH  AI_TMP_DIR "/screen.png"
#define AI_RESP_PATH AI_TMP_DIR "/resp.json"

#define AI_MAX_ITEMS 32
#define AI_WAIT_SECS 30          /* 译文停留多久后自动恢复游戏 */

typedef struct {
	char  orig[128];
	char  zh[768];
	float box[4];                /* 归一化 0~1000：左,上,右,下 */
	char  color[16];
	char  align[12];
} AI_Item;

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
	"- 已经是中文、或只是纯数字/符号的，zh 填空字符串 \"\"。\n"
	"- align 看这条文字在它所在位置是靠左还是居中。\n"
	"- 译文尽量短，别超出框的宽度。";

static char* ai_build_request(const char* b64) {
	char prompt[2048];
	char lang[64];
	ai_json_escape(CFG_getAITargetLang(), lang, sizeof(lang));
	snprintf(prompt, sizeof(prompt), AI_PROMPT_FMT, "像素", lang);

	char* esc = (char*)malloc(4096);
	if (!esc) return NULL;
	ai_json_escape(prompt, esc, 4096);

	size_t need = strlen(esc) + strlen(b64) + 1024;
	char* body = (char*)malloc(need);
	if (!body) { free(esc); return NULL; }
	snprintf(body, need,
		"{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":["
		"{\"type\":\"text\",\"text\":\"%s\"},"
		"{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,%s\"}}"
		"]}],\"temperature\":0.1}",
		CFG_getAIModel(), esc, b64);

	free(esc);
	return body;
}

/* ------------------------------------------------------------------ HTTP */

/* 用 curl 子进程发请求。http.c 的 HTTP_post 把 body 直接塞进命令行，
 * 我们的 body 有几百 KB，4KB 的 cmd 缓冲装不下，所以这里写成 @文件 的形式。 */
static int ai_http_post(const char* url, const char* body_path, int timeout_secs) {
	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
		"curl -sS -k -L --connect-timeout %d -m %d "
		"-H 'Content-Type: application/json' "
		"-H 'Authorization: Bearer %s' "
		"--data-binary @%s -o %s -w '%%{http_code}' %s 2>/dev/null",
		timeout_secs, timeout_secs * 3,
		CFG_getAIApiKey(), body_path, AI_RESP_PATH, url);

	FILE* pipe = popen(cmd, "r");
	if (!pipe) return -1;
	char status[16] = {0};
	size_t n = fread(status, 1, sizeof(status) - 1, pipe);
	status[n] = '\0';
	int rc = pclose(pipe);
	if (rc != 0) return -1;
	int code = atoi(status);
	return code > 0 ? code : -1;
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
		p = q + 1;
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
static void ai_draw_one(SDL_Surface* screen, const char* text, int bx0, int by0, int bx1, int by1,
                        const char* color_name, int align_left) {
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

/* ------------------------------------------------------------------ 主流程 */

void Menu_aiTranslate(void) {
	if (!CFG_getAIEnable() || !CFG_getAIApiKey()[0]) {
		ai_notify(screen, "AI Translate 未配置",
			"在 .userdata/shared/minuisettings.txt 里设置 aiEnable=1 和 aiApiKey");
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

	mkdir(AI_TMP_DIR, 0755);
	remove(AI_RESP_PATH);

	SDL_Surface* frozen = ai_grab();
	if (!frozen) {
		ai_notify(screen, "AI Translate：抓帧失败", NULL);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

	if (ai_save_png(frozen, CFG_getAIMaxImageWidth()) != 0) {
		ai_notify(screen, "AI Translate：存 PNG 失败", AI_PNG_PATH);
		SDL_FreeSurface(frozen);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}
	size_t png_len = 0;
	unsigned char* png = ai_read_file(AI_PNG_PATH, &png_len);
	if (!png) {
		ai_notify(screen, "AI Translate：读不到截图", NULL);
		SDL_FreeSurface(frozen);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}
	char* b64 = ai_base64(png, png_len);
	free(png);
	char* body = b64 ? ai_build_request(b64) : NULL;
	free(b64);
	if (!body) {
		ai_notify(screen, "AI Translate：内存不足", NULL);
		SDL_FreeSurface(frozen);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}
	int wrote = 0;
	FILE* rf = fopen(AI_REQ_PATH, "wb");
	if (rf) { wrote = (fwrite(body, 1, strlen(body), rf) == strlen(body)); fclose(rf); }
	free(body);
	if (!wrote) {
		ai_notify(screen, "AI Translate：写不了临时文件", AI_TMP_DIR);
		SDL_FreeSurface(frozen);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

	/* 请求要跑好几秒，先把画面冻住并告诉用户在干嘛 */
	SDL_BlitSurface(frozen, NULL, screen, NULL);
	ai_notify(screen, "AI Translate 翻译中…", "请稍候，几秒钟");

	int code = ai_http_post(CFG_getAIEndpoint(), AI_REQ_PATH, CFG_getAITimeoutSecs());

	/* 无论成败，都用同一张底图重新铺一遍再往上画 */
	SDL_BlitSurface(frozen, NULL, screen, NULL);

	SDL_FreeSurface(frozen);
	if (code != 200) {
		char msg[128];
		snprintf(msg, sizeof(msg), "HTTP %d（%s）", code, CFG_getAIModel());
		ai_notify(screen, "AI Translate 请求失败", msg);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

	size_t resp_len = 0;
	unsigned char* resp = ai_read_file(AI_RESP_PATH, &resp_len);
	SDL_FreeSurface(frozen);
	if (!resp) {
		ai_notify(screen, "AI Translate：空响应", NULL);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

	AI_Item items[AI_MAX_ITEMS];
	int n = ai_parse_items((const char*)resp, items, AI_MAX_ITEMS);
	free(resp);
	SDL_FreeSurface(frozen);
	if (n <= 0) {
		ai_notify(screen, "AI Translate：画面里没找到要翻译的文字", NULL);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

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
		if (x1 - x0 < 4 || y1 - y0 < 4) continue;              /* 框太小，不敢动 */
		if (x1 - x0 > W * 98 / 100 && y1 - y0 > H * 98 / 100) continue;

		px0[drawable] = x0; py0[drawable] = y0; px1[drawable] = x1; py1[drawable] = y1;
		if (ai_sampled_bg(screen, x0, y0, x1, y1, &bg_r[drawable], &bg_g[drawable], &bg_b[drawable]) != 0) {
			bg_r[drawable] = 0; bg_g[drawable] = 0; bg_b[drawable] = 0;
		}
		drawable++;
	}
	SDL_FreeSurface(frozen);
	if (drawable == 0) {
		ai_notify(screen, "AI Translate：坐标都不可用", NULL);
		ai_wait_dismiss(screen, AI_WAIT_SECS);
		return;
	}

	/* 先全部擦干净再画 —— 否则后面那条的擦除会把前面画好的译文抹掉 */
	for (int i = 0; i < drawable; i++) {
		int ex = px0[i] - 6, ey0 = py0[i] - 3, ex1 = px1[i] + 6, ey1 = py1[i] + 5;
		if (ex < 0) ex = 0;
		if (ey0 < 0) ey0 = 0;
		if (ex1 >= W) ex1 = W - 1;
		if (ey1 >= H) ey1 = H - 1;
		SDL_Rect r = {ex, ey0, ex1 - ex + 1, ey1 - ey0 + 1};
		SDL_FillRect(screen, &r, SDL_MapRGB(screen->format,
			(Uint8)bg_r[i], (Uint8)bg_g[i], (Uint8)bg_b[i]));
	}
	int k = 0;
	for (int i = 0; i < n && k < drawable; i++) {
		if (!items[i].zh[0]) continue;
		int x0 = (int)(items[i].box[0] / 1000.0f * W);
		int x1 = (int)(items[i].box[2] / 1000.0f * W);
		int y0 = (int)(items[i].box[1] / 1000.0f * H);
		int y1 = (int)(items[i].box[3] / 1000.0f * H);
		if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
		if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 >= W) x1 = W - 1;
		if (y1 >= H) y1 = H - 1;
		if (x1 - x0 < 4 || y1 - y0 < 4) continue;
		if (x1 - x0 > W * 98 / 100 && y1 - y0 > H * 98 / 100) continue;
		ai_draw_one(screen, items[i].zh, x0, y0, x1, y1, items[i].color,
			!strcmp(items[i].align, "left"));
		k++;
	}

	if (font.tiny) {
		SDL_Color hint = {230, 230, 235, 255};
		SDL_Surface* t = TTF_RenderUTF8_Blended(font.tiny, "按任意键继续", hint);
		if (t) {
			SDL_Rect pill = {screen->w / 2 - t->w / 2 - 10, screen->h - t->h - 18, t->w + 20, t->h + 10};
			SDL_FillRect(screen, &pill, SDL_MapRGB(screen->format, 0, 0, 0));
			SDL_Rect d = {screen->w / 2 - t->w / 2, screen->h - t->h - 13, 0, 0};
			SDL_BlitSurface(t, NULL, screen, &d);
			SDL_FreeSurface(t);
		}
	}
	GFX_flip(screen);
	SDL_FreeSurface(frozen);

	ai_wait_dismiss(screen, AI_WAIT_SECS);
}

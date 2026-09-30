/*
 * 纯逻辑层：UTF-8 处理、按像素宽度折行（中英混排）、SSE 行解析、base64、WAV。
 *
 * 这个文件刻意不 include SDL —— 宿主机上 tools/test_voiceai.c 直接编它就能跑测试，
 * 不用交叉工具链、不用上设备。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "voiceai.h"

/* ------------------------------------------------------------------ UTF-8 */

unsigned via_utf8_next(const char** p) {
	const unsigned char* s = (const unsigned char*)*p;
	unsigned c = s[0];
	int n = 0;
	if (c < 0x80)        { n = 1; }
	else if ((c & 0xE0) == 0xC0) { n = 2; c &= 0x1F; }
	else if ((c & 0xF0) == 0xE0) { n = 3; c &= 0x0F; }
	else if ((c & 0xF8) == 0xF0) { n = 4; c &= 0x07; }
	else                 { n = 1; c = 0xFFFD; }
	for (int i = 1; i < n; i++) {
		if ((s[i] & 0xC0) != 0x80) {          /* 截断的序列，按单字节处理 */
			*p += 1;
			return s[0];
		}
		c = (c << 6) | (s[i] & 0x3F);
	}
	*p += n;
	return c;
}

int via_utf8_truncate(const char* s, int len) {
	if (len <= 0) return 0;
	int i = len;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
	if (i <= 0) return len;
	unsigned char c = (unsigned char)s[i];
	int need = (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
	if (i + need <= len) return len;    /* 完整字符，不用修 */
	return i;
}

/* CJK / 全角标点：可以在任意字符之间断行 */
static int via_is_cjk(unsigned cp) {
	if (cp >= 0x2E80 && cp <= 0x9FFF)  return 1;   /* 部首扩展..CJK 统一表意 */
	if (cp >= 0xAC00 && cp <= 0xD7AF)  return 1;   /* 谚文 */
	if (cp >= 0xF900 && cp <= 0xFAFF)  return 1;   /* CJK 兼容表意 */
	if (cp >= 0xFF00 && cp <= 0xFFEF)  return 1;   /* 全角标点/字母 */
	if (cp >= 0x3000 && cp <= 0x303F)  return 1;   /* CJK 标点 */
	if (cp >= 0x20000 && cp <= 0x3FFFF) return 1;  /* 扩展 B 及以上 */
	return 0;
}

static int via_is_space(unsigned cp) {
	return cp == ' ' || cp == '\t' || cp == 0x3000;  /* 全角空格也算 */
}

/* ------------------------------------------------------------------ 折行 */

/* 量一段子串的宽度；量不了就按 len 像素估（退化路径） */
static int via_measure(ViaMeasureFn measure, void* ctx, const char* s, int len) {
	if (!measure) return len;
	int w = measure(ctx, s, len);
	return w < 0 ? len : w;
}

/*
 * 折行。核心是把「断点」找对：
 *   1. 先贪心地往后吃字符，直到宽度超了
 *   2. 超了就回退到最近一个合法断点
 *      - 拉丁：断在空格处（空格本身不显示）
 *      - CJK：断在任意字符之间
 *   3. 一个断点都没有（单个超宽长单词）就硬切
 *
 * 内存布局：一个文本副本 + 一个行指针数组，原地把副本切开。
 * 行指针数组和文本副本分开 malloc，调用方用 via_wrap_free 还回来。
 * （之前想用 realloc 长行数组，但那样调用方拿不到新指针 —— 别改回去。）
 */
int via_wrap_text(const char* text, int max_width, ViaMeasureFn measure, void* ctx,
                  char*** out_lines, int* out_cap, int lines_cap) {
	if (!out_lines || !out_cap) return 0;
	*out_lines = NULL;
	*out_cap = 0;
	if (!text || max_width <= 0 || lines_cap <= 0) return 0;

	int len = (int)strlen(text);
	char* buf = (char*)malloc(len + 1);
	if (!buf) return 0;
	memcpy(buf, text, len + 1);

	char** lines = (char**)calloc(lines_cap + 1, sizeof(char*));
	if (!lines) { free(buf); return 0; }

	int nlines = 0;
	int i = 0;

	/*
	 * 不变量：每轮至少消费一个字节（或写到 lines_cap 为止），所以一定终止。
	 * 每轮负责把 buf 里 [i, end) 这一段封成第 nlines 行。
	 */
	while (i < len && nlines < lines_cap) {
		if (buf[i] == '\n') {                 /* 空行 */
			buf[i] = '\0';
			lines[nlines++] = buf + i;
			i++;
			continue;
		}

		/* 这一段的强制边界（'\n' 之前） */
		int hard_end = len;
		for (int k = i; k < len; k++) {
			if (buf[k] == '\n') { hard_end = k; break; }
		}

		/* 贪心吃字符，同时记住最近的合法断点 */
		int j = i;
		int break_at = -1;                    /* 下一行的起点 */
		int last_space = -1;
		int last_cjk = -1;

		while (j < hard_end) {
			const char* p = buf + j;
			unsigned cp = via_utf8_next(&p);
			int clen = (int)(p - (buf + j));
			if (j + clen > hard_end) break;

			if (via_measure(measure, ctx, buf + i, (j + clen) - i) > max_width && j > i) {
				if (last_space >= 0)     break_at = last_space;
				else if (last_cjk >= 0)  break_at = last_cjk;
				else                     break_at = j;       /* 单个超宽词，硬切 */
				break;
			}

			if (via_is_space(cp))    last_space = j;
			else if (via_is_cjk(cp)) last_cjk = j + clen;

			j += clen;
			if (j >= hard_end) { break_at = hard_end; break; }
		}

		if (break_at < 0)      break_at = hard_end;          /* 整段都放得下 */
		if (break_at <= i)     break_at = i + 1;             /* 保底：至少前进一个字节 */

		/* 行尾空白吃掉 */
		int end = break_at;
		while (end > i && (buf[end - 1] == ' ' || buf[end - 1] == '\t')) end--;
		if (end <= i) end = i + 1;

		int next;
		/*
		 * 下一行的起点必须在写 '\0' 之前算好 —— 一旦把分隔符（空格或 '\n'）
		 * 覆盖成 '\0'，后面就读不出来了。
		 * 这个坑踩过一次：之前先 buf[end]='\0' 再判断 buf[break_at]=='\n'，
		 * 结果多行文本从第二行起全部变空。
		 */
		if (break_at >= hard_end) {
			next = (hard_end < len) ? hard_end + 1 : len;    /* 跳过硬边界 '\n' */
		} else if (buf[break_at] == ' ') {
			next = break_at + 1;                             /* 断点处的空格丢掉 */
		} else {
			next = break_at;
		}

		buf[end] = '\0';
		lines[nlines++] = buf + i;

		if (next <= i) next = i + 1;
		i = next;
	}

	*out_lines = lines;
	*out_cap = lines_cap + 1;
	return nlines;
}

void via_wrap_free(char** lines, int* cap) {
	(void)cap;
	if (!lines) return;
	if (lines[0]) free(lines[0]);   /* buf 的基址存在第 0 行里 */
	free(lines);
}

/* ------------------------------------------------------------------ SSE */

int via_sse_data_line(const char* line, char* out, int out_cap) {
	if (!line || !out || out_cap <= 0) return 0;
	out[0] = '\0';
	while (*line == ' ') line++;
	if (strncmp(line, "data:", 5) != 0) return 0;   /* id:/event:/: 注释 都跳过 */
	line += 5;
	while (*line == ' ') line++;
	int n = (int)strlen(line);
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
	if (n <= 0) return 0;
	if (n == 6 && !strncmp(line, "[DONE]", 6)) return 0;
	if (n >= out_cap) n = out_cap - 1;
	memcpy(out, line, n);
	out[n] = '\0';
	return n;
}

/* ------------------------------------------------------------------ JSON */

const char* via_json_find(const char* obj, const char* key) {
	char pat[80];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char* p = obj;
	while ((p = strstr(p, pat)) != NULL) {
		const char* q = p + strlen(pat);
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
		if (*q == ':') {
			q++;
			while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
			return q;
		}
		p++;
	}
	return NULL;
}

static void via_utf8_append(char* out, int cap, int* j, unsigned cp) {
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

static unsigned via_hex4(const char* p) {
	unsigned v = 0;
	for (int k = 0; k < 4; k++) {
		char c = p[k];
		if (!isxdigit((unsigned char)c)) break;
		v = v * 16 + (unsigned)(c <= '9' ? c - '0' : (tolower((unsigned char)c) - 'a' + 10));
	}
	return v;
}

int via_json_string(const char* obj, const char* key, char* out, int cap) {
	if (cap <= 0) return -1;
	out[0] = '\0';
	const char* p = via_json_find(obj, key);
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
				case '/': out[j++] = '/';  p++; break;
				case 'u': {
					unsigned cp = via_hex4(p + 1);
					p += 5;
					/* 代理对：高低位合起来 */
					if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
						unsigned lo = via_hex4(p + 2);
						if (lo >= 0xDC00 && lo <= 0xDFFF) {
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							p += 6;
						}
					}
					via_utf8_append(out, cap, &j, cp);
					break;
				}
				default: if (*p) out[j++] = *p++; else out[j++] = '\\'; break;
			}
		} else {
			out[j++] = *p++;
		}
		if (j >= cap - 4) break;
	}
	out[j] = '\0';
	return j;
}

int via_json_int(const char* obj, const char* key, int def) {
	const char* p = via_json_find(obj, key);
	if (!p) return def;
	while (*p == '[' || *p == ' ' || *p == '"') p++;
	if ((*p < '0' || *p > '9') && *p != '-') return def;
	return atoi(p);
}

void via_json_escape(const char* in, char* out, int cap) {
	int j = 0;
	if (!in) { out[0] = '\0'; return; }
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

/* ------------------------------------------------------------------ base64 */

static const char VIA_B64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* via_base64_encode(const unsigned char* in, size_t n) {
	size_t olen = 4 * ((n + 2) / 3);
	char* out = (char*)malloc(olen + 1);
	if (!out) return NULL;
	size_t i = 0, j = 0;
	while (i + 2 < n) {
		unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | in[i + 2];
		out[j++] = VIA_B64[(v >> 18) & 63];
		out[j++] = VIA_B64[(v >> 12) & 63];
		out[j++] = VIA_B64[(v >> 6) & 63];
		out[j++] = VIA_B64[v & 63];
		i += 3;
	}
	if (i < n) {
		unsigned v = (unsigned)in[i] << 16;
		if (i + 1 < n) v |= (unsigned)in[i + 1] << 8;
		out[j++] = VIA_B64[(v >> 18) & 63];
		out[j++] = VIA_B64[(v >> 12) & 63];
		out[j++] = (i + 1 < n) ? VIA_B64[(v >> 6) & 63] : '=';
		out[j++] = '=';
	}
	out[j] = '\0';
	return out;
}

int via_base64_decode(const char* in, int in_len, unsigned char* out, int out_cap) {
	static int rev[256];
	static int init = 0;
	if (!init) {
		memset(rev, -1, sizeof(rev));
		for (int i = 0; i < 64; i++) rev[(unsigned char)VIA_B64[i]] = i;
		rev[(unsigned char)'-'] = 62;   /* URL-safe 变体也认一下 */
		rev[(unsigned char)'_'] = 63;
		init = 1;
	}
	if (in_len < 0) in_len = (int)strlen(in);
	/*
	 * acc 必须是 unsigned。
	 * 用 int 的话累积到第 4 个字符（24 位）时 (acc << 6) 会左移进符号位，
	 * 属有符号溢出 = UB。UBSan 实测会报
	 *   "left shift of 473454708 by 6 places cannot be represented in type 'int'"
	 * 一旦编译器按 UB 优化，解出来的音频就会静默损坏（接口还是 200）。
	 */
	int o = 0, nbits = 0;
	unsigned acc = 0;
	for (int i = 0; i < in_len; i++) {
		int v = rev[(unsigned char)in[i]];
		if (v < 0) {
			if (in[i] == '=' || in[i] == '\n' || in[i] == '\r' || in[i] == ' ') continue;
			return -1;                  /* 非法字符，这块数据坏了 */
		}
		acc = (acc << 6) | (unsigned)v;
		nbits += 6;
		if (nbits >= 8) {
			nbits -= 8;
			if (o >= out_cap) return -1;
			out[o++] = (unsigned char)((acc >> nbits) & 0xFF);
		}
	}
	return o;
}

/* ------------------------------------------------------------------ TTS SSE */
/*
 * ⚠ 当前版本没用到这两块 —— 本工具只做语音输入、文字输出，不合成语音。
 * 保留的原因：tools/tts-sample.sse 是真实抓包，test_voiceai.c 靠它回归
 * 验证「SSE 分片 + base64 累积」不丢字节。这类地方最容易静默出错
 * （第一块漏解就会丢开头一截音频，而接口返回 200 看不出问题）。
 * 以后要加播报功能，接上这两个函数即可。
 */
int via_tts_extract_chunk(const char* json, unsigned char* out, int out_cap) {
	/* 直接定位，不要先往定长缓冲里拷 —— 一块就是 20KB 音频（约 27KB base64），
	 * 拷进小缓冲只会解出开头几个字节，静默丢数据。 */
	const char* p = via_json_find(json, "data");
	if (!p || *p != '"') return 0;
	p++;

	/* 找结尾引号（base64 里不会出现需要转义的字符） */
	const char* q = p;
	while (*q && *q != '"') q++;
	int len = (int)(q - p);
	if (len <= 0) return 0;

	int n = via_base64_decode(p, len, out, out_cap);
	if (n < 0) {
		/* 定长缓冲不够时退回分块解码 */
		unsigned char tmp[8192];
		int total = 0;
		int chunk = (int)sizeof(tmp);
		int i = 0;
		while (i < len) {
			int take = len - i;
			if (take > chunk) take = chunk;
			/* 保持 4 的倍数，避免半个 base64 组 */
			take -= take % 4;
			if (take == 0) break;
			int got = via_base64_decode(p + i, take, tmp, chunk);
			if (got < 0) return -1;
			if (total + got > out_cap) return -1;
			memcpy(out + total, tmp, got);
			total += got;
			i += take;
		}
		return total;
	}
	return n;
}

int via_tts_is_finish(const char* json) {
	char fr[32];
	if (via_json_string(json, "finish_reason", fr, sizeof(fr)) < 0) return 0;
	return strcmp(fr, "stop") == 0;
}

/* ------------------------------------------------------------------ WAV */

static void via_put32(unsigned char* p, unsigned v) {
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)((v >> 8) & 0xFF);
	p[2] = (unsigned char)((v >> 16) & 0xFF);
	p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static void via_put16(unsigned char* p, unsigned v) {
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)((v >> 8) & 0xFF);
}
static unsigned via_get16(const unsigned char* p) { return p[0] | (p[1] << 8); }
static unsigned via_get32(const unsigned char* p) {
	return p[0] | (p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

int via_wav_wrap(unsigned char* buf, int cap, const unsigned char* pcm, int pcm_len,
                 int sample_rate, int channels) {
	if (cap < 44 + pcm_len) return -1;
	int bits = 16;
	int block = channels * bits / 8;
	int byte_rate = sample_rate * block;

	memcpy(buf, "RIFF", 4);
	via_put32(buf + 4, (unsigned)(36 + pcm_len));
	memcpy(buf + 8, "WAVE", 4);
	memcpy(buf + 12, "fmt ", 4);
	via_put32(buf + 16, 16);
	via_put16(buf + 20, 1);                  /* PCM */
	via_put16(buf + 22, (unsigned)channels);
	via_put32(buf + 24, (unsigned)sample_rate);
	via_put32(buf + 28, (unsigned)byte_rate);
	via_put16(buf + 32, (unsigned)block);
	via_put16(buf + 34, (unsigned)bits);
	memcpy(buf + 36, "data", 4);
	via_put32(buf + 40, (unsigned)pcm_len);

	return 44;
}

/*
 * 定出音频数据的起点。
 *
 * 三态返回值：
 *   >0  找到 data 段，就是这个偏移
 *   -1  头还没收全（data 长度字段比已收到的多）→ 调用方继续攒
 *   -2  不是 WAV 或结构坏了 → 调用方把整块当裸 PCM 处理
 *
 * 为什么需要 -1：流式 TTS 的 RIFF/data 长度字段是 0x7FFFFFFF 这种假值，
 * 我们没法用它算结尾；但正因为长度不可信，才能反过来判断「这块还没收完」。
 * 判据是 fmt 是否已经读到 —— fmt 在 data 前面，读到 fmt 了说明位置可信。
 */
int via_wav_data_offset(const unsigned char* buf, int len, int* out_rate,
                        int* out_channels) {
	if (len < 12) return -1;
	if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return -2;

	int have_fmt = 0;
	int off = 12;
	while (off + 8 <= len) {
		const unsigned char* cid = buf + off;
		unsigned csz = via_get32(buf + off + 4);
		int fits = (csz <= (unsigned)(len - off - 8));

		if (!memcmp(cid, "fmt ", 4)) {
			if (off + 8 + 16 > len) return -1;      /* fmt 还没收全 */
			const unsigned char* f = buf + off + 8;
			if (out_channels) *out_channels = (int)via_get16(f + 2);
			if (out_rate)     *out_rate     = (int)via_get32(f + 4);
			have_fmt = 1;
		}

		if (!memcmp(cid, "data", 4)) {
			if (fits) return off + 8;               /* 长度自洽，就是这 */
			return have_fmt ? (off + 8) : -1;       /* 假长度：靠 fmt 定，否则继续攒 */
		}

		if (!fits) return have_fmt ? -1 : -2;       /* 跳不过去，等更多数据 */
		off += 8 + (int)csz + ((int)csz & 1);
	}
	return -1;   /* 走到这说明还没定位到 data 段 */
}

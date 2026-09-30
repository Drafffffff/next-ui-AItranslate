/*
 * HTTP + API 调用，以及整条「录音 -> 识别 -> 对话」流水线。
 * 产品形态是**语音输入、文字输出**，所以没有任何 TTS / 音频播放代码。
 *
 * 下面这些形状都是 2026-09-30 用卡上那把百炼 key 实测过的
 * （实测脚本和响应样本见 tools/），改之前先看注释。
 *
 *   ASR  POST {asr_endpoint}  （默认可直接复用 ai-translate 的键）
 *        {"model":"qwen3-asr-flash","messages":[{"role":"user","content":[
 *           {"type":"input_audio","input_audio":{"data":"data:audio/wav;base64,..."}}]}]}
 *        -> choices[0].message.content = 识别文本
 *           另外 annotations 里会带 [{"language":"zh","emotion":"neutral"}]
 *
 *   Chat POST {chat_endpoint}  标准 OpenAI 格式
 *        流式时 stream:true，响应是 SSE：每行 data: {...}，
 *        正文在 choices[0].delta.content（OpenAI / DeepSeek / 百炼兼容模式都一样）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "defines.h"     /* SDCARD_PATH / PLATFORM —— 下面拼路径要用 */
#include "voiceai.h"
#include "via_api.h"
#include "via_audio.h"

#ifndef VIA_TMP_DIR
#define VIA_TMP_DIR SDCARD_PATH "/.userdata/ai"
#endif
#ifndef VIA_HIST_PATH
#define VIA_HIST_PATH SDCARD_PATH "/.userdata/" PLATFORM "/VoiceAI/history.txt"
#endif

static unsigned via_now_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (unsigned)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* ------------------------------------------------------------------ 文件 */

static unsigned char* via_read_file(const char* path, int* out_len) {
	FILE* f = fopen(path, "rb");
	if (!f) { if (out_len) *out_len = 0; return NULL; }
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len <= 0) { fclose(f); if (out_len) *out_len = 0; return NULL; }
	unsigned char* buf = (unsigned char*)malloc((size_t)len + 1);
	if (!buf) { fclose(f); if (out_len) *out_len = 0; return NULL; }
	size_t got = fread(buf, 1, (size_t)len, f);
	fclose(f);
	buf[got] = '\0';
	if (out_len) *out_len = (int)got;
	return buf;
}

/* ------------------------------------------------------------------ HTTP */

static int via_have_cmd(const char* name) {
	char c[96];
	snprintf(c, sizeof(c), "command -v %s >/dev/null 2>&1", name);
	return system(c) == 0;
}

static void via_read_stderr(const char* path, char* err, int cap) {
	int el = 0;
	unsigned char* e = via_read_file(path, &el);
	if (e) {
		char* nl = strchr((char*)e, '\n');
		if (nl) *nl = '\0';
		snprintf(err, cap, "%s", (char*)e);
		free(e);
	}
}

int via_http_post_file(const char* url, const char* body_path, const char* resp_path,
                       const char* key, int timeout_secs, char* err, int err_cap) {
	char cmd[4096];
	char stderr_path[512];
	char status[256] = {0};
	err[0] = '\0';

	snprintf(stderr_path, sizeof(stderr_path), "%s/stderr.err", VIA_TMP_DIR);

	if (!via_have_cmd("curl")) {
		snprintf(err, err_cap, "设备上找不到 curl");
		return -2;
	}
	if (timeout_secs < 5) timeout_secs = 5;

	/* -N 关掉 curl 的 stdout 缓冲，流式时才能及时拿到每一块 */
	snprintf(cmd, sizeof(cmd),
		"curl -sS -k -L -N --connect-timeout %d -m %d "
		"-H 'Content-Type: application/json' "
		"-H 'Authorization: Bearer %s' "
		"-H 'Expect:' "                        /* 关掉 100-continue，否则状态码和计时会乱 */
		"--data-binary @%s -o %s "
		"-w '%%{http_code} %%{time_total}' "
		"%s 2>%s",
		timeout_secs, timeout_secs * 3,
		key ? key : "", body_path, resp_path, url, stderr_path);

	FILE* pipe = popen(cmd, "r");
	if (!pipe) {
		snprintf(err, err_cap, "起不了 curl 进程");
		return -1;
	}
	size_t n = fread(status, 1, sizeof(status) - 1, pipe);
	status[n] = '\0';
	int rc = pclose(pipe);

	int code = 0;
	double total = 0;
	sscanf(status, "%d %lf", &code, &total);

	if (code <= 0 || rc != 0) {
		via_read_stderr(stderr_path, err, err_cap);
		if (!err[0]) snprintf(err, err_cap, "curl 退出码 %d", rc);
		return code > 0 ? code : -1;
	}
	return code;
}

void via_http_error_text(const char* resp_path, char* out, int cap) {
	out[0] = '\0';
	int len = 0;
	unsigned char* r = via_read_file(resp_path, &len);
	if (!r) return;

	char msg[384] = {0};
	if (via_json_string((char*)r, "message", msg, sizeof(msg)) < 0) {
		via_json_string((char*)r, "error", msg, sizeof(msg));
	}
	if (msg[0]) {
		snprintf(out, cap, "%s", msg);
	} else {
		int take = len < 200 ? len : 200;
		snprintf(out, cap, "%.*s", take, (char*)r);
	}
	for (char* p = out; *p; p++) {
		if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
	}
	free(r);
}

/* ------------------------------------------------------------------ ASR */

int via_stt(const ViaConfig* cfg, const unsigned char* wav, int wav_len,
            char* text, int text_cap, char* err, int err_cap) {
	text[0] = '\0';
	err[0] = '\0';

	char* b64 = via_base64_encode(wav, wav_len);
	if (!b64) { snprintf(err, err_cap, "内存不足（音频 base64）"); return -1; }

	char body_path[512], resp_path[512];
	snprintf(body_path, sizeof(body_path), "%s/stt_req.json", VIA_TMP_DIR);
	snprintf(resp_path, sizeof(resp_path), "%s/stt_resp.json", VIA_TMP_DIR);

	FILE* f = fopen(body_path, "wb");
	if (!f) {
		free(b64);
		snprintf(err, err_cap, "写不了 %s", body_path);
		return -1;
	}
	fprintf(f, "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":["
	           "{\"type\":\"input_audio\",\"input_audio\":{\"data\":"
	           "\"data:audio/wav;base64,", cfg->asr_model);
	fwrite(b64, 1, strlen(b64), f);
	fprintf(f, "\"}}]}]}");
	fclose(f);
	free(b64);

	int code = via_http_post_file(cfg->asr_endpoint, body_path, resp_path,
	                              cfg->asr_key, cfg->timeout_secs, err, err_cap);
	if (code != 200) {
		char detail[384];
		via_http_error_text(resp_path, detail, sizeof(detail));
		if (code > 0) snprintf(err, err_cap, "识别 HTTP %d：%s", code, detail);
		return code > 0 ? code : -1;
	}

	int len = 0;
	unsigned char* resp = via_read_file(resp_path, &len);
	if (!resp) { snprintf(err, err_cap, "读不到识别响应"); return -1; }

	int got = via_json_string((char*)resp, "content", text, text_cap);
	free(resp);
	if (got < 0) { snprintf(err, err_cap, "识别响应里没有 content"); return -1; }

	char* s = text;
	while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
	if (s != text) memmove(text, s, strlen(s) + 1);
	int tl = (int)strlen(text);
	while (tl > 0 && (text[tl-1] == ' ' || text[tl-1] == '\n' ||
	                  text[tl-1] == '\r' || text[tl-1] == '\t')) {
		text[--tl] = '\0';
	}
	return 0;
}

/* ------------------------------------------------------------------ 对话 */

/* 拼请求体。抽出来是因为流式和非流式共用。 */
static int via_chat_build_body(const ViaConfig* cfg, const ViaHistory* hist, int turns,
                               int stream, const char* path) {
	FILE* f = fopen(path, "wb");
	if (!f) return -1;

	char esc[4096];
	fprintf(f, "{\"model\":\"%s\",\"messages\":[", cfg->chat_model);
	via_json_escape(cfg->system_prompt, esc, sizeof(esc));
	fprintf(f, "{\"role\":\"system\",\"content\":\"%s\"}", esc);

	int n = via_history_count(hist);
	int from = (turns < n) ? (n - turns) : 0;
	for (int i = from; i < n; i++) {
		const ViaTurn* t = via_history_at(hist, i);
		if (t->user[0]) {
			via_json_escape(t->user, esc, sizeof(esc));
			fprintf(f, ",{\"role\":\"user\",\"content\":\"%s\"}", esc);
		}
		if (t->ai[0]) {
			via_json_escape(t->ai, esc, sizeof(esc));
			fprintf(f, ",{\"role\":\"assistant\",\"content\":\"%s\"}", esc);
		}
	}
	fprintf(f, "],\"temperature\":0.7,\"max_tokens\":%d", cfg->chat_max_tokens);
	if (stream) fprintf(f, ",\"stream\":true");
	fprintf(f, "}");
	fclose(f);
	return 0;
}

/* 从一条 SSE 的 data JSON 里取增量文字。
 * 兼容两种写法：
 *   OpenAI/DeepSeek/百炼兼容模式：choices[0].delta.content
 *   有些实现会把整段放 message.content（少见，兜底用） */
static int via_chat_delta(const char* json, char* out, int cap) {
	out[0] = '\0';
	const char* d = via_json_find(json, "delta");
	if (d) {
		if (via_json_string(d, "content", out, cap) > 0) return 0;
		out[0] = '\0';
	}
	/* 兜底：非流式结构 */
	const char* m = via_json_find(json, "message");
	if (m && via_json_string(m, "content", out, cap) > 0) return 0;
	out[0] = '\0';
	return -1;
}

int via_chat_stream(const ViaConfig* cfg, const ViaHistory* hist, int turns,
                    ViaDeltaFn on_delta, void* dctx,
                    char* reply, int reply_cap, char* err, int err_cap) {
	reply[0] = '\0';
	err[0] = '\0';

	char body_path[512], resp_path[512], stderr_path[512];
	snprintf(body_path, sizeof(body_path), "%s/chat_req.json", VIA_TMP_DIR);
	snprintf(resp_path, sizeof(resp_path), "%s/chat_resp.json", VIA_TMP_DIR);
	snprintf(stderr_path, sizeof(stderr_path), "%s/chat_stderr.err", VIA_TMP_DIR);

	if (!via_have_cmd("curl")) { snprintf(err, err_cap, "设备上找不到 curl"); return -2; }
	if (via_chat_build_body(cfg, hist, turns, on_delta ? 1 : 0, body_path) != 0) {
		snprintf(err, err_cap, "写不了 %s", body_path);
		return -1;
	}

	int timeout = cfg->timeout_secs;
	if (timeout < 10) timeout = 10;

	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
		"curl -sS -k -L -N --connect-timeout 10 -m %d "
		"-H 'Content-Type: application/json' "
		"-H 'Authorization: Bearer %s' "
		"-H 'Expect:' "
		"--data-binary @%s %s 2>%s",
		timeout * 2, cfg->chat_key, body_path, cfg->chat_endpoint, stderr_path);

	/* 非流式：curl 直接写文件，再整体解析 */
	if (!on_delta) {
		char cmd2[4096];
		int t2 = cfg->timeout_secs;
		if (t2 < 5) t2 = 5;
		snprintf(cmd2, sizeof(cmd2),
			"curl -sS -k -L --connect-timeout 10 -m %d "
			"-H 'Content-Type: application/json' "
			"-H 'Authorization: Bearer %s' -H 'Expect:' "
			"-H 'Expect:' --data-binary @%s -o %s -w '%%{http_code}' %s 2>%s",
			t2 * 2, cfg->chat_key, body_path, resp_path, cfg->chat_endpoint, stderr_path);
		FILE* pipe = popen(cmd2, "r");
		if (!pipe) { snprintf(err, err_cap, "起不了 curl 进程"); return -1; }
		char st[32] = {0};
		size_t n = fread(st, 1, sizeof(st) - 1, pipe);
		st[n] = '\0';
		int prc = pclose(pipe);
		int code = atoi(st);
		if (code != 200) {
			char detail[384];
			via_http_error_text(resp_path, detail, sizeof(detail));
			via_read_stderr(stderr_path, err, err_cap);
			if (code > 0) snprintf(err, err_cap, "对话 HTTP %d：%s", code, detail);
			else if (!err[0]) snprintf(err, err_cap, "curl 退出码 %d", prc);
			return code > 0 ? code : -1;
		}
		int len = 0;
		unsigned char* resp = via_read_file(resp_path, &len);
		if (!resp) { snprintf(err, err_cap, "读不到对话响应"); return -1; }
		const char* m = via_json_find((char*)resp, "message");
		int got = -1;
		if (m) got = via_json_string(m, "content", reply, reply_cap);
		if (got < 0) got = via_json_string((char*)resp, "content", reply, reply_cap);
		free(resp);
		if (got < 0) { snprintf(err, err_cap, "对话响应里没有 content"); return -1; }
		return 0;
	}

	/* 流式：一行行读 SSE，增量和整段一起攒 */
	FILE* pipe = popen(cmd, "r");
	if (!pipe) { snprintf(err, err_cap, "起不了 curl 进程"); return -1; }

	int cap = 64 * 1024;
	char* line = (char*)malloc(cap);
	char* json = (char*)malloc(cap);
	if (!line || !json) {
		pclose(pipe); free(line); free(json);
		snprintf(err, err_cap, "内存不足（对话缓冲）");
		return -1;
	}

	int line_len = 0;
	int total = 0;
	int saw_any = 0;
	int http_err = 0;
	int c;

	while ((c = getc(pipe)) != EOF) {
		if (c != '\n') {
			if (line_len < cap - 1) line[line_len++] = (char)c;
			continue;
		}
		line[line_len] = '\0';
		line_len = 0;

		/* 出错时 DashScope/DeepSeek 会直接回一个错误 JSON，不带 data: 前缀 */
		if (line[0] == '{' && strstr(line, "\"error\"")) {
			char code[64] = {0}, msg[256] = {0};
			via_json_string(line, "code", code, sizeof(code));
			via_json_string(line, "message", msg, sizeof(msg));
			if (!msg[0]) via_json_string(line, "error", msg, sizeof(msg));
			snprintf(err, err_cap, "对话出错 %s %s", code, msg);
			http_err = 1;
			break;
		}

		/* 流结束标记。注意：via_sse_data_line 把 [DONE] 当空行返回 0，
		 * 所以必须在调它之前判断，否则永远等不到收尾。 */
		if (!strncmp(line, "data: [DONE]", 12) || !strncmp(line, "data:[DONE]", 11)) break;

		int jn = via_sse_data_line(line, json, cap);
		if (jn <= 0) continue;

		char delta[1024];
		if (via_chat_delta(json, delta, sizeof(delta)) == 0 && delta[0]) {
			saw_any = 1;
			int dl = (int)strlen(delta);
			if (total + dl < reply_cap - 1) {
				memcpy(reply + total, delta, dl);
				total += dl;
				reply[total] = '\0';
			}
			if (on_delta) on_delta(dctx, delta);
		}
	}

	int rc = pclose(pipe);
	free(line); free(json);

	if (http_err) return -1;
	if (rc != 0) {
		via_read_stderr(stderr_path, err, err_cap);
		if (!err[0]) snprintf(err, err_cap, "curl 退出码 %d", rc);
		return -1;
	}
	if (!saw_any) {
		snprintf(err, err_cap, "模型没有返回任何内容");
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ 历史 */

int via_history_count(const ViaHistory* h) { return h->count; }

const ViaTurn* via_history_at(const ViaHistory* h, int i) {
	if (i < 0 || i >= h->count) return NULL;
	return &h->turn[i];
}

void via_history_push(ViaHistory* h, const char* user, const char* ai) {
	if (h->count >= VIA_MAX_TURNS) {
		memmove(&h->turn[0], &h->turn[1], sizeof(ViaTurn) * (VIA_MAX_TURNS - 1));
		h->count = VIA_MAX_TURNS - 1;
	}
	ViaTurn* t = &h->turn[h->count++];
	snprintf(t->user, sizeof(t->user), "%s", user ? user : "");
	snprintf(t->ai, sizeof(t->ai), "%s", ai ? ai : "");
}

void via_history_clear(ViaHistory* h) { memset(h, 0, sizeof(*h)); }

/* 换行会破坏「一行一轮」的存盘格式，压成空格 */
static void via_flatten(char* s) {
	for (char* p = s; *p; p++) {
		if (*p == '\n' || *p == '\r') *p = ' ';
	}
}

void via_history_save(const ViaHistory* h) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", VIA_HIST_PATH);
	char* slash = strrchr(dir, '/');
	if (slash) { *slash = '\0'; mkdir(dir, 0755); }

	FILE* f = fopen(VIA_HIST_PATH, "wb");
	if (!f) return;
	int n = h->count;
	int from = (n > 20) ? n - 20 : 0;    /* 只留最近 20 轮，文件别无限长 */
	for (int i = from; i < n; i++) {
		char u[VIA_TURN_CHARS], a[VIA_TURN_CHARS];
		snprintf(u, sizeof(u), "%s", h->turn[i].user);
		snprintf(a, sizeof(a), "%s", h->turn[i].ai);
		via_flatten(u);
		via_flatten(a);
		fprintf(f, "U:%s\nA:%s\n", u, a);
	}
	fclose(f);
}

void via_history_load(ViaHistory* h) {
	via_history_clear(h);
	FILE* f = fopen(VIA_HIST_PATH, "rb");
	if (!f) return;
	char line[VIA_TURN_CHARS + 8];
	char user[VIA_TURN_CHARS] = {0};
	while (fgets(line, sizeof(line), f)) {
		int n = (int)strlen(line);
		while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
		if (n < 2) continue;
		if (line[0] == 'U' && line[1] == ':') {
			snprintf(user, sizeof(user), "%s", line + 2);
		} else if (line[0] == 'A' && line[1] == ':') {
			via_history_push(h, user, line + 2);
			user[0] = '\0';
		}
	}
	fclose(f);
}

/* ------------------------------------------------------------------ 状态 */

void via_pipeline_set_status(ViaPipeline* p, const char* fmt, ...) {
	va_list ap;
	pthread_mutex_lock(&p->lock);
	va_start(ap, fmt);
	vsnprintf(p->status, sizeof(p->status), fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&p->lock);
}

void via_pipeline_append_ai(ViaPipeline* p, const char* text) {
	if (!text || !text[0]) return;
	pthread_mutex_lock(&p->lock);
	int cur = (int)strlen(p->ai_text);
	int add = (int)strlen(text);
	if (cur + add < VIA_TURN_CHARS - 1) {
		memcpy(p->ai_text + cur, text, add + 1);
	}
	pthread_mutex_unlock(&p->lock);
}

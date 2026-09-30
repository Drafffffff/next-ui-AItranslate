/*
 * 宿主机测试：不依赖 SDL、不依赖设备、不联网。
 *
 *   cc -O1 -o /tmp/test_voiceai tools/test_voiceai.c workspace/all/voiceai/via_ui.c
 *   /tmp/test_voiceai [TTS的SSE响应文件]
 *
 * 第三个参数可选：传一个真实的 TTS SSE 响应体（curl -H 'X-DashScope-SSE: enable' 存下来的），
 * 用来验证流式音频解析不丢字节。仓库里 tools/测试解析器.sh 就是这么用的。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../workspace/all/voiceai/voiceai.h"

static int g_fail = 0;
#define CHECK(cond, ...) do { \
	if (!(cond)) { g_fail++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } \
	else         { printf("  ✓ "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ---- 假度量：ASCII 按 8px，CJK 按 16px，全角标点按 16px ---- */
static int fake_measure(void* ctx, const char* s, int len) {
	(void)ctx;
	int w = 0, i = 0;
	while (i < len) {
		const char* p = s + i;
		unsigned cp = via_utf8_next(&p);
		i = (int)(p - s);
		w += (cp < 0x80) ? 8 : 16;
	}
	return w;
}

static void dump_lines(const char* title, const char* text, int maxw) {
	char** lines = NULL;
	int cap = 0;
	int n = via_wrap_text(text, maxw, fake_measure, NULL, &lines, &cap, 64);
	printf("\n[%s]  宽<=%dpx  得到 %d 行\n", title, maxw, n);
	for (int i = 0; i < n; i++) {
		int w = fake_measure(NULL, lines[i], (int)strlen(lines[i]));
		printf("   %2d %3dpx %2dB [%s]\n", i + 1, w, (int)strlen(lines[i]), lines[i]);
		if (w > maxw) {
			/* 单个字符本身就比 maxw 宽时只能让它超 —— 再切也切不动了 */
			if (strlen(lines[i]) <= 4) {
				printf("        ⚠ 单字（%dB）本身就比 maxw 宽，只能让它超\n",
				       (int)strlen(lines[i]));
			} else {
				g_fail++;
				printf("        ✗ 这行超宽了\n");
			}
		}
	}
	via_wrap_free(lines, &cap);
}

/* ---- 真实 TTS SSE 响应体测试 ---- */
static unsigned char* slurp(const char* path, int* out_len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0) { fclose(f); return NULL; }
	unsigned char* b = (unsigned char*)malloc(n + 1);
	size_t got = fread(b, 1, n, f);
	fclose(f);
	b[got] = '\0';
	if (out_len) *out_len = (int)got;
	return b;
}

static void test_tts_sse(const char* path) {
	printf("\n=== TTS SSE 流式解析：%s ===\n", path);
	int len = 0;
	unsigned char* raw = slurp(path, &len);
	if (!raw) { printf("  (读不到，跳过)\n"); return; }
	printf("  响应体 %d 字节\n", len);

	unsigned char* pcm = (unsigned char*)malloc(4 * 1024 * 1024);
	if (!pcm) { free(raw); return; }
	int total = 0, chunks = 0, finished = 0, bad = 0;

	/* 按行切，只喂 data: 行 */
	char* save = NULL;
	char* line = strtok_r((char*)raw, "\n", &save);
	char json[256 * 1024];
	while (line) {
		int n = via_sse_data_line(line, json, sizeof(json));
		if (n > 0) {
			int got = via_tts_extract_chunk(json, pcm + total, 4 * 1024 * 1024 - total);
			if (got < 0) bad++;
			else if (got > 0) { total += got; chunks++; }
			if (via_tts_is_finish(json)) finished = 1;
		}
		line = strtok_r(NULL, "\n", &save);
	}

	printf("  data 块 %d 个，音频合计 %d 字节，finish=%d，坏块=%d\n",
	       chunks, total, finished, bad);
	CHECK(bad == 0, "没有解不出来的 data 块");
	CHECK(chunks > 0, "解出了音频块");
	CHECK(finished, "能看到 finish_reason=stop");

	/*
	 * 模拟设备上的调用协议：一边攒一边问「头到了没」。
	 * 注意别拿「第一个 data 块的大小」当探针 —— 那已经有 15KB，
	 * 头早齐了，会直接返回 44（我第一次就写错在这个假设上）。
	 * 真正会走到 -1 的是前 44 字节还没收全的时候。
	 */
	CHECK(via_wav_data_offset(pcm, 8, NULL, NULL) == -1,
	      "只收到 8 字节（连 RIFF/WAVE 都不全）返回 -1");
	CHECK(via_wav_data_offset(pcm, 36, NULL, NULL) == -1,
	      "收到 36 字节（data 块头没齐）返回 -1");
	CHECK(via_wav_data_offset(pcm, 44, NULL, NULL) == 44,
	      "收到 44 字节（正好到音频起点）返回 44");
	printf("  头齐判据：8B -> -1，36B -> -1，44B -> 44\n");

	int rate = 0, ch = 0;
	int off = via_wav_data_offset(pcm, total, &rate, &ch);
	printf("  收全 %d 字节后：data 偏移 %d，采样率 %d，声道 %d\n", total, off, rate, ch);
	CHECK(off == 44, "认出了 WAV 头偏移 44");
	CHECK(rate == 24000, "采样率是 24000（文档值）");
	CHECK(ch == 1, "单声道");
	if (off < 0) off = 0;

	/* 头里 data 长度字段是假的，这里确认我们没被它骗到 */
	if (total > 44) {
		int pcm_len = total - off;
		printf("  跳过表头后 PCM %d 字节 = %.2f 秒 @24kHz\n",
		       pcm_len, pcm_len / 2.0 / 24000.0);
		CHECK(pcm_len > 24000, "音频时长超过 0.5 秒（不是只解出第一块）");
		/* 峰值：确认不是全零 */
		short peak = 0;
		for (int i = off; i + 1 < total; i += 2) {
			short v = (short)(pcm[i] | (pcm[i + 1] << 8));
			if (v < 0) v = -v;
			if (v > peak) peak = v;
		}
		printf("  PCM 峰值 %d\n", peak);
		CHECK(peak > 1000, "音频不是静音");
	}

	free(pcm);
	free(raw);
}

int main(int argc, char** argv) {
	printf("=== 折行 / 中英混排 ===\n");

	/* 1. 纯中文：应该在任意字符间断，且不超宽 */
	dump_lines("纯中文", "今天天气怎么样？我想去公园散步，顺便买点水果回来。", 160);

	/* 2. 中英混排：中文可断，英文单词要整体保留 */
	dump_lines("中英混排",
	           "这个 API 的 endpoint 是 https://dashscope.aliyuncs.com 好不好用？",
	           160);

	/* 3. 长 URL：没有断点，必须硬切而不是死循环 */
	dump_lines("超长不可断词",
	           "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
	           120);

	/* 4. 强制换行 */
	dump_lines("强制换行", "第一行\n第二行比第一行长一些\n\n第四行", 200);

	/* 5. 边界：只有空格、空串、单字符 */
	dump_lines("空串", "", 100);
	dump_lines("单字符", "好", 100);
	dump_lines("多个空格", "a    b     c", 100);

	/* 6. 恰好一个字符都放不下 */
	dump_lines("宽度极小", "中文测试", 10);

	printf("\n=== 重采样 ===\n");
	{
		/*
		 * 这是本次修复的核心，必须验准。
		 * 前情：采集用设备实际速率、WAV 头却写配置里的 16000，
		 * 48k 的样本被标成 16k -> 音调/语速全错 -> 识别乱码，
		 * 而音量条正常（只算 RMS），极难发现。
		 */
		const int N = 48000;                 /* 1 秒 @48k */
		static short src[48000];
		for (int i = 0; i < N; i++) {
			/* 440Hz 正弦，幅度 3000 */
			src[i] = (short)(3000.0 * sin(2.0 * M_PI * 440.0 * i / 48000.0));
		}

		/* 48k -> 16k：应该是 16000 个样本，仍然是 440Hz */
		static short dst[20000];
		int got = via_resample_s16(src, N, 48000, dst, 20000, 16000);
		printf("  48k %d 帧 -> 16k %d 帧\n", N, got);
		CHECK(got == 16000, "48k->16k 得到 16000 帧（1 秒不变）");

		/* 数零点过零次数推频率：440Hz 在 1 秒里过零约 880 次 */
		int zc = 0;
		for (int i = 1; i < got; i++) {
			if ((dst[i - 1] < 0 && dst[i] >= 0) || (dst[i - 1] >= 0 && dst[i] < 0)) zc++;
		}
		printf("  过零 %d 次（440Hz 期望约 880）\n", zc);
		CHECK(zc >= 860 && zc <= 900, "频率没变（音调正确）");

		/* 幅度不该被改变 */
		int peak = 0;
		for (int i = 0; i < got; i++) { int v = dst[i] < 0 ? -dst[i] : dst[i]; if (v > peak) peak = v; }
		printf("  峰值 %d（期望约 3000）\n", peak);
		CHECK(peak >= 2900 && peak <= 3100, "幅度没变（音量正确）");

		/* 速率相同：应当原样拷贝 */
		static short same[48000];
		int n2 = via_resample_s16(src, 1000, 16000, same, 48000, 16000);
		CHECK(n2 == 1000 && memcmp(same, src, 1000 * 2) == 0, "同速率直接拷贝，不引入误差");

		/* 升采样 16k -> 48k：时长同样是 1 秒 */
		static short up[60000];
		int n3 = via_resample_s16(src, 16000, 16000, up, 60000, 48000);
		CHECK(n3 == 48000, "16k->48k 得到 48000 帧");

		/* 边界：0 帧、负长度、空指针都不该崩 */
		CHECK(via_resample_s16(src, 0, 48000, dst, 20000, 16000) == 0, "0 帧返回 0");
		CHECK(via_resample_s16(NULL, 100, 48000, dst, 20000, 16000) == 0, "空源指针返回 0");
		CHECK(via_resample_s16(src, 100, 0, dst, 20000, 16000) == 0, "0 速率返回 0");
	}

	printf("\n=== JSON 反转义 ===\n");
	{
		char out[256];
		const char* j = "{\"content\":\"\\u4f60\\u597d\\uff0c\\\"世界\\\"\\n第二行\"}";
		int n = via_json_string(j, "content", out, sizeof(out));
		printf("  解出 %d 字节：%s\n", n, out);
		CHECK(strcmp(out, "你好，\"世界\"\n第二行") == 0, "\\u 转义 + 引号 + 换行都对");

		const char* j2 = "{\"finish_reason\":null,\"x\":1}";
		char t[32];
		CHECK(via_json_string(j2, "finish_reason", t, sizeof(t)) < 0, "null 不当字符串解");

		const char* j3 = "{\"output\":{\"audio\":{\"data\":\"AAAA\"}},\"request_id\":\"r\"}";
		CHECK(via_json_int(j3, "nope", 7) == 7, "缺的整数字段返回默认值");
	}

	printf("\n=== base64 往返 ===\n");
	{
		unsigned char data[1024];
		for (int i = 0; i < (int)sizeof(data); i++) data[i] = (unsigned char)(i * 7 + i / 3);
		/* 三种长度都要对：被 3 整除、余 1、余 2 */
		int lens[] = {999, 1000, 1024};
		for (int k = 0; k < 3; k++) {
			char* enc = via_base64_encode(data, lens[k]);
			unsigned char dec[2048];
			int n = via_base64_decode(enc, (int)strlen(enc), dec, sizeof(dec));
			CHECK(n == lens[k] && memcmp(dec, data, lens[k]) == 0,
			      "长度 %d 往返一致", lens[k]);
			free(enc);
		}
	}

	printf("\n=== WAV 表头 ===\n");
	{
		unsigned char pcm[100];
		memset(pcm, 0x11, sizeof(pcm));
		unsigned char wav[256];
		int rc = via_wav_wrap(wav, sizeof(wav), pcm, sizeof(pcm), 16000, 1);
		CHECK(rc == 44, "返回表头长度 44");
		int rate = 0, ch = 0;
		int off = via_wav_data_offset(wav, 44 + 100, &rate, &ch);
		CHECK(off == 44 && rate == 16000 && ch == 1, "自己包的 WAV 能解回来");
		CHECK(via_wav_data_offset(wav, 20, NULL, NULL) == -1, "头没收全时返回 -1（继续攒）");
		{ const char* junk = "not a wav at all, 20B";
		  CHECK(via_wav_data_offset((const unsigned char*)junk, 20, NULL, NULL) == -2,
		        "不是 WAV 时返回 -2（当裸 PCM）"); }
		CHECK(!memcmp(wav, "RIFF", 4) && !memcmp(wav + 8, "WAVE", 4), "RIFF/WAVE 魔数对");

		/*
		 * 流式 TTS 的 WAV：长度字段是 0x7FFFFFFF（1.5MB 的响应体里就这么写的，
		 * 真实抓包见 tools/tts-sample.sse）。因为我们只看偏移不看长度，
		 * 只要 fmt 齐全就应该稳定返回 44。
		 */
		unsigned char fake[64];
		memset(fake, 0, sizeof(fake));
		memcpy(fake, "RIFF", 4);
		fake[4] = 0xFF; fake[5] = 0xFF; fake[6] = 0xFF; fake[7] = 0x7F;
		memcpy(fake + 8, "WAVE", 4);
		memcpy(fake + 12, "fmt ", 4);
		fake[16] = 16;
		fake[20] = 1;                                  /* PCM */
		fake[22] = 1;                                  /* mono */
		/* rate = 24000 */
		fake[24] = 0xC0; fake[25] = 0x5D;
		memcpy(fake + 36, "data", 4);
		fake[40] = 0xFF; fake[41] = 0xFF; fake[42] = 0xFF; fake[43] = 0x7F;  /* 假长度 */
		int fr = 0, fc = 0;
		int off2 = via_wav_data_offset(fake, sizeof(fake), &fr, &fc);
		CHECK(off2 == 44, "长度字段是假的（0x7FFFFFFF）也能靠 fmt 定出偏移 44");
		CHECK(fr == 24000 && fc == 1, "假长度 WAV 也读到了 24000/mono");
		/* 只收到前 20 字节时：还不知道是不是假长度，得继续等 */
		CHECK(via_wav_data_offset(fake, 20, NULL, NULL) == -1,
		      "只收到 20 字节时返回 -1，不会误报偏移");

		/* 没有 fmt 时无法确定音频格式，宁可不播也不给半个表头（故意返回 0） */
		unsigned char nofmt[64];
		memset(nofmt, 0, sizeof(nofmt));
		memcpy(nofmt, "RIFF", 4);
		memcpy(nofmt + 8, "WAVE", 4);
		memcpy(nofmt + 12, "data", 4);
		nofmt[16] = 0xFF; nofmt[17] = 0xFF; nofmt[18] = 0xFF; nofmt[19] = 0x7F;
		CHECK(via_wav_data_offset(nofmt, sizeof(nofmt), NULL, NULL) == -1,
		      "假长度 + 没有 fmt 时返回 -1（宁可等也不猜）");
	}

	if (argc > 1) test_tts_sse(argv[1]);

	printf("\n%s（%d 个失败）\n", g_fail ? "❌ 有失败" : "✅ 全部通过", g_fail);
	return g_fail ? 1 : 0;
}

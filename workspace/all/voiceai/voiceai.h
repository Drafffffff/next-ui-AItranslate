#ifndef __VOICEAI_H__
#define __VOICEAI_H__

/*
 * Voice AI —— NextUI 原生工具：**语音输入，文字输出**。
 *
 * 按 B 说话 -> 录音（静音自动结束）-> 语音识别 -> 大模型对话 -> 回答显示在屏幕上。
 * 不做 TTS 播报，所以没有音频播放、没有重采样：回答直接用文字看。
 *
 * 拆成几个翻译单元，是为了把「不依赖 SDL / 设备」的部分拿到宿主机上单测：
 *
 *   via_main.c      main、配置读写、界面渲染、按键
 *   via_audio.c     ALSA 麦克风采集（含 VAD）
 *   via_pipeline.c  录音->识别->对话 的 worker 状态机
 *   via_api.c       curl 发请求、OpenAI 兼容 JSON、流式 SSE 解析、历史落盘
 *   via_ui.c        纯逻辑：UTF-8 折行、JSON、base64、WAV（可宿主机测试）
 */

#include <stddef.h>

/* ------------------------------------------------------------------ 小工具 */

int   via_utf8_truncate(const char* s, int len);   /* 不切半个 UTF-8 字符 */
unsigned via_utf8_next(const char** p);            /* 取一个码点并前进 */

/* ------------------------------------------------------------------ 折行 */

/*
 * 文本度量回调：返回一段 UTF-8 子串的像素宽度。
 * 设备上由 TTF_SizeUTF8 提供；宿主机测试里传假函数。
 * 返回 -1 表示量不了，折行会退化成按字节数硬切。
 */
typedef int (*ViaMeasureFn)(void* ctx, const char* s, int len);

/*
 * 把 text 折成多行。返回行数。
 * 中英混排的关键规则：
 *   - CJK 字符之间可以直接断行
 *   - 拉丁单词整体不断开（断点只落在空格），单词本身超宽才硬切
 *   - 行尾空白丢掉；'\n' 强制换行
 * 内存：文本副本 + 行指针数组，用 via_wrap_free 还。
 */
int  via_wrap_text(const char* text, int max_width, ViaMeasureFn measure, void* ctx,
                   char*** out_lines, int* out_cap, int lines_cap);
void via_wrap_free(char** lines, int* cap);

/* ------------------------------------------------------------------ SSE */

/* 从一行 SSE 里取出 data: 后面的正文。返回长度，0 = 这行不是 data 行 */
int via_sse_data_line(const char* line, char* out, int out_cap);

/* ------------------------------------------------------------------ JSON */

int  via_json_string(const char* obj, const char* key, char* out, int cap);  /* -1 = 没这个键 */
int  via_json_int(const char* obj, const char* key, int def);
void via_json_escape(const char* in, char* out, int cap);
const char* via_json_find(const char* obj, const char* key);

/* ------------------------------------------------------------------ base64 */

char* via_base64_encode(const unsigned char* in, size_t n);
int   via_base64_decode(const char* in, int in_len, unsigned char* out, int out_cap);

/* ------------------------------------------------------------------ WAV */

/* 把裸 PCM(16bit) 包成 WAV 头写进 buf，返回表头长度（44），-1 = 放不下 */
int via_wav_wrap(unsigned char* buf, int cap, const unsigned char* pcm, int pcm_len,
                 int sample_rate, int channels);

/*
 * 从 WAV 头里定出 data 段偏移。三态：
 *   >0  找到了，就是这个偏移
 *   -1  头还没收全（流式接收时继续攒）
 *   -2  不是 WAV
 * 设备上主要用来构造上传给识别接口的 WAV（via_wav_wrap + 这个偏移）。
 */
int via_wav_data_offset(const unsigned char* buf, int len, int* out_rate, int* out_channels);

/*
 * 下面是 TTS 流式响应（SSE + base64 音频分片）的解析。
 *
 * ⚠ 当前版本**没用**它 —— 产品形态是语音输入、文字输出，不合成语音。
 * 保留的原因：一是这套「SSE 分片 + base64 累积」的解析是通用件，
 * 二是 tools/tts-sample.sse 里存了真实抓包，tools/test_voiceai.c 靠它
 * 回归验证分片拼接不丢字节（这个 bug 很容易犯，而一旦犯了音频就断了）。
 * 以后要加播报功能，直接把这两个函数接上就行。
 */
int via_tts_extract_chunk(const char* json, unsigned char* out, int out_cap);
int via_tts_is_finish(const char* json);

/* ------------------------------------------------------------------ 配置 */

#define VIA_MAX_URL   256
#define VIA_MAX_KEY   192
#define VIA_MAX_MODEL 64
#define VIA_MAX_LANG  32

typedef struct {
	/* 语音识别 */
	char asr_endpoint[VIA_MAX_URL];
	char asr_model[VIA_MAX_MODEL];
	char asr_key[VIA_MAX_KEY];
	/* 对话 */
	char chat_endpoint[VIA_MAX_URL];
	char chat_model[VIA_MAX_MODEL];
	char chat_key[VIA_MAX_KEY];
	char system_prompt[1024];
	/* 录音 */
	char mic_device[64];
	int  sample_rate;      /* 采集目标速率，默认 16000 */
	int  silence_ms;       /* 说完静音多久自动结束 */
	int  min_ms;           /* 短于这个时长当误触 */
	int  max_secs;         /* 最长录多久 */
	int  vad_threshold;    /* 语音判定阈值 0~32767 */
	/* 其它 */
	int  chat_max_tokens;
	int  timeout_secs;
	int  history_turns;    /* 带几轮上下文 */
	int  debug;
} ViaConfig;

/*
 * 读配置。
 * 自有的键在 .userdata/shared/voice-ai.txt；没写的话会退回读
 * .userdata/shared/ai-translate.txt 里已有的 key（两个工具共用一把 key 省事）。
 */
int via_config_load(ViaConfig* cfg);
int via_config_save(const ViaConfig* cfg);

#endif

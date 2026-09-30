#ifndef __VIA_PIPELINE_H__
#define __VIA_PIPELINE_H__

/*
 * 录音 -> 识别 -> 对话 的状态机。
 *
 * 产品形态：**语音输入，文字输出**。不做 TTS 播报 —— 模型的话直接显示在屏幕上，
 * 这样少一次 API 往返，快很多，也不用处理音频播放和重采样。
 *
 * 线程分工（很重要）：
 *   worker 线程：ALSA 采集 + curl 网络调用。完全不碰 SDL 绘图。
 *   主线程    ：画界面、处理按键、把 worker 写好的文字刷出来。
 * 共享的字符串都走 p->lock，纯计数/状态走 SDL 原子量。
 */

#include <pthread.h>
#include <SDL2/SDL.h>   /* 只用到 SDL_Thread / SDL_atomic_t（SDL2 里叫这个，不是 SDL3 的 SDL_AtomicInt）/ SDL_mutex */

#include "voiceai.h"
#include "via_audio.h"

#define VIA_MAX_TURNS   40
#define VIA_TURN_CHARS  2048

typedef struct {
	char user[VIA_TURN_CHARS];
	char ai[VIA_TURN_CHARS];
} ViaTurn;

typedef struct {
	ViaTurn turn[VIA_MAX_TURNS];
	int     count;             /* 已用完的槽位（满了就整体前移挤掉最老的） */
} ViaHistory;

const ViaTurn* via_history_at(const ViaHistory* h, int i);   /* 0 = 最老的一条 */
int  via_history_count(const ViaHistory* h);
void via_history_push(ViaHistory* h, const char* user, const char* ai);
void via_history_clear(ViaHistory* h);
void via_history_save(const ViaHistory* h);
void via_history_load(ViaHistory* h);

enum {
	VIA_ST_IDLE = 0,      /* 待机，等用户按 B */
	VIA_ST_RECORDING,
	VIA_ST_TRANSCRIBING,
	VIA_ST_THINKING,
	VIA_ST_ERROR,
	VIA_ST_DONE,
};

typedef struct ViaPipeline {
	ViaConfig cfg;              /* 启动时填好，之后只读 */

	/* --- 主线程写 / worker 读 --- */
	SDL_atomic_t stop;         /* 要求 worker 中止（停止录音 / 放弃本轮） */

	/* --- worker 写 / 主线程读 --- */
	SDL_atomic_t state;
	SDL_atomic_t level;        /* 实时音量 0~100，画电平条 */
	SDL_atomic_t rec_ms;       /* 已录时长（毫秒） */
	SDL_atomic_t thread_done;  /* 1 = 没有 worker 在跑 */
	SDL_atomic_t stt_ms, chat_ms, total_ms;
	SDL_atomic_t chat_streaming;   /* 1 = 回答正在一段段回来 */

	/* 需要互斥保护的字符串 */
	pthread_mutex_t lock;
	char status[128];
	char err[512];
	char user_text[VIA_TURN_CHARS];
	char ai_text[VIA_TURN_CHARS];   /* 边收边追加，主线程滚动显示 */
	char mic_name[64];              /* 实际用的采集设备，显示在状态栏 */

	/*
	 * 麦克风硬件的**实际**采集速率。WAV 头必须用这个值，不能用配置里的
	 * 目标值 —— 设备经常只支持 48k，配置写 16000 时真正收到的仍是 48k 数据。
	 * 用错的话音频被按错速度解释（音调变尖/变慢），识别结果全是乱码，
	 * 但音量条依旧正常（它只算 RMS），非常难查。
	 */
	SDL_atomic_t mic_rate;

	ViaHistory hist;
	SDL_Thread* thread;
} ViaPipeline;

/* 初始化。会读回上次的对话历史（.userdata/<platform>/VoiceAI/history.txt） */
int  via_pipeline_init(ViaPipeline* p, const ViaConfig* cfg);
void via_pipeline_quit(ViaPipeline* p);

int  via_pipeline_thread(void* arg);          /* SDL 线程入口 */
int  via_pipeline_start(ViaPipeline* p);      /* 开始一轮：返回 -1 表示已有在跑 */
void via_pipeline_stop(ViaPipeline* p);

int  via_pipeline_state(ViaPipeline* p);
void via_pipeline_status(ViaPipeline* p, char* out, int cap);
void via_pipeline_error(ViaPipeline* p, char* out, int cap);
void via_pipeline_user_text(ViaPipeline* p, char* out, int cap);
void via_pipeline_ai_text(ViaPipeline* p, char* out, int cap);
void via_pipeline_mic_name(ViaPipeline* p, char* out, int cap);
int  via_pipeline_level(ViaPipeline* p);      /* 实时音量 0~100 */
int  via_pipeline_mic_rate(ViaPipeline* p);   /* 麦克风实际采集速率 */
int  via_pipeline_rec_ms(ViaPipeline* p);     /* 已录时长（毫秒） */

void via_pipeline_set_status(ViaPipeline* p, const char* fmt, ...);
void via_pipeline_append_ai(ViaPipeline* p, const char* text);   /* 流式追加回答 */

/*
 * 把这次录到的音频存到 .userdata/ai/samples/ 里，只留最近几个。
 *
 * 存在的意义：语音识别出乱码时，光看界面上的音量条完全查不出来 ——
 * 音量条只算 RMS，采样率标错、字节序错、声道错它都照样正常跳。
 * 必须把真正发出去的音频拷到电脑上听一遍才能定位。
 * 每次存两个文件：
 *   <时间戳>_raw.wav   麦克风原始采集，按硬件真实速率标注 —— 先听这个
 *   <时间戳>_sent.wav  重采样后、真正 base64 发出去的那段 —— 再听这个
 * 两个听着都正常但识别还是乱 → 问题不在音频，在网络或接口那一层。
 *
 * sent 可以和 raw 是同一块内存（不需要重采样时就是同一块）。
 */
void via_keep_sample(const short* raw, int raw_frames, int raw_rate,
                     const short* sent, int sent_frames, int sent_rate);

#endif

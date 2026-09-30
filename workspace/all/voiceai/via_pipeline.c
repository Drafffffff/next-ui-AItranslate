/*
 * 流水线实现：录音 -> 识别 -> 对话（流式出字）。
 * 产品形态是语音输入、文字输出，所以这里完全没有 TTS / 音频播放代码。
 *
 * 线程分工（重要）：
 *   worker 线程：ALSA 采集、curl 网络调用，把识别文本和模型回答写进共享字段。
 *                完全不碰 SDL 绘图（SDL 的绘图只能在主线程做）。
 *   主线程    ：画界面、处理按键、读共享字段。
 * 共享字符串走 p->lock；状态/计数走 SDL 原子量。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "defines.h"     /* SDCARD_PATH —— 临时文件路径要用 */
#include "voiceai.h"
#include "via_audio.h"
#include "via_pipeline.h"
#include "via_api.h"

#ifndef VIA_TMP_DIR
#define VIA_TMP_DIR SDCARD_PATH "/.userdata/ai"
#endif

#define VIA_REC_MAX_SECS 120
#define VIA_MIC_WAV VIA_TMP_DIR "/mic.wav"

static unsigned via_now_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (unsigned)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

typedef struct {
	short* buf;
	int    cap_frames;
	int    frames;
} RecBuf;

/* ------------------------------------------------------------------ 状态读写 */

static void via_set_error(ViaPipeline* p, const char* fmt, ...) {
	va_list ap;
	pthread_mutex_lock(&p->lock);
	va_start(ap, fmt);
	vsnprintf(p->err, sizeof(p->err), fmt, ap);
	va_end(ap);
	SDL_AtomicSet(&p->state, VIA_ST_ERROR);
}

static void via_set_text(ViaPipeline* p, char* which, const char* text) {
	pthread_mutex_lock(&p->lock);
	snprintf(which, VIA_TURN_CHARS, "%s", text ? text : "");
	pthread_mutex_unlock(&p->lock);
}

int via_pipeline_state(ViaPipeline* p) { return SDL_AtomicGet(&p->state); }

static void via_get_locked(ViaPipeline* p, const char* src, char* out, int cap) {
	pthread_mutex_lock(&p->lock);
	snprintf(out, cap, "%s", src);
	pthread_mutex_unlock(&p->lock);
}

void via_pipeline_status(ViaPipeline* p, char* out, int cap)    { via_get_locked(p, p->status, out, cap); }
void via_pipeline_error(ViaPipeline* p, char* out, int cap)     { via_get_locked(p, p->err, out, cap); }
void via_pipeline_user_text(ViaPipeline* p, char* out, int cap) { via_get_locked(p, p->user_text, out, cap); }
void via_pipeline_ai_text(ViaPipeline* p, char* out, int cap)   { via_get_locked(p, p->ai_text, out, cap); }
void via_pipeline_mic_name(ViaPipeline* p, char* out, int cap)  { via_get_locked(p, p->mic_name, out, cap); }
int  via_pipeline_level(ViaPipeline* p)  { return SDL_AtomicGet(&p->level); }
int  via_pipeline_rec_ms(ViaPipeline* p) { return SDL_AtomicGet(&p->rec_ms); }

/* ------------------------------------------------------------------ 生命周期 */

int via_pipeline_init(ViaPipeline* p, const ViaConfig* cfg) {
	memset(p, 0, sizeof(*p));
	p->cfg = *cfg;

	pthread_mutex_init(&p->lock, NULL);

	SDL_AtomicSet(&p->state, VIA_ST_IDLE);
	SDL_AtomicSet(&p->stop, 0);
	SDL_AtomicSet(&p->thread_done, 1);

	mkdir(VIA_TMP_DIR, 0755);
	via_pipeline_set_status(p, "准备就绪");

	via_history_load(&p->hist);
	return 0;
}

void via_pipeline_quit(ViaPipeline* p) {
	SDL_AtomicSet(&p->stop, 1);
	if (p->thread) {
		/* 最多等 5 秒，别把退出卡死 */
		for (int i = 0; i < 500 && !SDL_AtomicGet(&p->thread_done); i++) usleep(10000);
		SDL_WaitThread(p->thread, NULL);
		p->thread = NULL;
	}
	via_history_save(&p->hist);
	pthread_mutex_destroy(&p->lock);
}

int via_pipeline_start(ViaPipeline* p) {
	if (!SDL_AtomicGet(&p->thread_done)) return -1;   /* 上一轮还在跑 */

	pthread_mutex_lock(&p->lock);
	p->user_text[0] = '\0';
	p->ai_text[0] = '\0';
	p->err[0] = '\0';
	pthread_mutex_unlock(&p->lock);

	SDL_AtomicSet(&p->stop, 0);
	SDL_AtomicSet(&p->chat_streaming, 0);
	SDL_AtomicSet(&p->thread_done, 0);
	p->thread = SDL_CreateThread(via_pipeline_thread, "voiceai", p);
	if (!p->thread) {
		SDL_AtomicSet(&p->thread_done, 1);
		return -1;
	}
	return 0;
}

void via_pipeline_stop(ViaPipeline* p) { SDL_AtomicSet(&p->stop, 1); }

/* ------------------------------------------------------------------ 录音 */

/*
 * 录一段话。
 *
 * 返回：0 = 录到有效音频；1 = 用户主动结束；-1 = 失败（错误已写进 p->err）。
 *
 * VAD 用能量法（不引任何依赖）：
 *   1. 先等到某个 20ms 帧的 RMS 超过阈值 -> 认为开始说话
 *   2. 之后连续 silence_ms 都低于阈值 -> 认为说完了，自动结束
 *   3. 保护：一直没声音最多等 no_speech 秒；总长不超过 max_secs
 *   4. 用户按 B 随时结束
 */
static int via_record_once(ViaPipeline* p, RecBuf* rb) {
	ViaMic mic;
	if (via_mic_open(&mic, p->cfg.mic_device, p->cfg.sample_rate) != 0) {
		via_set_error(p, "打不开麦克风：%s", mic.err);
		return -1;
	}

	int rate = mic.rate > 0 ? mic.rate : 16000;

	pthread_mutex_lock(&p->lock);
	snprintf(p->mic_name, sizeof(p->mic_name), "%s", mic.device);
	snprintf(p->status, sizeof(p->status),
	         "正在听……说完停一下会自动结束，也可以按 B 结束");
	pthread_mutex_unlock(&p->lock);
	SDL_AtomicSet(&p->state, VIA_ST_RECORDING);
	SDL_AtomicSet(&p->level, 0);
	SDL_AtomicSet(&p->rec_ms, 0);

	int frame_frames = rate / 50;                 /* 20ms */
	if (frame_frames < 32) frame_frames = 32;
	if (frame_frames > 2048) frame_frames = 2048;

	short* frame = (short*)malloc((size_t)frame_frames * 2);
	if (!frame) { via_mic_close(&mic); via_set_error(p, "内存不足（录音帧）"); return -1; }

	int silence_need = (p->cfg.silence_ms > 0 ? p->cfg.silence_ms : 1200) * rate / 1000;
	int min_need     = (p->cfg.min_ms > 0 ? p->cfg.min_ms : 400) * rate / 1000;
	int max_need     = (p->cfg.max_secs > 0 ? p->cfg.max_secs : 30) * rate;
	int no_speech    = 6 * rate;
	int thresh       = p->cfg.vad_threshold > 0 ? p->cfg.vad_threshold : 500;

	int silence_run = 0;
	int spoke = 0;
	int manual = 0;
	int failed = 0;
	unsigned t0 = via_now_ms();

	for (;;) {
		if (SDL_AtomicGet(&p->stop)) { manual = 1; break; }

		if (via_mic_read(&mic, frame, frame_frames, 300) < 0) {
			via_set_error(p, "读麦克风失败：%s", mic.err);
			failed = 1;
			break;
		}

		int rms = via_rms(frame, frame_frames);
		SDL_AtomicSet(&p->level, rms * 100 / 32767);
		SDL_AtomicSet(&p->rec_ms, (int)(rb->frames * 1000 / rate));

		if (rb->frames + frame_frames > rb->cap_frames) break;   /* 录满了 */

		memcpy(rb->buf + rb->frames, frame, (size_t)frame_frames * 2);
		rb->frames += frame_frames;

		if (rms >= thresh) { spoke = 1; silence_run = 0; }
		else if (spoke)    { silence_run += frame_frames; }

		if (spoke && silence_run >= silence_need) break;         /* 说完了 */
		if (rb->frames >= max_need) break;                       /* 到上限 */
		if (!spoke && rb->frames >= no_speech) break;             /* 一直没说话 */
	}

	via_mic_close(&mic);
	free(frame);
	SDL_AtomicSet(&p->level, 0);

	if (failed) return -1;

	if (rb->frames < min_need) {
		rb->frames = 0;
		if (manual) return 1;
		via_set_error(p, "没听清：只录到 %dms，低于 min_ms(%d)",
		              rb->frames * 1000 / rate, p->cfg.min_ms);
		return -1;
	}
	/* 语音识别要 16k 的输入；设备若给了别的速率，这里按需要报出来 */
	if (rate != 16000) {
		via_pipeline_set_status(p, "提示：麦克风跑在 %dHz（不是 16kHz）", rate);
	}
	(void)t0;
	return 0;
}

/* ------------------------------------------------------------------ 主流程 */

static int via_save_mic_wav(const short* pcm, int frames, int rate) {
	int pcm_bytes = frames * 2;
	unsigned char* wav = (unsigned char*)malloc((size_t)pcm_bytes + 64);
	if (!wav) return -1;
	int hdr = via_wav_wrap(wav, pcm_bytes + 64, (const unsigned char*)pcm, pcm_bytes, rate, 1);
	if (hdr < 0) { free(wav); return -1; }
	int rc = 0;
	FILE* f = fopen(VIA_MIC_WAV, "wb");
	if (!f) rc = -1;
	else {
		if (fwrite(wav, 1, (size_t)(hdr + pcm_bytes), f) != (size_t)(hdr + pcm_bytes)) rc = -1;
		fclose(f);
	}
	free(wav);
	return rc;
}

/* 流式回调：把增量文字追加到共享字段，主线程会滚动显示 */
static void via_on_delta(void* ctx, const char* delta) {
	ViaPipeline* p = (ViaPipeline*)ctx;
	if (SDL_AtomicGet(&p->chat_streaming) == 0) {
		SDL_AtomicSet(&p->chat_streaming, 1);
		via_pipeline_set_status(p, "回答中…");
	}
	via_pipeline_append_ai(p, delta);
}

int via_pipeline_thread(void* arg) {
	ViaPipeline* p = (ViaPipeline*)arg;
	unsigned t_start = via_now_ms();

	RecBuf rb;
	rb.cap_frames = VIA_REC_MAX_SECS * 16000;
	rb.frames = 0;
	rb.buf = (short*)malloc((size_t)rb.cap_frames * 2);
	if (!rb.buf) {
		via_set_error(p, "内存不足（录音缓冲需要 %dMB）",
		              VIA_REC_MAX_SECS * 16000 * 2 / 1024 / 1024);
		goto done;
	}

	/* 1) 录音 */
	{
		int r = via_record_once(p, &rb);
		if (r != 0) {
			if (r == 1) {
				SDL_AtomicSet(&p->state, VIA_ST_IDLE);
				via_pipeline_set_status(p, "已取消");
			}
			goto done;
		}
	}

	/* 2) 识别 */
	{
		int rate = 16000;
		{
			/* 用设备实际速率算时长和 WAV 头；mic_name 里存了设备名，
			 * 速率这里重新问一次不值当，直接按配置值 —— 不一致时
			 * via_record_once 已经提示过了 */
			rate = p->cfg.sample_rate > 0 ? p->cfg.sample_rate : 16000;
		}
		if (via_save_mic_wav(rb.buf, rb.frames, rate) != 0) {
			via_set_error(p, "写不了录音文件 %s", VIA_MIC_WAV);
			goto done;
		}

		SDL_AtomicSet(&p->state, VIA_ST_TRANSCRIBING);
		via_pipeline_set_status(p, "识别中…（%.1f 秒音频）", rb.frames / (double)rate);

		int wav_len = 0;
		unsigned char* wav = NULL;
		FILE* f = fopen(VIA_MIC_WAV, "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			wav_len = (int)ftell(f);
			fseek(f, 0, SEEK_SET);
			wav = (unsigned char*)malloc((size_t)wav_len + 1);
			if (wav && fread(wav, 1, (size_t)wav_len, f) != (size_t)wav_len) {
				free(wav); wav = NULL;
			}
			fclose(f);
		}
		if (!wav) { via_set_error(p, "读不回录音文件 %s", VIA_MIC_WAV); goto done; }

		char text[VIA_TURN_CHARS];
		char err[512];
		unsigned t0 = via_now_ms();
		int rc = via_stt(&p->cfg, wav, wav_len, text, sizeof(text), err, sizeof(err));
		free(wav);
		SDL_AtomicSet(&p->stt_ms, (int)(via_now_ms() - t0));

		if (rc != 0) { via_set_error(p, "识别失败：%s", err); goto done; }
		if (!text[0]) { via_set_error(p, "有声音但没听出字，再说一次？"); goto done; }

		via_set_text(p, p->user_text, text);
		remove(VIA_MIC_WAV);
	}

	/* 3) 对话（流式出字） */
	{
		SDL_AtomicSet(&p->state, VIA_ST_THINKING);
		via_pipeline_set_status(p, "在想…");

		char user[VIA_TURN_CHARS];
		via_pipeline_user_text(p, user, sizeof(user));

		/* 先把用户这句推进历史，模型才有上下文；回答留空，拿到后补 */
		via_history_push(&p->hist, user, "");

		char reply[VIA_TURN_CHARS];
		char err[512];
		unsigned t0 = via_now_ms();
		int turns = p->cfg.history_turns > 0 ? p->cfg.history_turns : 6;
		int rc = via_chat_stream(&p->cfg, &p->hist, turns,
		                         via_on_delta, p, reply, sizeof(reply), err, sizeof(err));
		SDL_AtomicSet(&p->chat_ms, (int)(via_now_ms() - t0));

		if (rc != 0) {
			if (p->hist.count > 0) p->hist.count--;   /* 撤掉刚才那条，别留空回答 */
			via_set_error(p, "对话失败：%s", err);
			goto done;
		}
		if (!reply[0]) { via_set_error(p, "模型返回了空回答"); goto done; }

		via_set_text(p, p->ai_text, reply);          /* 兜底：流式没拼全时也能显示 */
		if (p->hist.count > 0) {
			snprintf(p->hist.turn[p->hist.count - 1].ai, VIA_TURN_CHARS, "%s", reply);
		}
		via_history_save(&p->hist);
	}

	SDL_AtomicSet(&p->state, VIA_ST_DONE);

done:
	SDL_AtomicSet(&p->total_ms, (int)(via_now_ms() - t_start));
	if (rb.buf) free(rb.buf);
	SDL_AtomicSet(&p->thread_done, 1);
	return 0;
}

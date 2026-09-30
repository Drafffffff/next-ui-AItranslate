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
#include <time.h>
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
int  via_pipeline_mic_rate(ViaPipeline* p) { return SDL_AtomicGet(&p->mic_rate); }

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
		int ms = rb->frames * 1000 / rate;
		rb->frames = 0;
		if (manual) return 1;
		via_set_error(p, "没听清：只录到 %dms，低于 min_ms(%d)", ms, p->cfg.min_ms);
		return -1;
	}

	/*
	 * 把硬件实际速率告诉上层 —— WAV 头必须按这个值写。
	 * 这里是本次修复的核心：以前上层固定用 cfg.sample_rate 写 WAV 头，
	 * 而设备实际可能跑在 48k，于是 48k 的样本被标成 16k，
	 * 送进识别接口后音调/语速全错，出来就是乱码。
	 */
	SDL_AtomicSet(&p->mic_rate, rate);
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

/* ------------------------------------------------------------------ 留样本 */

/*
 * 写一个 16bit 单声道 PCM 到 WAV 文件。
 * 注意用 via_wav_wrap 写**正确**的采样率 —— 这里正是最容易出错的地方，
 * 之前就是因为 WAV 头写死 16000 而数据其实是 48k，导致识别全是乱码。
 */
static int via_write_wav_file(const char* path, const short* pcm, int frames, int rate) {
	if (!path || frames <= 0 || rate <= 0) return -1;
	int pcm_bytes = frames * 2;
	unsigned char* wav = (unsigned char*)malloc((size_t)pcm_bytes + 64);
	if (!wav) return -1;
	int hdr = via_wav_wrap(wav, pcm_bytes + 64, (const unsigned char*)pcm, pcm_bytes, rate, 1);
	if (hdr < 0) { free(wav); return -1; }

	int rc = 0;
	FILE* f = fopen(path, "wb");
	if (!f) rc = -1;
	else {
		if (fwrite(wav, 1, (size_t)(hdr + pcm_bytes), f) != (size_t)(hdr + pcm_bytes)) rc = -1;
		fclose(f);
	}
	free(wav);
	return rc;
}

/* 目录里只留最近 N 个样本，其余删掉（每个样本 2 个文件） */
static void via_prune_samples(const char* dir, int keep_files) {
	/* 用 ls 的排序结果，按名字（前缀是时间戳）从旧到新删 */
	char cmd[600];
	snprintf(cmd, sizeof(cmd),
	         "ls -1t '%s'/*.wav 2>/dev/null | tail -n +%d | while read f; do rm -f \"$f\"; done",
	         dir, keep_files + 1);
	if (system(cmd) != 0) { /* 清理失败不影响主流程 */ }
}

void via_keep_sample(const short* raw, int raw_frames, int raw_rate,
                     const short* sent, int sent_frames, int sent_rate) {
	if (raw_frames <= 0) return;

	char dir[512];
	snprintf(dir, sizeof(dir), "%s/samples", VIA_TMP_DIR);
	mkdir(dir, 0755);

	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm tm;
	localtime_r(&tv.tv_sec, &tm);

	char raw_path[600], sent_path[600];
	snprintf(raw_path, sizeof(raw_path), "%s/%04d%02d%02d-%02d%02d%02d_raw.wav",
	         dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	         tm.tm_hour, tm.tm_min, tm.tm_sec);
	snprintf(sent_path, sizeof(sent_path), "%s/%04d%02d%02d-%02d%02d%02d_sent.wav",
	         dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	         tm.tm_hour, tm.tm_min, tm.tm_sec);

	/* 原始采集：按硬件真实速率写，这样才能听出「到底录成了什么」 */
	via_write_wav_file(raw_path, raw, raw_frames, raw_rate);

	/* 真正发出去的那段 */
	if (sent && sent_frames > 0) {
		via_write_wav_file(sent_path, sent, sent_frames, sent_rate);
	}

	/* 留最近 5 次（=10 个文件），别把卡写满 */
	via_prune_samples(dir, 10);
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
		/*
		 * 先把音频统一到 16kHz 单声道 —— 这是 qwen3-asr-flash 要的输入格式，
		 * 也是我们能控制的唯一格式。
		 *
		 * 设备实际速率用 mic_rate（录音阶段写进去的），不是配置值。
		 */
		int src_rate = SDL_AtomicGet(&p->mic_rate);
		if (src_rate <= 0) src_rate = p->cfg.sample_rate > 0 ? p->cfg.sample_rate : 16000;
		const int ASR_RATE = 16000;

		short* pcm = rb.buf;
		int    frames = rb.frames;
		short* resampled = NULL;

		if (src_rate != ASR_RATE) {
			/*
			 * 输出长度按比例算，再留 64 个样本余量避免边界溢出。
			 * 最高支持到 48k -> 16k 这种常见的 3:1 降采样。
			 */
			int cap = (int)((long long)frames * ASR_RATE / src_rate) + 64;
			if (cap < 1) cap = 1;
			resampled = (short*)calloc((size_t)cap, sizeof(short));   /* calloc：函数不擦尾部 */
			if (!resampled) {
				via_set_error(p, "内存不足（重采样 %d -> %d）", src_rate, ASR_RATE);
				goto done;
			}
			int got = via_resample_s16(rb.buf, frames, src_rate, resampled, cap, ASR_RATE);
			if (got <= 0) {
				free(resampled);
				via_set_error(p, "重采样失败（%d -> %d）", src_rate, ASR_RATE);
				goto done;
			}
			via_pipeline_set_status(p, "重采样 %dHz -> %dHz…", src_rate, ASR_RATE);
			pcm = resampled;
			frames = got;
		}

		/*
		 * 留样本：把「实际发给识别接口的东西」存下来。
		 * 语音识别出乱码时，光看音量条是查不出来的 ——
		 * 必须把音频拷出来听。这个目录就是干这个的。
		 */
		if (p->cfg.keep_samples) {
			via_keep_sample(rb.buf, rb.frames, src_rate, pcm, frames, ASR_RATE);
		}

		if (via_save_mic_wav(pcm, frames, ASR_RATE) != 0) {
			if (resampled) free(resampled);
			via_set_error(p, "写不了录音文件 %s", VIA_MIC_WAV);
			goto done;
		}
		if (resampled) { free(resampled); resampled = NULL; }

		SDL_AtomicSet(&p->state, VIA_ST_TRANSCRIBING);
		via_pipeline_set_status(p, "识别中…（%.1f 秒，%dHz）", frames / (double)ASR_RATE, ASR_RATE);

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

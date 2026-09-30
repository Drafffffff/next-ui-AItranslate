/*
 * ALSA 采集与播放。
 *
 * 设备情况（TrimUI Brick / tg5040，实测 + 参考 nextui-bmo-pak 的结论一致）：
 *   - 内置单声道麦克风挂在 hw:0,0 上，和扬声器共用同一个声卡
 *   - 采集用 16kHz / S16_LE / 单声道（也是 qwen3-asr-flash 偏好的输入格式）
 *   - TTS 返回的是 24kHz / S16_LE / 单声道，需要重采样到 48kHz 再放
 *
 * 打开顺序 hw:0,0 -> plughw:0,0 -> default：
 *   hw: 是直通，最省 CPU 也最可控，能开就用它；
 *   plughw/default 会走 ALSA 的插件层自动转格式，作为兜底。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <alsa/asoundlib.h>

#include "via_audio.h"

#define VIA_CAP_FRAMES 320    /* 一次读 20ms @16kHz */
#define VIA_PB_FRAMES  960    /* 一次写 20ms @48kHz */

/* ------------------------------------------------------------------ 采集 */

static const char* VIA_MIC_FALLBACKS[] = { "hw:0,0", "plughw:0,0", "default", NULL };

int via_mic_open(ViaMic* m, const char* device, int rate) {
	memset(m, 0, sizeof(*m));
	m->fd = -1;
	m->rate = rate;

	const char* try_list[4];
	int n = 0;
	if (device && device[0]) try_list[n++] = device;
	for (int i = 0; VIA_MIC_FALLBACKS[i] && n < 4; i++) {
		if (device && !strcmp(device, VIA_MIC_FALLBACKS[i])) continue;
		try_list[n++] = VIA_MIC_FALLBACKS[i];
	}
	try_list[n] = NULL;

	char last_err[256] = "没有可用的录音设备";
	for (int t = 0; try_list[t]; t++) {
		snd_pcm_t* pcm = NULL;
		int rc = snd_pcm_open(&pcm, try_list[t], SND_PCM_STREAM_CAPTURE, 0);
		if (rc < 0) {
			snprintf(last_err, sizeof(last_err), "%s: %s", try_list[t], snd_strerror(rc));
			continue;
		}

		snd_pcm_hw_params_t* hw = NULL;
		snd_pcm_hw_params_alloca(&hw);
		if ((rc = snd_pcm_hw_params_any(pcm, hw)) < 0) goto fail;
		if ((rc = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) goto fail;
		if ((rc = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE)) < 0) goto fail;
		if ((rc = snd_pcm_hw_params_set_channels(pcm, hw, 1)) < 0) goto fail;

		{
			unsigned want = (unsigned)rate;
			unsigned got = want;
			rc = snd_pcm_hw_params_set_rate_near(pcm, hw, &got, 0);
			if (rc < 0) goto fail;
			/* 硬件不接受 16k 就按它给的来；上层用实际值算 VAD 时长 */
			m->rate = (int)got;
		}

		/* 缓冲给足 200ms，避免 SD 卡或调度抖动导致采集丢样 */
		{
			snd_pcm_uframes_t bufsz = (snd_pcm_uframes_t)(m->rate / 5);
			snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bufsz);
		}

		if ((rc = snd_pcm_hw_params(pcm, hw)) < 0) goto fail;
		if ((rc = snd_pcm_prepare(pcm)) < 0) goto fail;

		m->pcm = pcm;
		snprintf(m->device, sizeof(m->device), "%s", try_list[t]);
		return 0;

	fail:
		snprintf(last_err, sizeof(last_err), "%s: %s", try_list[t], snd_strerror(rc));
		if (pcm) snd_pcm_close(pcm);
	}

	snprintf(m->err, sizeof(m->err), "%s", last_err);
	return -1;
}

int via_mic_read(ViaMic* m, short* buf, int frames, int timeout_ms) {
	if (!m->pcm) return -1;

	if (timeout_ms > 0) {
		/* 等数据可读，免得静音时死等 */
		for (int waited = 0; waited < timeout_ms; waited += 5) {
			int n = snd_pcm_avail_update((snd_pcm_t*)m->pcm);
			if (n < 0) {
				if (n == -EPIPE) { snd_pcm_prepare((snd_pcm_t*)m->pcm); continue; }
				if (n == -EAGAIN) { usleep(5000); continue; }
				return -1;
			}
			if (n > 0) break;
			usleep(5000);
		}
	}

	int got = 0;
	while (got < frames) {
		int rc = snd_pcm_readi((snd_pcm_t*)m->pcm, buf + got, frames - got);
		if (rc == -EPIPE) {                 /* 溢出：重同步，别直接失败 */
			snd_pcm_prepare((snd_pcm_t*)m->pcm);
			continue;
		}
		if (rc == -EAGAIN) { usleep(2000); continue; }
		if (rc < 0) {
			snprintf(m->err, sizeof(m->err), "%s", snd_strerror(rc));
			return -1;
		}
		got += rc;
	}
	return got;
}

void via_mic_close(ViaMic* m) {
	if (m->pcm) {
		snd_pcm_drop((snd_pcm_t*)m->pcm);
		snd_pcm_close((snd_pcm_t*)m->pcm);
		m->pcm = NULL;
	}
}

/* 一段 16bit PCM 的均方根音量（0~32767），给 VAD 和电平条用 */
int via_rms(const short* s, int n) {
	if (n <= 0) return 0;
	double acc = 0;
	for (int i = 0; i < n; i++) acc += (double)s[i] * s[i];
	return (int)sqrt(acc / n);
}

/* ------------------------------------------------------------------ 播放 */

int via_spk_open(ViaSpk* s, const char* device, int rate) {
	memset(s, 0, sizeof(*s));
	s->fd = -1;
	s->rate = rate;

	const char* try_list[4];
	int n = 0;
	if (device && device[0]) try_list[n++] = device;
	for (int i = 0; VIA_MIC_FALLBACKS[i] && n < 4; i++) {
		if (device && !strcmp(device, VIA_MIC_FALLBACKS[i])) continue;
		try_list[n++] = VIA_MIC_FALLBACKS[i];
	}
	try_list[n] = NULL;

	char last_err[256] = "没有可用的播放设备";
	for (int t = 0; try_list[t]; t++) {
		snd_pcm_t* pcm = NULL;
		int rc = snd_pcm_open(&pcm, try_list[t], SND_PCM_STREAM_PLAYBACK, 0);
		if (rc < 0) {
			snprintf(last_err, sizeof(last_err), "%s: %s", try_list[t], snd_strerror(rc));
			continue;
		}

		snd_pcm_hw_params_t* hw = NULL;
		snd_pcm_hw_params_alloca(&hw);
		if ((rc = snd_pcm_hw_params_any(pcm, hw)) < 0) goto fail;
		if ((rc = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) goto fail;
		if ((rc = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE)) < 0) goto fail;
		if ((rc = snd_pcm_hw_params_set_channels(pcm, hw, 1)) < 0) goto fail;
		{
			unsigned want = (unsigned)rate, got = want;
			if ((rc = snd_pcm_hw_params_set_rate_near(pcm, hw, &got, 0)) < 0) goto fail;
			s->rate = (int)got;
		}
		if ((rc = snd_pcm_hw_params(pcm, hw)) < 0) goto fail;
		if ((rc = snd_pcm_prepare(pcm)) < 0) goto fail;

		s->pcm = pcm;
		snprintf(s->device, sizeof(s->device), "%s", try_list[t]);
		return 0;

	fail:
		snprintf(last_err, sizeof(last_err), "%s: %s", try_list[t], snd_strerror(rc));
		if (pcm) snd_pcm_close(pcm);
	}

	snprintf(s->err, sizeof(s->err), "%s", last_err);
	return -1;
}

void via_spk_close(ViaSpk* s) {
	if (s->pcm) {
		snd_pcm_drain((snd_pcm_t*)s->pcm);
		snd_pcm_close((snd_pcm_t*)s->pcm);
		s->pcm = NULL;
	}
}

void via_spk_stop(ViaSpk* s) {
	if (s->pcm) snd_pcm_drop((snd_pcm_t*)s->pcm);
}

/*
 * 播放 PCM。TTS 是 24kHz，声卡通常跑 48kHz，所以这里做 2 倍线性插值上采样。
 *
 * 为什么不用 snd_pcm_hw_params_set_rate_near 直接要 24kHz：
 * 硬件步进（rate_step）可能不支持，plughw 的自动转换又依赖具体配置。
 * 自己插值一共就几行，且可控 —— 24k->48k 是整数倍，插值质量足够语音用。
 */
int via_spk_play(ViaSpk* s, const short* pcm, int frames,
                 int src_rate, void (*tick)(void*, int), void* tick_ctx) {
	if (!s->pcm || frames <= 0) return 0;
	if (src_rate <= 0) src_rate = 24000;

	double ratio = (double)s->rate / src_rate;
	int dst_total = (int)(frames * ratio);
	if (dst_total <= 0) return 0;

	short out[VIA_PB_FRAMES];
	int produced = 0;

	while (produced < dst_total) {
		int n = dst_total - produced;
		if (n > VIA_PB_FRAMES) n = VIA_PB_FRAMES;

		for (int i = 0; i < n; i++) {
			double sp = (produced + i) / ratio;      /* 源位置（浮点） */
			int i0 = (int)sp;
			double fr = sp - i0;
			if (i0 >= frames - 1) {
				out[i] = pcm[frames - 1];
			} else {
				double v = pcm[i0] * (1.0 - fr) + pcm[i0 + 1] * fr;
				if (v > 32767.0) v = 32767.0;
				if (v < -32768.0) v = -32768.0;
				out[i] = (short)v;
			}
		}

		int off = 0;
		while (off < n) {
			int rc = snd_pcm_writei((snd_pcm_t*)s->pcm, out + off, n - off);
			if (rc == -EPIPE) { snd_pcm_prepare((snd_pcm_t*)s->pcm); continue; }
			if (rc == -EAGAIN) { usleep(2000); continue; }
			if (rc < 0) {
				snprintf(s->err, sizeof(s->err), "%s", snd_strerror(rc));
				return -1;
			}
			off += rc;
		}

		produced += n;
		if (tick) tick(tick_ctx, produced * 100 / dst_total);   /* 进度 0~100 */
	}

	snd_pcm_drain((snd_pcm_t*)s->pcm);
	snd_pcm_prepare((snd_pcm_t*)s->pcm);   /* 准备下一次播放 */
	return 0;
}

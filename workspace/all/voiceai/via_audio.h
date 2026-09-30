#ifndef __VIA_AUDIO_H__
#define __VIA_AUDIO_H__

/* ALSA 采集 / 播放。pcm 用 void* 是以免把 alsa 头文件泄漏给调用方。 */

typedef struct {
	void* pcm;
	int   fd;          /* 占位，保持结构体布局稳定 */
	int   rate;
	char  device[64];
	char  err[256];
} ViaMic;

typedef struct {
	void* pcm;
	int   fd;
	int   rate;
	char  device[64];
	char  err[256];
} ViaSpk;

/* device 传 NULL 或空串就自动探测。rate 传 16000（采集）/ 48000（播放） */
int  via_mic_open(ViaMic* m, const char* device, int rate);
int  via_mic_read(ViaMic* m, short* buf, int frames, int timeout_ms);
void via_mic_close(ViaMic* m);

int  via_spk_open(ViaSpk* s, const char* device, int rate);
void via_spk_close(ViaSpk* s);
void via_spk_stop(ViaSpk* s);

/*
 * 播放 frames 个 16bit 单声道采样。src_rate 是数据本身的采样率
 * （TTS 是 24000），内部会插值到设备实际速率。
 * tick 每写一小块回调一次，参数是 0~100 的百分比；返回 -1 表示写失败。
 */
int  via_spk_play(ViaSpk* s, const short* pcm, int frames, int src_rate,
                  void (*tick)(void*, int), void* tick_ctx);

/* 一段 PCM 的均方根音量 0~32767 */
int  via_rms(const short* s, int n);

#endif

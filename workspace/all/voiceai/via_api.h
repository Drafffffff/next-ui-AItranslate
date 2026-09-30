#ifndef __VIA_API_H__
#define __VIA_API_H

#include "voiceai.h"
#include "via_pipeline.h"

/* ---- 底层 HTTP ----
 * 用 curl 子进程发请求。和 minarch 的 ma_ai.c 一个套路：
 * body 走 @文件 而不是命令行（几百 KB 的 base64 塞不进 4KB 的 cmd 缓冲）。
 * 返回 >0 = HTTP 状态码；-1 = 网络/传输失败；-2 = 没有 curl。
 */
int via_http_post_file(const char* url, const char* body_path, const char* resp_path,
                       const char* key, int timeout_secs,
                       char* err, int err_cap);

/* 把 HTTP >= 400 时响应体里的报错文案抠出来，拼成人话 */
void via_http_error_text(const char* resp_path, char* out, int cap);

/* 语音识别：wav 是完整的 RIFF 字节流。返回 0 成功 */
int via_stt(const ViaConfig* cfg, const unsigned char* wav, int wav_len,
            char* text, int text_cap, char* err, int err_cap);

/*
 * 对话。on_delta 每收到一段增量文字就回调一次（流式），传 NULL 则是整段返回。
 * 返回值：0 成功；>0 = HTTP 错误码；<0 = 网络/本地错误。
 */
typedef void (*ViaDeltaFn)(void* ctx, const char* delta);

int via_chat_stream(const ViaConfig* cfg, const ViaHistory* hist, int turns,
                    ViaDeltaFn on_delta, void* dctx,
                    char* reply, int reply_cap, char* err, int err_cap);

/* 流水线 worker 入口 */
void via_pipeline_run(ViaPipeline* p);

#endif

#ifndef __MA_AI_H__
#define __MA_AI_H__

/*
 * AI 画面翻译
 *
 * 游戏中按热键 -> 抓下当前画面 -> 发给支持视觉的模型 -> 把译文覆盖回原文所在的位置。
 *
 * 定位完全由模型负责（提示词要求它返回归一化坐标），本地不做任何找文字的图像算法。
 * 因此所选模型必须能识别文字并返回位置；显示端负责字号、排版和描边。
 *
 * 翻译设置在 .userdata/shared/ai-translate.txt；公共 Key 在 shared/ai-keys.txt。
 */
void Menu_aiTranslate(void);

/* Read-only CLI diagnostic. Never prints credentials or starts a request. */
int AI_credentialsStatus(void);

/* 游戏内 Options -> AI Translate 的子菜单（开关 / 服务商 / 停留时间） */
struct MenuList;
int OptionAI_openMenu(struct MenuList* list, int i);


/* ---- AI 帮你玩（自动操作） ----
 * 开一个后台线程反复「抓帧 -> 问模型按哪个键 -> 注入按键」，直到用户按任意键。
 * 主循环每帧调 AI_playTick()，画面翻转前调 AI_playDrawBadge() 画状态角标。
 */
void Menu_aiAutoPlay(void);          /* 热键入口：开始 / 停止 */
void AI_playTick(void);              /* 主循环每帧调用 */
void AI_playDrawBadge(SDL_Surface* dst); /* 在 screen_flip 之前调用 */
int  AI_playInjectMask(void);        /* 核心输入回调里取要注入的按键位 */
int  AI_playIsActive(void);
void AI_playStopFor(const char* why); /* 由别的功能抢用临时文件时先把它停掉 */

#endif

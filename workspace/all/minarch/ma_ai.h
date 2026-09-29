#ifndef __MA_AI_H__
#define __MA_AI_H__

/*
 * AI 画面翻译
 *
 * 游戏中按热键 -> 抓下当前画面 -> 发给支持视觉的模型 -> 把译文覆盖回原文所在的位置。
 *
 * 定位完全由模型负责（提示词要求它返回归一化坐标），本地不做任何找文字的图像算法。
 * 因此所选模型必须真的会 grounding：实测 qwen3-vl-plus / qwen3-vl-flash 稳定可用，
 * deepseek-flash 每次返回的 y 缩放系数都不一样，不能用。
 *
 * 配置项见 common/config.h 的 ai* 字段（写在 .userdata/shared/minuisettings.txt）。
 */
void Menu_aiTranslate(void);

/* 游戏内 Options -> AI Translate 的子菜单（开关 / 服务商 / 停留时间） */
struct MenuList;
int OptionAI_openMenu(struct MenuList* list, int i);

#endif

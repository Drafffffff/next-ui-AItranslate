#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define AI_MAX_ITEMS 32
#define AI_ITEM_LINES 12
typedef struct {
	char  orig[768];
	char  zh[768];
	float box[4];                /* 归一化 0~1000：左,上,右,下 */
	float lines[AI_ITEM_LINES][4];
	int   nlines;
	char  color[16];
	char  align[12];
	char  kind[16];
} AI_Item;

/* ---- SDL_ttf 的假实现：只为验证折行/挑字号的**逻辑**，不追求真实字形宽度 ----
 * 假定：汉字宽 ≈ 1em、ASCII ≈ 0.55em、行高 ≈ 1.2em（真机字体也是这个量级）。
 * 所以折行结果和真机同量级；真机字形细节另有截图肉眼验收。 */
typedef struct { int size; } TTF_Font;

static TTF_Font g_font;

static TTF_Font* TTF_OpenFont(const char* path, int size) {
	(void)path;
	g_font.size = size;
	return &g_font;
}
static void TTF_CloseFont(TTF_Font* f) { (void)f; }
static void TTF_SetFontStyle(TTF_Font* f, int s) { (void)f; (void)s; }
static int  TTF_FontLineSkip(const TTF_Font* f) { return f->size * 12 / 10; }

static int TTF_SizeUTF8(TTF_Font* f, const char* s, int* w, int* h) {
	int width = 0;
	const unsigned char* p = (const unsigned char*)s;
	while (*p) {
		int cl = (*p < 0x80) ? 1 : ((*p < 0xE0) ? 2 : ((*p < 0xF0) ? 3 : 4));
		width += (cl == 1) ? f->size * 55 / 100 : f->size;
		p += cl;
	}
	if (w) *w = width;
	if (h) *h = f->size * 12 / 10;
	return 0;
}

static int CFG_getFontStyle(void) { return 0; }

static const char* ai_skip_ws(const char* p) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

static const char* ai_find_key(const char* obj, const char* key) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char* p = obj;
	while ((p = strstr(p, pat)) != NULL) {
		const char* q = ai_skip_ws(p + strlen(pat));
		if (*q == ':') return ai_skip_ws(q + 1);
		p++;
	}
	return NULL;
}

static void ai_utf8_append(char* out, int cap, int* j, unsigned cp) {
	if (*j >= cap - 4) return;
	if (cp < 0x80) {
		out[(*j)++] = (char)cp;
	} else if (cp < 0x800) {
		out[(*j)++] = (char)(0xC0 | (cp >> 6));
		out[(*j)++] = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out[(*j)++] = (char)(0xE0 | (cp >> 12));
		out[(*j)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*j)++] = (char)(0x80 | (cp & 0x3F));
	} else {
		out[(*j)++] = (char)(0xF0 | (cp >> 18));
		out[(*j)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[(*j)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*j)++] = (char)(0x80 | (cp & 0x3F));
	}
}

static int ai_json_string(const char* obj, const char* key, char* out, int cap) {
	out[0] = '\0';
	const char* p = ai_find_key(obj, key);
	if (!p || *p != '"') return -1;
	p++;
	int j = 0;
	while (*p && *p != '"') {
		if (*p == '\\') {
			p++;
			switch (*p) {
				case 'n': out[j++] = '\n'; p++; break;
				case 't': out[j++] = '\t'; p++; break;
				case 'r': out[j++] = '\r'; p++; break;
				case 'b': out[j++] = '\b'; p++; break;
				case 'f': out[j++] = '\f'; p++; break;
				case 'u': {
					unsigned cp = 0;
					for (int k = 0; k < 4 && isxdigit((unsigned char)p[1 + k]); k++) {
						char c = p[1 + k];
						cp = cp * 16 + (c <= '9' ? c - '0' : (tolower(c) - 'a' + 10));
					}
					p += 5;
					/* 代理对：把高低位合起来 */
					if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
						unsigned lo = 0;
						for (int k = 0; k < 4 && isxdigit((unsigned char)p[2 + k]); k++) {
							char c = p[2 + k];
							lo = lo * 16 + (c <= '9' ? c - '0' : (tolower(c) - 'a' + 10));
						}
						if (lo >= 0xDC00 && lo <= 0xDFFF) {
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							p += 6;
						}
					}
					ai_utf8_append(out, cap, &j, cp);
					break;
				}
				default: out[j++] = *p ? *p : '\\'; if (*p) p++; break;
			}
		} else {
			out[j++] = *p++;
		}
		if (j >= cap - 4) break;
	}
	out[j] = '\0';
	return j;
}

static int ai_json_numbers(const char* obj, const char* key, float* out, int n) {
	const char* p = ai_find_key(obj, key);
	if (!p || *p != '[') return -1;
	p++;
	for (int i = 0; i < n; i++) {
		p = ai_skip_ws(p);
		out[i] = (float)atof(p);
		while (*p && *p != ',' && *p != ']') p++;
		if (*p == ',') p++;
	}
	return 0;
}

/* 从响应里抠出 items 数组。返回条数，-1 表示结构不对 */
static int ai_parse_items(const char* resp, AI_Item* items, int max) {
	const char* p = strstr(resp, "\"items\"");
	if (!p) return -1;
	p = strchr(p, '[');
	if (!p) return -1;
	p++;

	int n = 0;
	while (n < max) {
		p = ai_skip_ws(p);
		if (*p != '{') break;
		const char* q = p;
		int depth = 0, instr = 0;
		for (; *q; q++) {
			if (instr) {
				if (*q == '\\') { if (q[1]) q++; }
				else if (*q == '"') instr = 0;
			} else if (*q == '"') {
				instr = 1;
			} else if (*q == '{') {
				depth++;
			} else if (*q == '}') {
				depth--;
				if (depth == 0) break;
			}
		}
		if (*q != '}') break;

		size_t len = (size_t)(q - p) + 1;
		char* obj = (char*)malloc(len + 1);
		if (!obj) break;
		memcpy(obj, p, len);
		obj[len] = '\0';

		AI_Item* it = &items[n];
		memset(it, 0, sizeof(*it));
		ai_json_string(obj, "orig", it->orig, sizeof(it->orig));
		ai_json_string(obj, "zh", it->zh, sizeof(it->zh));
		ai_json_string(obj, "color", it->color, sizeof(it->color));
		ai_json_string(obj, "align", it->align, sizeof(it->align));
		int has_box = (ai_json_numbers(obj, "box", it->box, 4) == 0);
		free(obj);

		if (has_box && it->zh[0]) n++;
		/* 跳到下一个元素：数组元素之间是逗号，漏掉它就只会解出第一条 */
		p = q + 1;
		p = ai_skip_ws(p);
		if (*p == ',') p++;
	}
	return n;
}


/* ---- 自测：坐标换算 + 折行 ---- */
static int g_fail = 0;
static void CHECK(int ok, const char* fmt, ...) {
	if (ok) return;
	va_list ap;
	va_start(ap, fmt);
	printf("  [FAIL] ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	g_fail++;
}

static void test_normalize(void) {
	printf("\n== 坐标换算 ai_boxes_normalize ==\n");
	AI_Item it[2];

	/* 提示词要的就是这种：0~1 的小数 → ×1000（最常走的一条路） */
	memset(it, 0, sizeof(it));
	it[0].box[0] = 0.25f; it[0].box[1] = 0.10f; it[0].box[2] = 0.78f; it[0].box[3] = 0.15f;
	it[1].box[0] = 0.30f; it[1].box[1] = 0.40f; it[1].box[2] = 0.60f; it[1].box[3] = 0.50f;
	const char* sp = ai_boxes_normalize(it, 2, 384, 288);
	CHECK(!strcmp(sp, "0~1"), "0~1 被误判为 %s", sp);
	CHECK(it[0].box[2] > 770 && it[0].box[2] < 790, "0~1 横向换算错：%.0f", it[0].box[2]);
	CHECK(it[1].box[3] > 490 && it[1].box[3] < 510, "0~1 第二条换算错：%.0f", it[1].box[3]);
	printf("  0~1 的小数（提示词要的写法）→ ×1000 ✓ (%s)  [0.25,0.1,0.78,0.15] → [%.0f,%.0f,%.0f,%.0f]\n",
		sp, it[0].box[0], it[0].box[1], it[0].box[2], it[0].box[3]);

	/* 模型自作主张按 0~1000 整数给：原样保留（比例本来就一样） */
	memset(it, 0, sizeof(it));
	it[0].box[0] = 250; it[0].box[1] = 90; it[0].box[2] = 780; it[0].box[3] = 150;
	sp = ai_boxes_normalize(it, 1, 384, 288);
	CHECK(!strcmp(sp, "0~1000"), "0~1000 被误判为 %s", sp);
	CHECK(it[0].box[2] == 780.0f && it[0].box[3] == 150.0f,
		"0~1000 不该被改动：got [%.0f,%.0f]", it[0].box[2], it[0].box[3]);
	printf("  模型自己按 0~1000 整数给 → 原样保留 ✓ (%s)\n", sp);

	/* 像素值：坐标空间「不可能」是发出去那张图的像素时才敢换算。
	 * 这里 x=1200 > 图宽 384，也不在 0~1000 里 → 只能是像素（或者模型乱来）。 */
	memset(it, 0, sizeof(it));
	it[0].box[0] = 8; it[0].box[1] = 6; it[0].box[2] = 1200; it[0].box[3] = 30;
	sp = ai_boxes_normalize(it, 1, 384, 288);
	CHECK(!strcmp(sp, "像素"), "越界的像素空间被误判为 %s", sp);
	CHECK(it[0].box[2] > 3100 && it[0].box[2] < 3130, "横向像素没按图宽折算：%.0f", it[0].box[2]);
	CHECK(it[0].box[3] > 100 && it[0].box[3] < 110, "纵向像素没按图高折算：%.0f", it[0].box[3]);
	printf("  越界像素值 → 按发图 384x288 折算 ✓ (%s)  [8,6,1200,30] → [%.0f,%.0f,%.0f,%.0f]\n",
		sp, it[0].box[0], it[0].box[1], it[0].box[2], it[0].box[3]);

	/* 落在图内的值 → 判不出来，按 0~1000 原样用。
	 * 这是已知盲区：deepseek 在 384 宽的图上按像素给值时，数值也全在图内，
	 * 和 0~1000 无法区分（实测它的比例还会随图片尺寸变，属于模型侧限制）。 */
	memset(it, 0, sizeof(it));
	it[0].box[0] = 30; it[0].box[1] = 40; it[0].box[2] = 240; it[0].box[3] = 90;
	sp = ai_boxes_normalize(it, 1, 384, 288);
	CHECK(!strcmp(sp, "0~1000"), "图内的值不该乱换算，实际 %s", sp);
	CHECK(it[0].box[2] == 240.0f, "图内的值被改动了：%.0f", it[0].box[2]);
	printf("  落在图内的值 → 判不出来，按 0~1000 原样用 ✓ (%s)  ← 已知盲区\n", sp);

	/* lines（逐行框）也要跟着一起换算 */
	memset(it, 0, sizeof(it));
	it[0].box[0]=0.1f; it[0].box[1]=0.2f; it[0].box[2]=0.9f; it[0].box[3]=0.6f;
	it[0].nlines = 3;
	for (int l = 0; l < 3; l++) {
		it[0].lines[l][0]=0.1f; it[0].lines[l][1]=0.2f+0.13f*l;
		it[0].lines[l][2]=0.9f; it[0].lines[l][3]=0.3f+0.13f*l;
	}
	sp = ai_boxes_normalize(it, 1, 384, 288);
	CHECK(!strcmp(sp, "0~1"), "带 lines 的 0~1 被误判为 %s", sp);
	CHECK(it[0].lines[2][1] > 450 && it[0].lines[2][1] < 470, "lines 纵向没换算：%.0f", it[0].lines[2][1]);
	CHECK(it[0].lines[0][2] == 900.0f, "lines 横向没换算：%.0f", it[0].lines[0][2]);
	printf("  lines（逐行框）随 box 一起换算 ✓ (%s)  lines[2][1]=%.0f\n", sp, it[0].lines[2][1]);

	/* 不知道发图尺寸时不能瞎算 */
	memset(it, 0, sizeof(it));
	it[0].box[2] = 1200;
	sp = ai_boxes_normalize(it, 1, 0, 0);
	CHECK(!strcmp(sp, "像素(无尺寸)"), "无尺寸时应拒绝换算，实际 %s", sp);
	CHECK(it[0].box[2] == 1200.0f, "无尺寸时不该改动坐标");
	printf("  有像素值但没图尺寸 → 不换算、不崩 ✓ (%s)\n", sp);
}

/* 折出来的每一行拼回去必须和原文一致（不丢字、不重复、不吞空格） */
static void join_lines(char* out, int cap, char* flat, AI_Line* lines, int nl, int with_space) {
	out[0] = '\0';
	int j = 0;
	for (int i = 0; i < nl; i++) {
		if (with_space && i) out[j++] = ' ';
		for (const char* p = flat + lines[i].off; *p && j < cap - 2; p++) out[j++] = *p;
	}
	out[j] = '\0';
}

static void test_wrap(void) {
	printf("\n== 折行与挑字号 ai_wrap / ai_fit_lines ==\n");
	char flat[AI_FLAT_CAP];
	AI_Line lines[AI_MAX_LINES];
	TTF_Font* f = NULL;
	int size = 0, nl = 0;
	char joined[2048];

	/* 单字 + 宽框：字号应该被框高顶住，不会缩成蚂蚁 */
	nl = ai_fit_lines("f.ttf", "开", 100, 48, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
	CHECK(nl == 1, "「开」应折成 1 行，实际 %d", nl);
	CHECK(f != NULL, "「开」没给出字体");
	CHECK(size >= 30 && size <= 48, "「开」的字号不合理：%d（框高 48）", size);
	printf("  「开」+ 100x48 的框 → %d 行 字号 %d ✓\n", nl, size);
	if (f) TTF_CloseFont(f);

	/* 长句 + 窄框：折成多行，每行塞得进框宽，整块塞得进框高，且不丢字 */
	{
		const char* t = "蝙蝠信号划破夜空，蝙蝠侠重返哥谭市。";
		nl = ai_fit_lines("f.ttf", t, 160, 60, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl >= 2, "长句应折成多行，实际 %d", nl);
		int wide = 0;
		for (int i = 0; i < nl; i++) if (lines[i].w > 160) wide++;
		int h = nl * TTF_FontLineSkip(f);
		CHECK(!wide, "有 %d 行超过框宽 160（%d 宽）", wide, lines[0].w);
		CHECK(h <= 60, "整块高 %d 超过框高 60", h);
		join_lines(joined, sizeof(joined), flat, lines, nl, 0);
		CHECK(!strcmp(joined, t), "折行后拼回来不一致：\n         原=%s\n         拼=%s", t, joined);
		printf("  长句 + 160x60 → %d 行 字号 %d 总高 %d/60 ✓  拼回原文一致 ✓\n", nl, size, h);
		if (f) TTF_CloseFont(f);
	}

	/* 西文：能断在空格后就不许把单词劈开 */
	{
		const char* t = "THE BAT SIGNAL BLAZES INTO";
		nl = ai_fit_lines("f.ttf", t, 120, 40, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl >= 2, "这句应该折成多行，实际 %d", nl);
		join_lines(joined, sizeof(joined), flat, lines, nl, 1);   /* 用空格拼回去 */
		CHECK(!strcmp(joined, t), "西文断行劈开了单词：\n         原=%s\n         拼=%s", t, joined);
		int bad = 0;
		for (int i = 0; i < nl; i++)
			if (flat[lines[i].off] == ' ' ||
				flat[lines[i].off + (int)strlen(flat + lines[i].off) - 1] == ' ') bad++;
		CHECK(!bad, "有行首/行尾空格没清掉");
		printf("  「%s」+ 120x40 → %d 行 字号 %d，按空格断、单词没劈开 ✓\n", t, nl, size);
		if (f) TTF_CloseFont(f);
	}

	/* 一个超宽的长单词：没有空格可断，只能拆开，而且不能死循环 */
	{
		nl = ai_fit_lines("f.ttf", "Supercalifragilisticexpialidocious", 60, 30,
			0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl >= 2, "超宽长单词应该被拆行，实际 %d", nl);
		join_lines(joined, sizeof(joined), flat, lines, nl, 0);
		CHECK(!strcmp(joined, "Supercalifragilisticexpialidocious"), "拆行丢字：%s", joined);
		printf("  超宽长单词 + 60x30 → %d 行 字号 %d，拆开且没死循环 ✓\n", nl, size);
		if (f) TTF_CloseFont(f);
	}

	/* 断行优先落在标点后：把长句喂进一个「填满才断」的框，断点应落在标点之后 */
	{
		const char* t = "蝙蝠侠、小丑及相关角色、标志和标记均为商标。版权所有。由任天堂授权发行。";
		nl = ai_fit_lines("f.ttf", t, 200, 400, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		int end_punct = 0;
		for (int i = 0; i < nl - 1; i++) {          /* 最后一行不用管 */
			int L = (int)strlen(flat + lines[i].off);
			const char* tail = flat + lines[i].off + L - 3;   /* 收尾标点是 3 字节 */
			if (L >= 3 && !strncmp(tail, "、", 3)) end_punct++;
			if (L >= 3 && !strncmp(tail, "。", 3)) end_punct++;
			if (L >= 3 && !strncmp(tail, "，", 3)) end_punct++;
		}
		printf("  标点优先断行 + 框宽 200 → %d 行 [", nl);
		for (int i = 0; i < nl; i++) printf("%s%s", i ? " / " : "", flat + lines[i].off);
		printf("]\n");
		CHECK(end_punct > 0, "没有任何一行断在标点后（说明退回了硬断）");
		join_lines(joined, sizeof(joined), flat, lines, nl, 0);
		CHECK(!strcmp(joined, t), "标点断行后拼回来不一致");
		if (f) TTF_CloseFont(f);
	}

	/* 屏幕宽度上限：框比屏幕还宽时，折行要按屏幕可用宽度算 */
	{
		const char* t = "这是一句很长的台词，用来验证可用宽度会被屏幕裁剪，而不是按框宽铺到底溢出屏幕外。";
		nl = ai_fit_lines("f.ttf", t, 6, 400, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl > 0, "6px 窄框也该尽力给结果");
		nl = ai_fit_lines("f.ttf", t, 2, 400, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl == 0, "2px 的框应该直接放弃（build 里有 max_w<4 的守卫）");
		printf("  窄框：6px 尽力给出结果、2px 直接放弃，都没崩 ✓\n");
		if (f) TTF_CloseFont(f);
	}

	/* 行首不许挂收尾标点：断点落在「，」前面时要把它捎到上一行。
	 * 折行位置随框宽变，所以扫一串框宽、逐个断言 —— 只测某一个宽度很容易刚好绕过。 */
	{
		const char* t = "很久很久以前，这个村子里架着一座大桥；那时候，河的两岸都是稻田。";
		int bad = 0, unjoined = 0, cases = 0, shown = 0;
		for (int mw = 60; mw <= 220; mw += 7) {
			nl = ai_fit_lines("f.ttf", t, mw, 400, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
			if (nl <= 0) continue;
			cases++;
			for (int i = 0; i < nl; i++)
				if (ai_is_no_line_start(flat + lines[i].off)) bad++;
			join_lines(joined, sizeof(joined), flat, lines, nl, 0);
			if (strcmp(joined, t)) unjoined++;
			if (!shown && nl > 1 && mw > 150) {
				printf("  中文长句 + 框宽 %d → %d 行 [", mw, nl);
				for (int i = 0; i < nl; i++) printf("%s%s", i ? " / " : "", flat + lines[i].off);
				printf("]\n");
				shown = 1;
			}
			if (f) TTF_CloseFont(f);
		}
		CHECK(cases >= 10, "折行用例太少（%d），测试没覆盖到", cases);
		CHECK(!bad, "扫了 %d 种框宽，有 %d 行以收尾标点开头", cases, bad);
		CHECK(!unjoined, "有 %d 种框宽下拼回来和原文不一致", unjoined);
		printf("  扫 %d 种框宽：行首无收尾标点、拼回原文一致 ✓\n", cases);
	}

	/* 最小字号：给一个很小的框 + 很小的 size_cap，字号也不许低于下限 */
	{
		nl = ai_fit_lines("f.ttf", "这句话不许被缩成蚂蚁", 400, 400, 12, 20, &f, &size,
			flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(size >= 20, "最小字号没生效：%d", size);
		printf("  size_cap=12 + 最小字号 20 → 实际字号 %d ✓\n", size);
		if (f) TTF_CloseFont(f);
	}

	/* 到达字号下限时仍须返回有效字体，不能先释放再 break 导致整段消失。 */
	{
		nl = ai_fit_lines("f.ttf", "不能消失的译文", 90, 12, 20, 20, &f, &size,
			flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl > 0 && f != NULL && size == 20, "下限溢出时丢失字体：nl=%d size=%d", nl, size);
		if (f) TTF_CloseFont(f);
	}

	/* 模型自己塞了 \n：当硬断行，不许并成一行 */
	{
		nl = ai_fit_lines("f.ttf", "第一行\n第二行", 400, 80, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl == 2, "\\n 应该断成 2 行，实际 %d", nl);
		CHECK(nl == 2 && !strcmp(flat + lines[0].off, "第一行") && !strcmp(flat + lines[1].off, "第二行"),
			"\\n 断行结果不对");
		printf("  模型塞的 \\n → 硬断行 ✓ (%d 行)\n", nl);
		if (f) TTF_CloseFont(f);
	}

	/* 框太窄：给不出结果也不能崩/死循环 */
	{
		nl = ai_fit_lines("f.ttf", "很长很长的句子", 3, 3, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl == 0 && f == NULL, "3x3 的框应返回 0，实际 nl=%d font=%p", nl, (void*)f);
		printf("  3x3 的框 → 放弃绘制、不崩 ✓ (nl=%d)\n", nl);
	}

	/* 超长文本不能把 flat 写爆（32 条以内，每条 zh 最多 767 字节）。
	 * 装不下时按最小字号尽力画（不返回 0），行数不超过上限。 */
	{
		char big[768];
		memset(big, 0, sizeof(big));
		for (int i = 0; i + 3 < 760; i += 3) { big[i] = 0xE4; big[i+1] = 0xB8; big[i+2] = 0x80; }  /* 一 */
		nl = ai_fit_lines("f.ttf", big, 100, 100, 0, 0, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl >= 1 && nl <= AI_MAX_LINES, "超长文本行数异常：%d", nl);
		CHECK(size >= 8, "字号越界：%d", size);
		printf("  767 字节满长度文本 → %d 行（上限 %d）字号 %d，没有越界 ✓\n", nl, AI_MAX_LINES, size);
		if (f) TTF_CloseFont(f);
	}
}

static void test_overlay_policy(void) {
	CHECK(ai_readable_min(720) == 32, "720 高的最小字号应为 32");
	CHECK(ai_readable_min(768) == 35, "768 高的最小字号应向上取整到 35");
	CHECK(ai_outline_width(40) == 8, "40px 文字应有 8px 描边");
	AI_Item items[2];
	int n = ai_parse_items("{\"items\":[{\"zh\":\"继续游戏\",\"kind\":\"menu\",\"box\":[0.2,0.4,0.7,0.5]}]}", items, 2);
	CHECK(n == 1 && !strcmp(items[0].kind, "menu"), "未解析出菜单层级");
	n = ai_parse_items("{\"items\":[{\"zh\":\"旧响应\",\"box\":[0.2,0.4,0.7,0.5]}]}", items, 2);
	CHECK(n == 1 && !items[0].kind[0], "旧响应应允许省略 kind");
	for (int width = 30; width <= 400; width += 37) {
		TTF_Font* f = NULL; int size = 0;
		char flat[AI_FLAT_CAP]; AI_Line lines[AI_MAX_LINES];
		int nl = ai_fit_lines("f.ttf", "这段对白不能为了塞进小框而变成小字。", width, 24,
			40, 32, &f, &size, flat, sizeof(flat), lines, AI_MAX_LINES);
		CHECK(nl > 0 && f && size >= 32, "窄框 %d 下违反阅读下限：%d", width, size);
		if (f) TTF_CloseFont(f);
	}
	printf("  掌机字号下限、加粗描边、层级解析和旧响应兼容 ✓\n");
}

int main(int argc, char** argv) {
	test_normalize();
	test_wrap();
	test_overlay_policy();
	if (argc < 2) {
		printf("\n%s（没给响应文件，只跑了自测）\n", g_fail ? "有失败" : "全部通过");
		return g_fail ? 2 : 0;
	}
	{
	FILE* f = fopen(argv[1], "rb");
	if (!f) { printf("打不开 %s\n", argv[1]); return 1; }
	fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
	char* resp = malloc(len+1);
	fread(resp, 1, len, f); resp[len] = 0; fclose(f);
	printf("响应长度 %ld\n", len);

	/* 完全照搬 Menu_aiTranslate 里的解析流程 */
	size_t clen = strlen(resp) + 1;
	char* content = malloc(clen);
	int n_str = ai_json_string(resp, "content", content, (int)clen);
	if (n_str <= 0) { snprintf(content, clen, "%s", resp); printf("(content 抽取失败，退化为裸响应)\n"); }
	else printf("content 反转义成功，长度 %d\n", n_str);

	char* cp = content;
	while (*cp==' '||*cp=='\n'||*cp=='\r'||*cp=='\t') cp++;
	if (!strncmp(cp, "```", 3)) { cp += 3; if (!strncmp(cp,"json",4)) cp += 4; while (*cp=='\n'||*cp=='\r') cp++; }

	AI_Item items[AI_MAX_ITEMS];
	int n = ai_parse_items(cp, items, AI_MAX_ITEMS);
	printf("解析出 %d 条：\n", n);
	const char* sp = "（没解出条目）";
	if (n > 0) sp = ai_boxes_normalize(items, n, 384, 288);
	for (int i = 0; i < n; i++) {
		printf("  [%d] orig=\"%s\"  zh=\"%s\"  box=[%.0f,%.0f,%.0f,%.0f]  color=%s align=%s\n",
			i, items[i].orig, items[i].zh, items[i].box[0], items[i].box[1],
			items[i].box[2], items[i].box[3], items[i].color, items[i].align);
	}
	char csum[256];
	ai_json_string(cp, "context", csum, sizeof(csum));
	printf("context 摘要：%s\n", csum[0] ? csum : "（无）");
	for (int i = 0; i < n; i++) {
		printf("  [%d] 逐行框 %d 个", i, items[i].nlines);
		for (int l = 0; l < items[i].nlines; l++)
			printf("  [%.0f,%.0f,%.0f,%.0f]", items[i].lines[l][0], items[i].lines[l][1],
				items[i].lines[l][2], items[i].lines[l][3]);
		printf("\n");
	}
	printf("坐标空间判定：%s（换算后已写成 0~1000，可直接当屏幕比例看）\n", sp);
	}
	return (g_fail == 0) ? 0 : 2;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define AI_MAX_ITEMS 32
typedef struct {
	char  orig[128];
	char  zh[768];
	float box[4];                /* 归一化 0~1000：左,上,右,下 */
	char  color[16];
	char  align[12];
} AI_Item;

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


int main(int argc, char** argv) {
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
	for (int i = 0; i < n; i++) {
		printf("  [%d] orig=\"%s\"  zh=\"%s\"  box=[%.0f,%.0f,%.0f,%.0f]  color=%s align=%s\n",
			i, items[i].orig, items[i].zh, items[i].box[0], items[i].box[1],
			items[i].box[2], items[i].box[3], items[i].color, items[i].align);
	}
	return n > 0 ? 0 : 2;
}

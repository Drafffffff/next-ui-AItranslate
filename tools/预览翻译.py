#!/usr/bin/env python3
"""从当前源码提取真正的 SDL 渲染器，离线回放截图和缓存响应，不请求 API。

需要宿主机 clang/gcc、pkg-config、SDL2、SDL2_image、SDL2_ttf。
用法：python3 tools/预览翻译.py 原图.png 响应.json 字体.ttf 输出.png
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def renderer_source():
    src = (Path(__file__).resolve().parents[1] / "workspace/all/minarch/ma_ai.c").read_text()
    def grab(start, end):
        a = src.index(start)
        return src[a:src.index(end, a)]
    result = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#define AI_MAX_ITEMS 32
#define AI_ITEM_LINES 12
#define RES_PATH ""
static const char* font_file;
static const char* CFG_getFontFile(void) { return font_file; }
static int CFG_getFontStyle(void) { return 0; }
''' + grab('typedef struct {\n\tchar  orig[768];', '/* ------------------------------------------------------------------ 配置读取 */') + grab('static const char* ai_skip_ws(', '/* ------------------------------------------------------------------ 绘制 */') + grab('/* Overlay 配色：', '/* 从 over 交叉淡出') + r'''
int main(int argc, char** argv) {
    if (argc != 5 || SDL_Init(0) || TTF_Init() || !(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) return 1;
    font_file = argv[3];
    FILE* file = fopen(argv[2], "rb");
    if (!file) return 2;
    fseek(file, 0, SEEK_END); long len = ftell(file); rewind(file);
    char* response = calloc(len + 1, 1);
    if (!response || fread(response, 1, len, file) != (size_t)len) return 3;
    fclose(file);
    char* content = calloc(len + 1, 1);
    ai_json_string(response, "content", content, len + 1);
    AI_Item items[AI_MAX_ITEMS];
    int n = ai_parse_items(content[0] ? content : response, items, AI_MAX_ITEMS);
    if (n <= 0) return 4;
    ai_boxes_normalize(items, n, 384, 288);
    SDL_Surface* input = IMG_Load(argv[1]);
    if (!input) return 5;
    int w = getenv("NEXTUI_WIDTH") ? atoi(getenv("NEXTUI_WIDTH")) : input->w;
    int h = getenv("NEXTUI_HEIGHT") ? atoi(getenv("NEXTUI_HEIGHT")) : input->h;
    if (w < 64 || h < 64 || w > 4096 || h > 4096) return 6;
    SDL_Surface* source = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_BlitScaled(input, NULL, source, NULL);
    SDL_Surface* over = SDL_ConvertSurface(source, source->format, 0);
    int drawn = ai_render_items(source, over, items, n);
    int result = IMG_SavePNG(over, argv[4]);
    printf("%d/%d items rendered\n", drawn, n);
    SDL_FreeSurface(input); SDL_FreeSurface(source); SDL_FreeSurface(over);
    free(content); free(response); TTF_Quit(); IMG_Quit(); SDL_Quit();
    return result != 0;
}
'''

    # 检验实际 SDL 字体路径：阅读下限必须在最终绘制时仍成立。
    return result.replace("\treturn nlay > 0;", r'''
    if (size < ai_readable_min(screen->h)) abort();
    if (getenv("NEXTUI_VERIFY")) fprintf(stderr, "layout size=%d outline=%d lines=%d\n", size, ol, nl);
    return nlay > 0;
''')


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--source":
        Path(sys.argv[2]).write_text(renderer_source())
    elif len(sys.argv) == 5:
        with tempfile.TemporaryDirectory(prefix="nextui-preview-") as temp:
            source, binary = Path(temp) / "preview.c", Path(temp) / "preview"
            source.write_text(renderer_source())
            flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl2", "SDL2_image", "SDL2_ttf"], text=True)
            subprocess.run([os.environ.get("CC", "cc"), str(source), "-o", str(binary), *shlex.split(flags)], check=True)
            subprocess.run([str(binary), *(str(Path(p).resolve()) for p in sys.argv[1:])], check=True)
    else:
        sys.exit(__doc__)

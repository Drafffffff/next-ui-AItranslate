#!/usr/bin/env python3
"""直接检查 C 调色板的文字/描边相对亮度对比度，不代表整个游戏画面的 AAA 认证。

标准：https://www.w3.org/WAI/WCAG22/Understanding/contrast-enhanced.html
"""
from pathlib import Path
import re


def luminance(rgb):
    linear = []
    for value in rgb:
        s = value / 255.0
        linear.append(s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4)
    return sum(a * b for a, b in zip(linear, (0.2126, 0.7152, 0.0722)))


def palette_ratios():
    source = (Path(__file__).resolve().parents[1] / "workspace/all/minarch/ma_ai.c").read_text()
    table = source.split("static const AI_Palette ai_palettes[] = {", 1)[1].split("};", 1)[0]
    entries = re.findall(r'\{"(\w+)",\s*\{([\d, ]+)\},\s*\{([\d, ]+)\}\}', table)
    assert len(entries) == 8, "调色板未完整读出"
    results = []
    for name, foreground, outline in entries:
        fg = tuple(map(int, foreground.split(",")))
        bg = tuple(map(int, outline.split(",")))
        assert len(fg) == len(bg) == 3 and all(0 <= n <= 255 for n in (*fg, *bg))
        low, high = sorted((luminance(fg), luminance(bg)))
        ratio = (high + 0.05) / (low + 0.05)
        assert ratio >= 7.0, f"{name}: {ratio} 不满足 7:1；阈值判断不得四舍五入"
        results.append((name, fg, bg, ratio))
    return results


if __name__ == "__main__":
    for name, fg, bg, ratio in palette_ratios():
        print(f"{name:6} {fg} / {bg}: {ratio:.3f}:1 PASS")

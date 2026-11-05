"""
產生韌體用的 12px 像素字型：src/assets/Font12.h / Font12.cpp

- 字型：俐方體 11 號 Cubic 11 (assets/fonts/Cubic_11.ttf，授權見 assets/fonts/OFL.txt)
- 收錄字：字型裡所有 BMP 範圍內有筆畫的字 (約 2 萬字，中日文、標點、全形符號)
  另外保證 src/ui/Strings.h 用到的字都在裡面 (沒有的話會警告)
- 因為要顯示 iPhone 通知等任意文字，所以收錄完整字集 (約 700KB Flash)

用法:  python tools/gen_font.py
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets", "fonts", "Cubic_11.ttf")
STRINGS = os.path.join(ROOT, "src", "ui", "Strings.h")
OUT = os.path.join(ROOT, "src", "assets")
CELL_H = 13  # 字高 (像素)

# 掃描的 Unicode 範圍 (BMP)
RANGES = [
    (0x20, 0x7E), (0xA0, 0x24F), (0x2000, 0x206F), (0x2100, 0x22FF), (0x2460, 0x27BF),
    (0x2E80, 0x2FDF), (0x3000, 0x30FF), (0x3100, 0x31FF), (0x3200, 0x33FF),
    (0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF), (0xFE30, 0xFE4F), (0xFF00, 0xFFEF),
]


def strings_chars():
    src = open(STRINGS, encoding="utf-8").read()
    src = re.sub(r"//[^\n]*", "", src)
    chars = set()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', src):
        chars.update(lit)
    return chars


def main():
    if not os.path.exists(FONT):
        sys.exit(f"找不到字型 {FONT}")
    font = ImageFont.truetype(FONT, 12)

    chars = []
    for lo, hi in RANGES:
        for cp in range(lo, hi + 1):
            ch = chr(cp)
            if cp == 0x20 or font.getmask(ch).getbbox():
                chars.append(ch)
    have = set(chars)
    missing = [c for c in strings_chars() if c not in have and ord(c) >= 0x20]
    if missing:
        print("警告：Strings.h 有字型沒有的字：", "".join(sorted(missing)))

    widths, rows = [], []
    for ch in chars:
        adv = 4 if ch == " " else int(round(font.getlength(ch)))
        adv = max(1, min(adv, 16))
        im = Image.new("1", (16, CELL_H), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((0, 0), ch, font=font, fill=1)
        widths.append(adv)
        for y in range(CELL_H):
            bits = 0
            for x in range(16):
                if im.getpixel((x, y)):
                    bits |= 1 << (15 - x)
            rows.append(bits)

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "Font12.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("// 由 tools/gen_font.py 自動產生，請勿手動修改\n#pragma once\n#include <stdint.h>\n\n"
                f"constexpr uint8_t FONT12_HEIGHT = {CELL_H};\n"
                f"constexpr uint16_t FONT12_COUNT = {len(chars)};\n"
                "extern const uint16_t FONT12_CODES[];   // 由小到大排序的 Unicode 碼點 (BMP)\n"
                "extern const uint8_t FONT12_WIDTHS[];   // 每個字的寬度 (像素)\n"
                "extern const uint16_t FONT12_ROWS[];    // 第 i 個字在 [i*FONT12_HEIGHT]，每列 16 bit，最高位 = 最左邊\n")
    with open(os.path.join(OUT, "Font12.cpp"), "w", encoding="utf-8", newline="\n") as f:
        f.write("// 由 tools/gen_font.py 自動產生，請勿手動修改\n#include \"Font12.h\"\n\n")
        f.write("const uint16_t FONT12_CODES[] = {\n")
        for i in range(0, len(chars), 16):
            f.write("  " + ",".join(f"0x{ord(c):X}" for c in chars[i:i + 16]) + ",\n")
        f.write("};\nconst uint8_t FONT12_WIDTHS[] = {\n")
        for i in range(0, len(widths), 32):
            f.write("  " + ",".join(map(str, widths[i:i + 32])) + ",\n")
        f.write("};\nconst uint16_t FONT12_ROWS[] = {\n")
        for i in range(0, len(rows), CELL_H):
            f.write("  " + ",".join(f"0x{r:X}" for r in rows[i:i + CELL_H]) + ",\n")
        f.write("};\n")
    size_kb = (len(chars) * (2 + 1 + CELL_H * 2)) / 1024
    print(f"font: {len(chars)} glyphs (~{size_kb:.0f} KB) -> src/assets/Font12.*")


if __name__ == "__main__":
    main()

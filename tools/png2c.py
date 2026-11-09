"""
把 assets/ 底下的 PNG 素材轉成韌體用的 C++ 陣列：src/assets/Sprites.h / Sprites.cpp

- 每張圖轉成「調色盤 (RGB565) + 每像素 1 byte 索引」，索引 0 = 透明。
- 每張圖最多 255 種不透明顏色；alpha < 128 視為透明。
- 寵物影格與裝扮的對齊點讀自 assets/anchors.json。

替換素材流程：
  1. 用像素繪圖軟體覆蓋 assets/ 裡的 PNG (尺寸規格見 assets/ASSETS.md)
  2. 如有需要，修改 assets/anchors.json 的對齊點
  3. python tools/png2c.py
  4. 重新編譯燒錄
"""
import json
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
OUT_DIR = os.path.join(ROOT, "src", "assets")

ANCHOR_IDS = {"head_top": 0, "eyes": 1, "neck": 2}

# 角色 (assets/pets/<名稱>/)。順序 = 韌體裡的角色編號 (設定存的是編號，新角色請加在最後)
# 角色名稱在 src/ui/Strings.h 的 Str::CHARACTER_NAMES，數量要一致
CHARACTERS = ["ayame"]

# 韌體程式會引用的素材 (缺任何一個就報錯，避免編譯時才發現)
# 每個角色都要有這些影格；順序 = PetFrameId (PF_xxx)
REQUIRED_PET = ["idle/idle_0", "idle/idle_1", "blink/blink_0", "happy/happy_0", "happy/happy_1",
                "sleep/sleep_0", "sleep/sleep_1", "wake/wake_0", "wake/wake_1", "wake/wake_2",
                "look/look_left", "look/look_right"]
REQUIRED_ACC = ["bow/bow", "crown/crown", "glasses/glasses", "leaf_hat/leaf_hat",
                "santa_hat/santa_hat", "sleep_hat/sleep_hat", "cape/cape_back", "cape/cape_front",
                "star_eyes/star_eyes"]
REQUIRED_UI = ["wifi_on", "wifi_off", "sparkle_0", "sparkle_1", "sparkle_2", "zzz", "shadow",
               "snack", "sweat", "bolt", "weather_sun", "weather_partly", "weather_cloud",
               "weather_rain", "weather_thunder", "weather_snow", "weather_fog", "bg_night", "bg_dawn", "bg_day", "bg_dusk",
               "ov_sun", "ov_moon", "ov_cloud_white", "ov_cloud_grey", "ov_cloud_dark", "ov_cloud_night",
               "ov_fog", "fan", "scarf", "butterfly_0", "butterfly_1", "walk_flower", "hand", "heart"]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def ident(name):
    return re.sub(r"[^0-9a-zA-Z_]", "_", name)


def convert(path):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    if w > 255 or h > 255:
        sys.exit(f"{path}: 尺寸 {w}x{h} 超過 255")
    palette = [0]          # 0 = 透明 (值不使用)
    lut = {}
    idx = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            if a < 128:
                idx.append(0)
                continue
            c = rgb565(r, g, b)
            if c not in lut:
                if len(palette) >= 256:
                    sys.exit(f"{path}: 顏色超過 255 種，請減少顏色")
                lut[c] = len(palette)
                palette.append(c)
            idx.append(lut[c])
    return w, h, palette, idx


def emit_sprite(cpp, sym, w, h, palette, idx):
    cpp.append(f"static const uint16_t {sym}_pal[] = {{" + ",".join(f"0x{c:04X}" for c in palette) + "};")
    rows = []
    for y in range(h):
        rows.append("  " + ",".join(str(v) for v in idx[y * w:(y + 1) * w]))
    cpp.append(f"static const uint8_t {sym}_px[] = {{\n" + ",\n".join(rows) + "\n};")
    cpp.append(f"const Sprite {sym} = {{{w}, {h}, {sym}_pal, {sym}_px}};")


def main():
    with open(os.path.join(ASSETS, "anchors.json"), encoding="utf-8") as f:
        anchors = json.load(f)

    hdr = ["// 由 tools/png2c.py 自動產生，請勿手動修改。素材來源: assets/",
           "#pragma once", "#include \"SpriteTypes.h\"", ""]
    cpp = ["// 由 tools/png2c.py 自動產生，請勿手動修改。素材來源: assets/",
           "#include \"Sprites.h\"", ""]

    def check(path):
        if not os.path.exists(path):
            sys.exit(f"缺少素材: {os.path.relpath(path, ROOT)}")

    # 寵物影格：每個角色一組，最後輸出 PET_FRAMES[角色][影格] 對照表
    hdr.append("// 寵物影格編號 (每個角色都有同一組影格)")
    hdr.append("enum PetFrameId : uint8_t {")
    hdr.append("  " + ", ".join("PF_" + ident(os.path.basename(n)) for n in REQUIRED_PET) + ", PF_COUNT")
    hdr.append("};")
    hdr.append(f"constexpr uint8_t PET_CHAR_COUNT = {len(CHARACTERS)};")
    hdr.append("extern const PetFrameAsset *const PET_FRAMES[PET_CHAR_COUNT][PF_COUNT];")
    hdr.append("extern const char *const PET_CHAR_IDS[PET_CHAR_COUNT];  // 資料夾名稱 (SD 卡音效資料夾也用這個)")
    cpp.append("const char *const PET_CHAR_IDS[PET_CHAR_COUNT] = {" + ", ".join(f'"{c}"' for c in CHARACTERS) + "};\n")
    hdr.append("")
    table = []
    seen = {}  # 影格內容 → sprite 名稱 (貼圖角色 12 格幾乎都同一張，只存一份)
    for char in CHARACTERS:
        pet_anchors = anchors["pet"].get(char)
        if pet_anchors is None:
            sys.exit(f"anchors.json 缺少角色 {char} 的對齊點")
        row = []
        for name in REQUIRED_PET:
            path = os.path.join(ASSETS, "pets", char, name + ".png")
            check(path)
            a = pet_anchors.get(name)
            if a is None:
                sys.exit(f"anchors.json 缺少 {char} 影格 {name} 的對齊點")
            sym = f"PET_{ident(char)}_{ident(os.path.basename(name))}"
            data = convert(path)
            key = (data[0], data[1], tuple(data[2]), tuple(data[3]))
            if key not in seen:
                seen[key] = sym + "_spr"
                emit_sprite(cpp, sym + "_spr", *data)
            cpp.append(
                f"static const PetFrameAsset {sym} = {{&{seen[key]}, {{{{{a['head_top'][0]}, {a['head_top'][1]}}}, "
                f"{{{a['eyes'][0]}, {a['eyes'][1]}}}, {{{a['neck'][0]}, {a['neck'][1]}}}}}, "
                f"{'true' if a['eyes_open'] else 'false'}}};\n")
            row.append("&" + sym)
        table.append("  {" + ", ".join(row) + "}")
    cpp.append("const PetFrameAsset *const PET_FRAMES[PET_CHAR_COUNT][PF_COUNT] = {\n" + ",\n".join(table) + "\n};\n")

    # 裝扮圖層
    for name in REQUIRED_ACC:
        path = os.path.join(ASSETS, "accessories", name + ".png")
        check(path)
        m = anchors["accessories"].get(name)
        if m is None:
            sys.exit(f"anchors.json 缺少裝扮 {name} 的設定")
        sym = "ACC_" + ident(os.path.basename(name))
        emit_sprite(cpp, sym + "_spr", *convert(path))
        ox = m["dx"] - m["pivot"][0]
        oy = m["dy"] - m["pivot"][1]
        cpp.append(f"const AccessoryLayer {sym} = {{&{sym}_spr, {ANCHOR_IDS[m['anchor']]}, {ox}, {oy}, "
                   f"{'true' if m.get('eyes_open_only') else 'false'}}};\n")
        hdr.append(f"extern const AccessoryLayer {sym};")

    # UI
    for name in REQUIRED_UI:
        path = os.path.join(ASSETS, "ui", name + ".png")
        check(path)
        sym = "UI_" + ident(name)
        emit_sprite(cpp, sym, *convert(path))
        cpp.append("")
        hdr.append(f"extern const Sprite {sym};")

    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "Sprites.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(hdr) + "\n")
    with open(os.path.join(OUT_DIR, "Sprites.cpp"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(cpp) + "\n")
    print("wrote", os.path.relpath(OUT_DIR, ROOT))


if __name__ == "__main__":
    main()

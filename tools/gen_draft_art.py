"""
產生「初版」像素素材 (PNG, 透明背景) 到 assets/。

這些圖只是可替換的草稿：你可以用任何像素繪圖軟體 (Aseprite、Piskel...) 直接覆蓋
assets/ 底下的 PNG，再執行 tools/png2c.py 轉成韌體用的陣列即可，不需要動這支腳本。

注意：重新執行本腳本會覆蓋 assets/ 下同名的 PNG 與 anchors.json。

用法:  python tools/gen_draft_art.py
"""
import json
import math
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

# ---- 調色盤 (柔和配色) ----
C = {
    "outline": (24, 44, 82),
    "body": (118, 202, 236),
    "body_hi": (168, 226, 248),
    "body_sh": (78, 160, 210),
    "fin": (44, 104, 178),
    "fin_in": (246, 236, 190),
    "frill": (250, 244, 218),
    "frill_sh": (234, 224, 188),
    "frill_edge": (176, 156, 108),
    "eye": (22, 26, 48),
    "eye_hi": (255, 255, 255),
    "mouth": (196, 70, 98),
    "tongue": (246, 140, 160),
    "cheek": (255, 178, 200),
}

W = H = 64


class Canvas:
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        self.px = [[None] * w for _ in range(h)]

    def set(self, x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y][x]
        return None

    def image(self):
        im = Image.new("RGBA", (self.w, self.h), (0, 0, 0, 0))
        for y in range(self.h):
            for x in range(self.w):
                c = self.px[y][x]
                if c is not None:
                    im.putpixel((x, y), c + (255,))
        return im


# ---- 形狀遮罩 (set of (x,y)) ----
def ellipse(cx, cy, rx, ry):
    pts = set()
    for y in range(int(cy - ry - 1), int(cy + ry + 2)):
        for x in range(int(cx - rx - 1), int(cx + rx + 2)):
            if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0:
                pts.add((x, y))
    return pts


def polygon(points):
    pts = set()
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    n = len(points)
    for y in range(int(min(ys)) - 1, int(max(ys)) + 2):
        for x in range(int(min(xs)) - 1, int(max(xs)) + 2):
            px, py = x + 0.5, y + 0.5
            inside = False
            j = n - 1
            for i in range(n):
                xi, yi = points[i]
                xj, yj = points[j]
                if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
                    inside = not inside
                j = i
            if inside:
                pts.add((x, y))
    return pts


def thick_line(points, r):
    pts = set()
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for s in range(steps + 1):
            t = s / steps
            pts |= ellipse(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, r)
    return pts


def paint(cv, mask, fill, edge=None):
    """把遮罩畫上去；edge 不為 None 時，遮罩邊緣用 edge 色描邊。"""
    for (x, y) in mask:
        c = fill
        if edge is not None:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (x + dx, y + dy) not in mask:
                    c = edge
                    break
        cv.set(x, y, c)


def outline_outside(cv, color):
    """在所有不透明像素外圍補一圈描邊 (透明背景上的外框)。"""
    add = []
    for y in range(cv.h):
        for x in range(cv.w):
            if cv.px[y][x] is None:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if cv.get(x + dx, y + dy) is not None:
                        add.append((x, y))
                        break
    for x, y in add:
        cv.set(x, y, color)


def mirror_pts(points, cx=32):
    return [(2 * cx - x, y) for (x, y) in points]


# ---- 水伊布風格角色 ----
def draw_pet(dy=0, ear_droop=0, eyes="open", look=0, mouth="smile",
             body_ry=9.0, body_cy=50.0, tail="up", wag=0, cheeks=True):
    cv = Canvas()
    hx, hy = 32, 29 + dy           # 頭中心
    o = C["outline"]

    # 尾巴 (最底層)
    if tail == "up":
        tpts = [(40, 55), (47, 54), (52, 49), (54 + wag * 0.5, 43)]
        fin_base = (54 + wag * 0.5, 42)
    else:  # 睡覺：尾巴繞到前方地面
        tpts = [(42, 57), (49, 59), (54, 57)]
        fin_base = (55, 56)
    paint(cv, thick_line(tpts, 2.6), C["body"], o)
    fx, fy = fin_base
    if tail == "up":
        fin = polygon([(fx - 3, fy + 1), (fx - 7 + wag, fy - 10), (fx, fy - 5),
                       (fx + 6 + wag, fy - 11), (fx + 4, fy + 1)])
    else:
        fin = polygon([(fx - 2, fy - 2), (fx + 7, fy - 8), (fx + 4, fy),
                       (fx + 8, fy + 5), (fx - 1, fy + 3)])
    paint(cv, fin, C["fin"], o)

    # 魚鰭耳：從頭側往外上方展開，三根鰭條末端是尖角，鰭條之間是奶油色鰭膜
    d = ear_droop
    base = (21.5, 23.5 + dy)
    tips = [(5, 22 + dy + d), (2, 12 + dy + d * 1.2), (10, 4 + dy + d * 1.4)]  # 鰭條尖端
    notch = [(5.5, 17 + dy + d * 1.1), (7.5, 10 + dy + d * 1.3)]               # 尖端之間的內凹
    outer = [(24, 20 + dy), (16, 9 + dy + d * 0.8), tips[2], notch[1], tips[1], notch[0],
             tips[0], (12, 27 + dy + d * 0.4), (20, 28 + dy)]
    inner = [(base[0] - 1, base[1] - 0.5), (15, 12 + dy + d * 0.8), (9.5, 8 + dy + d * 1.3),
             (6, 13 + dy + d * 1.2), (6.5, 21 + dy + d), (13, 25 + dy + d * 0.4)]

    def ear(outer_pts, inner_pts, spine_lines):
        paint(cv, polygon(outer_pts), C["fin"], o)
        paint(cv, polygon(inner_pts), C["fin_in"])
        for (x, y) in spine_lines:
            if cv.get(x, y) == C["fin_in"]:
                cv.set(x, y, C["fin"])

    spines = set()
    for t in tips[1:]:
        spines |= thick_line([base, t], 0.5)
    ear(outer, inner, spines)
    ear(mirror_pts(outer), mirror_pts(inner), {(63 - x, y) for (x, y) in spines})

    # 身體
    body = ellipse(32, body_cy, 12, body_ry)
    paint(cv, body, C["body"], o)
    for (x, y) in body:  # 下緣陰影
        if y > body_cy + body_ry * 0.45 and cv.get(x, y) == C["body"]:
            cv.set(x, y, C["body_sh"])
    # 前腳
    paw_y = body_cy + body_ry - 1
    for px in (26, 38):
        paint(cv, ellipse(px, paw_y, 3.2, 2.2), C["body"], o)

    # 頸部鰭狀環 (扇貝邊)
    fy0 = 41 + dy
    frill = ellipse(32, fy0, 15, 4.5)
    for sx in (19, 24.5, 30, 35.5, 41, 46):
        frill |= ellipse(sx - 0.5, fy0 + 3, 3, 2.6)
    paint(cv, frill, C["frill"], C["frill_edge"])
    for (x, y) in frill:
        if y >= fy0 + 3 and cv.get(x, y) == C["frill"]:
            cv.set(x, y, C["frill_sh"])

    # 頭
    head = ellipse(hx, hy, 13, 12)
    paint(cv, head, C["body"], o)
    for (x, y) in ellipse(hx - 4, hy - 6, 4, 2):  # 頭頂高光
        if (x, y) in head and cv.get(x, y) == C["body"]:
            cv.set(x, y, C["body_hi"])
    # 額頭小鰭
    paint(cv, polygon([(29.5, 19 + dy), (34.5, 19 + dy), (32, 11 + dy)]), C["fin"], o)

    # 臉
    ey = hy + 1
    for ex in (hx - 6, hx + 6):
        if eyes == "open":
            paint(cv, ellipse(ex + look, ey, 2.6, 3.6), C["eye"])
            cv.set(ex + look - 1, ey - 2, C["eye_hi"])
            cv.set(ex + look - 1, ey - 1, C["eye_hi"])
            cv.set(ex + look + 1, ey + 2, C["eye_hi"])
        elif eyes == "closed":      # ︶ 放鬆閉眼
            for x in range(ex - 2, ex + 3):
                cv.set(x, ey + 1, C["eye"])
            cv.set(ex - 3, ey, C["eye"])
            cv.set(ex + 3, ey, C["eye"])
        elif eyes == "happy":       # ^ 開心
            for i in range(3):
                cv.set(ex - i, ey - 1 + i, C["eye"])
                cv.set(ex + i, ey - 1 + i, C["eye"])
        elif eyes == "squeeze":     # > < 用力
            s = 1 if ex < hx else -1
            for i in range(3):
                cv.set(ex - s * 2 + s * i, ey - 2 + i, C["eye"])
                cv.set(ex - s * 2 + s * i, ey + 2 - i, C["eye"])

    if cheeks:
        for cx in (hx - 10, hx + 9):
            cv.set(cx, ey + 4, C["cheek"])
            cv.set(cx + 1, ey + 4, C["cheek"])

    my = ey + 6
    if mouth == "smile":
        cv.set(hx - 2, my - 1, C["mouth"]); cv.set(hx - 1, my, C["mouth"])
        cv.set(hx, my, C["mouth"]); cv.set(hx + 1, my, C["mouth"]); cv.set(hx + 2, my - 1, C["mouth"])
    elif mouth == "open":
        paint(cv, ellipse(hx, my, 2.6, 2.0), C["tongue"], C["mouth"])
    elif mouth == "yawn":
        paint(cv, ellipse(hx, my + 0.5, 2.2, 2.8), C["tongue"], C["mouth"])
    elif mouth == "flat":
        for x in range(hx - 1, hx + 2):
            cv.set(x, my, C["mouth"])

    outline_outside(cv, o)
    anchors = {
        "head_top": [hx, hy - 12],   # 頭頂 (帽子底部中心對齊這點)
        "eyes": [hx, ey],            # 兩眼中心
        "neck": [hx, fy0],           # 頸環中心 (披風)
        "eyes_open": eyes == "open",
    }
    return cv, anchors


def draw_face_eyes(cv, hx, ey, eyes, look, gap, open_fn, dark):
    """共用的表情眼睛：open 交給 open_fn(ex) 畫，其餘用 dark 色畫線條"""
    for ex in (hx - gap, hx + gap):
        if eyes == "open":
            open_fn(ex + look)
        elif eyes == "closed":
            for x in range(ex - 2, ex + 3):
                cv.set(x, ey + 1, dark)
            cv.set(ex - 3, ey, dark)
            cv.set(ex + 3, ey, dark)
        elif eyes == "happy":
            for i in range(3):
                cv.set(ex - i, ey - 1 + i, dark)
                cv.set(ex + i, ey - 1 + i, dark)
        elif eyes == "squeeze":
            s = 1 if ex < hx else -1
            for i in range(3):
                cv.set(ex - s * 2 + s * i, ey - 2 + i, dark)
                cv.set(ex - s * 2 + s * i, ey + 2 - i, dark)


# ---- 水晶燈風格角色 (玻璃燈球 + 四隻燭臂 + 紫色火焰，飄浮) ----
L = {
    "outline": (30, 26, 48),
    "black": (44, 40, 56),
    "black_hi": (82, 76, 104),
    "glass": (200, 210, 244),
    "glass_hi": (238, 242, 255),
    "glass_sh": (164, 166, 220),
    "flame": (132, 112, 222),
    "flame_hi": (176, 160, 246),
    "flame_core": (110, 176, 246),
    "eye": (252, 214, 56),
    "eye_dk": (40, 34, 54),
    "cheek": (236, 180, 230),
}


def _flame(cv, cx, base_y, h, w, sway, dim=False):
    """一團往上的火焰 (底部藍色、上面紫色)，sway = 尖端左右偏移"""
    if h <= 0:
        return
    tip = (cx + sway, base_y - h)
    pts = [(cx - w, base_y), (cx - w * 0.9, base_y - h * 0.45), (cx - w * 0.3 + sway * 0.5, base_y - h * 0.8),
           tip, (cx + w * 0.5 + sway * 0.5, base_y - h * 0.7), (cx + w, base_y - h * 0.35), (cx + w, base_y)]
    m = polygon(pts)
    body, hi = (L["flame_hi"], L["flame"]) if not dim else (L["glass_sh"], L["flame"])
    paint(cv, m, body, hi)
    for (x, y) in m:  # 底部藍色核心
        if y >= base_y - h * 0.3 and cv.get(x, y) == body:
            cv.set(x, y, L["flame_core"])


def draw_chandelure(dy=0, ear_droop=0, eyes="open", look=0, mouth="smile",
                    body_ry=9.0, body_cy=50.0, tail="up", wag=0, cheeks=True):
    cv = Canvas()
    o = L["outline"]
    asleep = tail == "down"
    hx, hy = 32, 30 + dy
    d = ear_droop
    fh = 4 if asleep else 9          # 燭火高度：睡覺時變小
    sway = wag * 0.6

    # 頭頂大火焰 (最底層，從頂冠後面往上冒)
    _flame(cv, hx, hy - 9, 7 if asleep else 14, 4, (wag - 1) * 1.0, dim=asleep)

    # 燭臂：從燈球下方往外彎，再往上托住燭台
    arms = [  # (起點, 彎曲點, 燭台位置)
        ((hx - 6, hy + 9), (hx - 18, hy + 13), (hx - 25, hy + 5 + d * 0.6)),
        ((hx + 6, hy + 9), (hx + 18, hy + 13), (hx + 25, hy + 5 + d * 0.6)),
        ((hx - 8, hy + 6), (hx - 15, hy + 6), (hx - 17, hy - 3 + d * 0.8)),
        ((hx + 8, hy + 6), (hx + 15, hy + 6), (hx + 17, hy - 3 + d * 0.8)),
    ]
    for i, (a, b, c) in enumerate(arms):
        paint(cv, thick_line([a, b, (c[0], c[1] + 4)], 1.1), L["black"])
        cx, cy = c
        cup = ellipse(cx, cy + 1, 2.8, 3.2) | polygon([(cx - 3, cy - 1), (cx - 2, cy - 3.5), (cx - 0.5, cy - 1.5),
                                                      (cx + 0.5, cy - 3.5), (cx + 2, cy - 1.5), (cx + 3, cy - 3.5),
                                                      (cx + 3, cy)])
        paint(cv, cup, L["black"])
        _flame(cv, cx, cy - 2, fh - (i // 2) * 1, 2.4, sway * (1 if i % 2 else -1), dim=asleep)
    # 手臂上的小捲 (裝飾)
    for side in (-1, 1):
        for (x, y) in ((hx + side * 12, hy + 12), (hx + side * 13, hy + 11)):
            cv.set(x, y, L["black_hi"])

    # 下方的燈座 + 尖錐
    base_y = hy + 10
    paint(cv, ellipse(hx, base_y, 7, 2.6), L["black"])
    paint(cv, polygon([(hx - 4.5, base_y + 1), (hx + 4.5, base_y + 1), (hx, base_y + 11)]), L["black"])
    cv.set(hx - 2, base_y, L["black_hi"]); cv.set(hx - 1, base_y, L["black_hi"])

    # 玻璃燈球
    globe = ellipse(hx, hy, 11.5, 10)
    paint(cv, globe, L["glass"], o)
    for (x, y) in globe:
        if cv.get(x, y) != L["glass"]:
            continue
        if y > hy + 3:
            cv.set(x, y, L["glass_sh"])
    for (x, y) in ellipse(hx - 5, hy - 5, 3, 1.6):  # 反光
        if cv.get(x, y) in (L["glass"],):
            cv.set(x, y, L["glass_hi"])
    # 燈球的黑色骨架 (直線)
    for rx in (-6, 0, 6):
        for y in range(int(hy - 9), int(hy + 10)):
            t = (y - hy) / 10.0
            x = int(round(hx + rx * math.sqrt(max(0.0, 1 - t * t))))
            if cv.get(x, y) in (L["glass"], L["glass_sh"], L["glass_hi"]):
                cv.set(x, y, L["black_hi"])

    # 頂冠 (尖尖的黑色王冠)
    crown_y = hy - 9
    crown = ellipse(hx, crown_y + 0.5, 6, 1.8)
    for k in (-4, -1.5, 1.5, 4):
        crown |= polygon([(hx + k - 1.5, crown_y), (hx + k, crown_y - 3.5), (hx + k + 1.5, crown_y)])
    paint(cv, crown, L["black"])

    # 眼睛：黃色圓眼 (在玻璃裡)
    ey = hy + 1

    def open_eye(ex):
        paint(cv, ellipse(ex, ey, 2.6, 2.6), L["eye"], L["eye_dk"])
        cv.set(ex - 1, ey - 1, L["glass_hi"])

    for ex in (hx - 5, hx + 5):  # 先把眼睛那一塊清成玻璃色，避免骨架穿過眼睛
        for (x, y) in ellipse(ex + look, ey, 3.2, 3.2):
            if cv.get(x, y) == L["black_hi"]:
                cv.set(x, y, L["glass"])
    draw_face_eyes(cv, hx, ey, eyes, look, 5, open_eye, L["eye_dk"])

    if cheeks:
        for cx in (hx - 9, hx + 8):
            cv.set(cx, ey + 3, L["cheek"]); cv.set(cx + 1, ey + 3, L["cheek"])

    # 沒有嘴巴：表情只靠眼睛 (mouth 參數不使用)
    outline_outside(cv, o)
    anchors = {
        "head_top": [hx, crown_y - 1],
        "eyes": [hx, ey],
        "neck": [hx, base_y],
        "eyes_open": eyes == "open",
    }
    return cv, anchors


# ---- 黑影風格角色 (白色飄髮遮半臉、紅色尖刺項圈、一隻青綠色眼睛、破破的下襬，飄浮) ----
D = {
    "outline": (18, 16, 26),
    "black": (58, 56, 70),
    "black_hi": (92, 90, 106),
    "shadow": (38, 36, 50),
    "white": (238, 238, 242),
    "white_sh": (192, 192, 206),
    "red": (216, 76, 62),
    "red_dk": (150, 42, 40),
    "eye": (70, 214, 214),
    "eye_dk": (16, 70, 84),
    "eye_hi": (255, 255, 255),
}


def jagged(points_top, base_y, teeth, depth):
    """上緣沿 points_top，下緣是 teeth 個往下的尖刺 (到 base_y + depth)"""
    x0, x1 = points_top[0][0], points_top[-1][0]
    bottom = []
    for i in range(teeth * 2 + 1):
        x = x1 - (x1 - x0) * i / (teeth * 2)
        bottom.append((x, base_y + (depth if i % 2 == 1 else 0)))
    return polygon(points_top + bottom)


def draw_darkrai(dy=0, ear_droop=0, eyes="open", look=0, mouth="smile",
                 body_ry=9.0, body_cy=50.0, tail="up", wag=0, cheeks=True):
    cv = Canvas()
    o = D["outline"]
    asleep = tail == "down"
    hx, hy = 32, 22 + dy
    d = ear_droop
    sway = wag * 0.6
    stretch = (body_ry - 9.0) * 0.6   # 伸懶腰時身體拉長、睡覺時縮短

    # 身後的暗影 (左右兩片鋸齒狀，最底層)
    for side in (-1, 1):
        sx = hx + side * 12
        pts = [(sx, hy + 4), (sx + side * 6, hy - 6 + d * 0.8 + sway * side), (sx + side * 6, hy),
               (sx + side * 11, hy - 3 + d + sway * side), (sx + side * 9, hy + 5),
               (sx + side * 13, hy + 6 + d * 0.6), (sx + side * 8, hy + 11), (sx + side * 10, hy + 15),
               (sx + side * 3, hy + 14), (sx - side * 1, hy + 9)]
        paint(cv, polygon(pts), D["shadow"])

    # 身體：肩膀 → 細腰 → 往下張開、下緣破破的下襬 (飄在空中，沒有腳)
    sh_y, waist_y = hy + 8, hy + 16 + stretch * 0.5
    skirt_top = waist_y + 1
    skirt_y = hy + 27 + stretch
    torso = polygon([(hx - 8, sh_y), (hx + 8, sh_y), (hx + 4, waist_y), (hx - 4, waist_y)])
    skirt = polygon([(hx - 4, skirt_top), (hx + 4, skirt_top), (hx + 11, skirt_y - 2), (hx - 11, skirt_y - 2)])
    skirt |= jagged([(hx - 11, skirt_y - 2), (hx + 11, skirt_y - 2)], skirt_y - 2, 5, 3)
    paint(cv, torso | skirt, D["black"])
    for (x, y) in skirt:  # 下襬的陰影
        if y > skirt_y - 1 and cv.get(x, y) == D["black"]:
            cv.set(x, y, D["shadow"])
    for y in range(int(sh_y + 1), int(waist_y)):  # 身體亮邊
        cv.set(hx - 5 + (y - sh_y) * 0.3, y, D["black_hi"])

    # 細手臂 + 爪子
    for side in (-1, 1):
        arm = [(hx + side * 7, sh_y + 1), (hx + side * 13, sh_y + 5 - d * 0.3),
               (hx + side * 15, sh_y + 11 + d * 0.5)]
        paint(cv, thick_line(arm, 1.2), D["black"])
        cx, cy = arm[-1]
        for k in (-1, 0, 1):
            cv.set(cx + k, cy + 2, D["black"])
            cv.set(cx + k * 1.5, cy + 3, D["black_hi"])

    # 紅色尖刺項圈 (往下的尖牙)
    cy_ = hy + 7
    collar = ellipse(hx, cy_, 9.5, 3)
    for k in (-6, -2, 2, 6):
        collar |= polygon([(hx + k - 2, cy_ + 1), (hx + k + 2, cy_ + 1), (hx + k, cy_ + 5)])
    for k in (-8, 8):  # 兩側往上翹的尖角
        collar |= polygon([(hx + k - 2, cy_), (hx + k * 1.25, cy_ - 5), (hx + k + 2 * (1 if k > 0 else -1), cy_ - 1)])
    paint(cv, collar, D["red"], D["red_dk"])

    # 頭 (小小的黑色頭)
    head = ellipse(hx, hy, 7.5, 7)
    paint(cv, head, D["black"], o)
    for (x, y) in ellipse(hx + 3, hy - 3, 2.5, 1.5):
        if cv.get(x, y) == D["black"]:
            cv.set(x, y, D["black_hi"])

    # 白色飄髮：從頭的左半邊往右上方大大地甩出去，遮住左眼
    tip_y = hy - 19 + d * 1.6
    plume = polygon([(hx - 8, hy + 4), (hx - 9, hy - 4), (hx - 5, hy - 11), (hx + 2, hy - 16 + d * 0.6),
                     (hx + 11, tip_y + 1), (hx + 21 + sway, tip_y - 1), (hx + 14 + sway * 0.5, tip_y + 5),
                     (hx + 6, hy - 9 + d * 0.5), (hx + 1, hy - 4), (hx - 1, hy + 2), (hx - 3, hy + 5)])
    paint(cv, plume, D["white"], o)
    for (x, y) in plume:  # 下緣陰影
        if cv.get(x, y) == D["white"] and (x + 1, y + 1) not in plume:
            cv.set(x, y, D["white_sh"])

    # 一隻看得到的眼睛 (右邊)
    ex, ey = hx + 3 + look, hy + 1
    dk = D["eye_dk"]
    if eyes == "open":
        paint(cv, ellipse(ex, ey, 2.4, 2.4), D["eye"], dk)
        cv.set(ex - 1, ey - 1, D["eye_hi"])
        cv.set(ex + 1, ey, dk)
    elif eyes == "closed":
        for x in range(ex - 2, ex + 3):
            cv.set(x, ey + 1, D["eye"])
    elif eyes == "happy":
        for i in range(3):
            cv.set(ex - i, ey - 1 + i, D["eye"])
            cv.set(ex + i, ey - 1 + i, D["eye"])
    elif eyes == "squeeze":
        for i in range(3):
            cv.set(ex + 2 - i, ey - 2 + i, D["eye"])
            cv.set(ex + 2 - i, ey + 2 - i, D["eye"])
    # 沒有嘴巴

    outline_outside(cv, o)
    anchors = {
        "head_top": [hx, hy - 7],
        "eyes": [hx, ey],
        "neck": [hx, cy_],
        "eyes_open": eyes == "open",
    }
    return cv, anchors


# 角色清單：(資料夾名稱, 繪圖函式)。順序 = 韌體裡的角色編號，新角色請加在最後面
CHARACTERS = [("vaporeon", draw_pet), ("chandelure", draw_chandelure), ("darkrai", draw_darkrai)]


# ---- 裝扮 (每個 = 一張圖 + 掛點 pivot + 對齊錨點) ----
def acc_bow():
    cv = Canvas(15, 9)
    red, dk = (238, 92, 128), (160, 40, 80)
    paint(cv, polygon([(0, 0), (7, 3.5), (0, 9)]), red, dk)
    paint(cv, polygon([(15, 0), (8, 3.5), (15, 9)]), red, dk)
    paint(cv, ellipse(7.5, 4.5, 2.2, 2.2), (255, 160, 190), dk)
    return cv, {"anchor": "head_top", "pivot": [7, 7], "dx": 9, "dy": 4}


def acc_crown():
    cv = Canvas(17, 11)
    g, dk, gem = (255, 206, 72), (170, 112, 20), (232, 64, 96)
    paint(cv, polygon([(1, 11), (1, 2), (5, 6), (8.5, 0), (12, 6), (16, 2), (16, 11)]), g, dk)
    cv.set(8, 7, gem); cv.set(4, 8, (90, 200, 255)); cv.set(12, 8, (90, 200, 255))
    return cv, {"anchor": "head_top", "pivot": [8, 10], "dx": 0, "dy": 2}


def acc_glasses():
    cv = Canvas(25, 10)
    fr = (60, 50, 50)
    for cx in (6, 18):
        ring = ellipse(cx + 0.5, 5, 5, 4.6) - ellipse(cx + 0.5, 5, 3.9, 3.5)
        paint(cv, ring, fr)
        cv.set(cx - 2, 3, (220, 240, 255))
    for x in (11, 12, 13):
        cv.set(x, 4, fr)
    return cv, {"anchor": "eyes", "pivot": [12, 5], "dx": 0, "dy": 0}


def acc_leaf_hat():
    cv = Canvas(20, 14)
    gr, dk, vein = (120, 200, 96), (44, 110, 50), (70, 150, 64)
    leaf = polygon([(1, 13), (4, 5), (10, 1), (16, 4), (19, 13)])
    paint(cv, leaf, gr, dk)
    for y in range(4, 13):
        cv.set(10, y, vein)
    for i in range(3):
        cv.set(9 - i, 8 + i, vein); cv.set(11 + i, 8 + i, vein)
    paint(cv, thick_line([(10, 1), (12, 0)], 0.6), (110, 80, 40))
    return cv, {"anchor": "head_top", "pivot": [10, 12], "dx": 0, "dy": 3}


def _cone_hat(body_col, dk, trim, pom, stripes=None):
    cv = Canvas(24, 18)
    cone = polygon([(3, 14), (9, 4), (15, 1), (21, 3), (20, 6), (17, 5), (19, 14)])
    paint(cv, cone, body_col, dk)
    if stripes:
        for (x, y) in cone:
            if (x + y) % 6 < 2 and cv.get(x, y) == body_col:
                cv.set(x, y, stripes)
    paint(cv, ellipse(11.5, 15, 10, 2.6), trim, (170, 170, 190))
    paint(cv, ellipse(21, 7, 2.6, 2.6), pom, (170, 170, 190))
    return cv


def acc_santa_hat():
    cv = _cone_hat((222, 56, 64), (140, 24, 36), (250, 250, 250), (250, 250, 250))
    return cv, {"anchor": "head_top", "pivot": [11, 16], "dx": 0, "dy": 3}


def acc_sleep_hat():
    cv = _cone_hat((112, 108, 200), (56, 50, 130), (236, 232, 255), (255, 226, 120),
                   stripes=(150, 146, 226))
    return cv, {"anchor": "head_top", "pivot": [11, 16], "dx": 0, "dy": 3}


def acc_cape():
    back = Canvas(54, 19)
    red, dk, lining = (200, 58, 78), (120, 26, 44), (240, 196, 92)
    cape = polygon([(13, 0), (41, 0), (53, 17), (46, 15), (40, 18), (27, 16), (14, 18), (8, 15), (1, 17)])
    paint(back, cape, red, dk)
    for (x, y) in cape:   # 披風下緣金色滾邊
        if y >= 14 and back.get(x, y) == red:
            back.set(x, y, lining)
    front = Canvas(5, 4)
    paint(front, ellipse(2.5, 2, 2.4, 1.9), (255, 206, 72), (170, 112, 20))
    return (back, {"anchor": "neck", "pivot": [27, 0], "dx": 0, "dy": -1, "layer": "back"},
            front, {"anchor": "neck", "pivot": [2, 1], "dx": 0, "dy": 2})


def acc_star_eyes():
    cv = Canvas(19, 7)
    y, dk = (255, 220, 72), (190, 120, 20)
    for cx in (3, 15):
        star = polygon([(cx, 0), (cx + 1.2, 2.3), (cx + 3.5, 2.5), (cx + 1.8, 4), (cx + 2.5, 7),
                        (cx + 0.5, 5.3), (cx - 1.5, 7), (cx - 0.8, 4), (cx - 2.5, 2.5), (cx - 0.2, 2.3)])
        paint(cv, star, y, dk)
    return cv, {"anchor": "eyes", "pivot": [9, 3], "dx": 0, "dy": 0, "eyes_open_only": True}


# ---- UI ----
def ui_wifi(on):
    cv = Canvas(13, 10)
    c = (140, 230, 170) if on else (120, 120, 140)
    for r, y0 in ((6, 8), (4, 8), (2, 8)):
        for a in range(-60, 61, 6):
            x = 6 + r * math.sin(math.radians(a))
            y = y0 - r * math.cos(math.radians(a))
            cv.set(x, y, c)
    cv.set(6, 8, c); cv.set(6, 9, c)
    if not on:
        for i in range(7):
            cv.set(3 + i, 2 + i, (236, 96, 110))
    return cv


def ui_sparkle(size):
    cv = Canvas(9, 9)
    col, core = (255, 236, 140), (255, 255, 255)
    for i in range(-size, size + 1):
        cv.set(4 + i, 4, col)
        cv.set(4, 4 + i, col)
    if size >= 3:
        for i in (-1, 1):
            cv.set(4 + i, 4 + i, col); cv.set(4 + i, 4 - i, col)
    cv.set(4, 4, core)
    return cv


def ui_z():
    cv = Canvas(6, 6)
    col = (210, 220, 255)
    for x in range(6):
        cv.set(x, 0, col); cv.set(x, 5, col)
    for i in range(1, 5):
        cv.set(5 - i, i, col)
    return cv


def ui_shadow():
    cv = Canvas(34, 6)
    for (x, y) in ellipse(17, 3, 16.5, 2.8):
        cv.set(x, y, (66, 128, 72))   # 草地上的影子
    return cv


def ui_snack():
    """點心：一塊餅乾 (午餐 / 點心時間)"""
    cv = Canvas(12, 12)
    dough, dk, chip = (232, 176, 104), (150, 96, 44), (96, 56, 30)
    paint(cv, ellipse(6, 6, 5.6, 5.6), dough, dk)
    for (x, y) in ((4, 4), (7, 3), (8, 7), (4, 8), (6, 6)):
        cv.set(x, y, chip)
    cv.set(3, 3, (250, 214, 150))
    return cv


def ui_sweat():
    """汗滴 (低電量)"""
    cv = Canvas(5, 7)
    paint(cv, polygon([(2.5, 0), (5, 4.5), (0, 4.5)]) | ellipse(2.5, 4.5, 2.5, 2.4),
          (150, 214, 255), (60, 120, 200))
    cv.set(1, 4, (255, 255, 255))
    return cv


def ui_bolt():
    """閃電 (充電中，畫在電池圖示上)"""
    cv = Canvas(7, 9)
    col = (255, 226, 80)
    paint(cv, polygon([(4, 0), (0, 5), (3, 5), (2, 9), (7, 3), (4, 3), (5, 0)]), col)
    return cv


def _sun(cv, cx, cy, r):
    sun, rays = (255, 206, 72), (255, 230, 140)
    paint(cv, ellipse(cx, cy, r, r), sun)
    for a in range(0, 360, 45):
        cv.set(cx - 0.5 + (r + 1.6) * math.cos(math.radians(a)), cy - 0.5 + (r + 1.6) * math.sin(math.radians(a)), rays)


def _cloud(cv, ox, oy, col=(236, 240, 250), edge=(150, 160, 190)):
    m = ellipse(ox + 4, oy + 5, 3.5, 2.6) | ellipse(ox + 7.5, oy + 3.5, 3.6, 3.2) | ellipse(ox + 10, oy + 5.2, 3, 2.4)
    paint(cv, m, col, edge)


def ui_weather(kind):
    """天氣圖示 15 x 12"""
    cv = Canvas(15, 12)
    if kind == "sun":
        _sun(cv, 7.5, 6, 3.6)
    elif kind == "partly":
        _sun(cv, 5, 4.5, 3)
        _cloud(cv, 1.5, 3)
    elif kind == "cloud":
        _cloud(cv, 0.5, 1.5, (200, 208, 226), (120, 130, 160))
    elif kind in ("rain", "thunder", "snow"):
        _cloud(cv, 0.5, 0, (190, 198, 220), (110, 120, 150))
        if kind == "rain":
            for x in (4, 8, 12):
                cv.set(x, 9, (110, 190, 255)); cv.set(x - 1, 10, (110, 190, 255))
        elif kind == "thunder":
            paint(cv, polygon([(8, 7), (5.5, 10.5), (7.5, 10.5), (6.5, 12), (10, 9), (8, 9), (9, 7)]), (255, 226, 80))
        else:
            for x in (4, 8, 12):
                cv.set(x, 10, (255, 255, 255)); cv.set(x - 1, 10, (220, 230, 255))
    elif kind == "fog":
        for y in (3, 6, 9):
            for x in range(1 + (y % 2), 14 - (y % 2)):
                cv.set(x, y, (190, 196, 214))
    return cv


MEADOW_PALETTES = {
    # 天頂 (= Theme::BG，跟時間區接起來) → 地平線、遠山兩層、草地三色
    "night": dict(horizon=(36, 48, 74), far=(44, 72, 84), near=(52, 96, 86),
                  grass=((132, 204, 112), (104, 180, 96), (78, 148, 80)), tuft=(150, 222, 124)),
    "dawn": dict(horizon=(246, 196, 176), far=(118, 120, 150), near=(92, 130, 118),
                 grass=((140, 206, 120), (112, 182, 104), (86, 152, 88)), tuft=(160, 222, 132)),
    "day": dict(horizon=(156, 214, 242), far=(110, 168, 150), near=(88, 150, 108),
                grass=((150, 222, 120), (118, 198, 100), (90, 166, 84)), tuft=(176, 236, 140)),
    "dusk": dict(horizon=(252, 158, 96), far=(122, 88, 110), near=(84, 96, 92),
                 grass=((128, 190, 104), (100, 164, 90), (76, 134, 76)), tuft=(146, 206, 116)),
}
SKY_TOP = (36, 48, 74)


def _lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def ui_meadow(period="night"):
    """草地背景 120 x 68 (螢幕上放大 2 倍 = 240 x 136，橫跨整個螢幕寬)
    天空由上方的畫面底色漸層到地平線色，避免跟時間區之間出現分界線。寵物腳底約在 y=61~63"""
    P = MEADOW_PALETTES[period]
    W_, H_ = 120, 68
    cv = Canvas(W_, H_)
    for y in range(H_):
        t = min(1.0, y / 38.0) ** 1.4
        c = _lerp(SKY_TOP, P["horizon"], t)
        for x in range(W_):
            cv.set(x, y, c)
    if period == "night":
        for (x, y, c) in ((8, 4, (230, 236, 255)), (27, 12, (180, 190, 230)), (48, 3, (230, 236, 255)),
                          (70, 9, (180, 190, 230)), (93, 5, (230, 236, 255)), (110, 14, (180, 190, 230)),
                          (16, 20, (150, 160, 200)), (102, 24, (150, 160, 200))):
            cv.set(x, y, c)
    elif period == "dawn":   # 剛升起的太陽 (畫在山後面)
        paint(cv, ellipse(105, 37, 6, 6), (255, 236, 170))
    elif period == "dusk":   # 夕陽
        paint(cv, ellipse(26, 37, 7, 7), (255, 196, 110))
    hill_far, hill_near = P["far"], P["near"]
    for x in range(W_):
        h1 = 38 + 5 * math.sin(x / 13.0) + 3 * math.sin(x / 5.3 + 1)
        h2 = 44 + 3 * math.sin(x / 9.0 + 2) + 2 * math.sin(x / 4.1)
        for y in range(int(h1), H_):
            cv.set(x, y, hill_far)
        for y in range(int(h2), H_):
            cv.set(x, y, hill_near)
    grass_top, grass, grass_dk = P["grass"]
    for x in range(W_):
        top = 50 + int(round(math.sin(x * math.pi / 30.0) * 1.2))  # 週期 60，可無縫捲動
        for y in range(top, H_):
            c = grass
            if y == top:
                c = grass_top
            elif y > 60 and (x * 7 + y * 3) % 12 == 0:
                c = grass_dk
            elif (x * 5 + y * 13) % 24 == 0:
                c = grass_dk
            cv.set(x, y, c)
    for x0 in (3, 17, 34, 52, 79, 95, 112, 63):
        base = 50 + int(round(math.sin(x0 * math.pi / 30.0) * 1.2))
        for dx, h in ((0, 2), (1, 3), (2, 2), (3, 1)):
            for k in range(h):
                cv.set(x0 + dx, base - 1 - k, P["tuft"])
    flower_w, flower_p, flower_y = (250, 246, 230), (255, 170, 200), (255, 220, 110)
    for (x, y, c) in ((10, 56, flower_w), (25, 62, flower_p), (40, 54, flower_y), (86, 57, flower_p),
                      (101, 63, flower_w), (114, 55, flower_y), (6, 65, flower_y), (73, 64, flower_w)):
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            cv.set(x + dx, y + dy, c if c != flower_y else (255, 240, 170))
        cv.set(x, y, (255, 200, 80))
    return cv


# ---- 天氣圖層 (疊在草地背景天空上) ----
def ov_sun():
    cv = Canvas(15, 15)
    paint(cv, ellipse(7.5, 7.5, 4.6, 4.6), (255, 214, 80), (240, 170, 50))
    for a in range(0, 360, 45):
        r = 6.6
        cv.set(7 + r * math.cos(math.radians(a)), 7 + r * math.sin(math.radians(a)), (255, 230, 140))
    return cv


def ov_moon():
    cv = Canvas(10, 10)
    m = ellipse(5, 5, 4.5, 4.5) - ellipse(7, 3.6, 4, 4)
    paint(cv, m, (250, 238, 180))
    return cv


def ov_cloud(col, edge):
    cv = Canvas(24, 10)
    m = (ellipse(6, 6.5, 5, 3.2) | ellipse(11.5, 4.5, 5.5, 4.2) | ellipse(17, 6, 5, 3.4) |
         polygon([(2, 8), (22, 8), (22, 10), (2, 10)]))
    m = {(x, y) for (x, y) in m if y < 10}
    paint(cv, m, col, edge)
    return cv


def ov_fog():
    cv = Canvas(120, 14)
    col = (206, 212, 226)
    for y in range(14):
        for x in range(120):
            if (y % 4 == 1 and (x + y * 3) % 3 != 0) or (y % 4 == 3 and (x * 2 + y) % 5 == 0):
                cv.set(x, y, col)
    return cv


def ui_butterfly(frame):
    """蝴蝶 (散步模式)：frame 0 翅膀張開，1 翅膀收起"""
    cv = Canvas(7, 5)
    wing, dk, body = (255, 196, 120), (220, 120, 70), (70, 50, 60)
    if frame == 0:
        for (x, y) in ((0, 0), (1, 0), (0, 1), (1, 1), (2, 1), (1, 2), (5, 0), (6, 0), (4, 1), (5, 1), (6, 1), (5, 2)):
            cv.set(x, y, wing)
        for (x, y) in ((1, 3), (0, 3), (5, 3), (6, 3)):
            cv.set(x, y, dk)
    else:
        for (x, y) in ((2, 0), (2, 1), (4, 0), (4, 1), (2, 2), (4, 2)):
            cv.set(x, y, wing)
        cv.set(2, 3, dk)
        cv.set(4, 3, dk)
    for y in range(1, 5):
        cv.set(3, y, body)
    return cv


def ui_walk_flower():
    """路邊的大花 (散步模式)"""
    cv = Canvas(7, 10)
    petal, dk, mid, stem = (255, 170, 200), (220, 110, 150), (255, 220, 110), (70, 150, 80)
    for y in range(4, 10):
        cv.set(3, y, stem)
    cv.set(2, 7, stem)
    cv.set(1, 6, stem)
    cv.set(4, 8, stem)
    cv.set(5, 7, stem)
    paint(cv, ellipse(3, 2.5, 3, 2.5), petal, dk)
    cv.set(3, 2, mid)
    cv.set(3, 3, mid)
    return cv


def ui_hand():
    """摸頭的手 (手掌朝下，從右上方伸過來)"""
    cv = Canvas(16, 9)
    skin, edge, cuff = (255, 226, 200), (196, 140, 112), (140, 230, 210)
    palm = ellipse(7, 5, 6.5, 3.6)
    fingers = set()
    for i, fx in enumerate((2, 5, 8, 11)):
        fingers |= ellipse(fx, 6.6, 1.5, 2.2)
    paint(cv, palm | fingers, skin, edge)
    for x in range(12, 16):          # 袖口
        for y in range(2, 7):
            cv.set(x, y, cuff)
    for x in (4, 7, 10):             # 指縫
        cv.set(x, 7, edge)
    return cv


def ui_heart():
    """愛心 (被摸頭時冒出來)"""
    cv = Canvas(7, 6)
    col, dk, hi = (255, 120, 160), (210, 70, 110), (255, 210, 225)
    pts = [(1, 0), (2, 0), (4, 0), (5, 0), (0, 1), (1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (6, 1),
           (0, 2), (1, 2), (2, 2), (3, 2), (4, 2), (5, 2), (6, 2), (1, 3), (2, 3), (3, 3), (4, 3), (5, 3),
           (2, 4), (3, 4), (4, 4), (3, 5)]
    for (x, y) in pts:
        cv.set(x, y, col)
    for (x, y) in ((0, 2), (1, 3), (2, 4), (3, 5), (4, 4), (5, 3), (6, 2)):
        cv.set(x, y, dk)
    cv.set(1, 1, hi)
    return cv


def ui_fan():
    """手持扇子 (熱的時候)"""
    cv = Canvas(11, 12)
    paint(cv, polygon([(5.5, 11), (0, 3), (2, 0.5), (5.5, 0), (9, 0.5), (11, 3)]), (255, 238, 196), (210, 120, 90))
    for i in range(6):  # 扇骨
        cv.set(3 + i * 0.1, 3 + i, (230, 150, 120))
        cv.set(8 - i * 0.1, 3 + i, (230, 150, 120))
    paint(cv, polygon([(5, 9), (6, 9), (6, 12), (5, 12)]), (150, 96, 60))
    return cv


def ui_scarf():
    """圍巾 (冷的時候)，畫在頸環上"""
    cv = Canvas(28, 12)
    red, dk, stripe = (232, 80, 96), (150, 36, 56), (255, 236, 236)
    band = ellipse(14, 3.5, 13.5, 3.4)
    tail = polygon([(17, 4), (23, 4), (22, 12), (17, 11)])
    paint(cv, band | tail, red, dk)
    for (x, y) in band | tail:
        if cv.get(x, y) == red and (x // 3) % 2 == 0:
            cv.set(x, y, stripe)
    return cv


def save(cv, rel):
    path = os.path.join(ASSETS, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    cv.image().save(path)


def main():
    frames = {
        # 動畫/影格: draw_pet 參數
        "idle/idle_0": dict(),
        "idle/idle_1": dict(wag=2),
        "blink/blink_0": dict(eyes="closed"),
        "happy/happy_0": dict(eyes="happy", mouth="open"),
        "happy/happy_1": dict(eyes="happy", mouth="open", wag=2, dy=-1, body_cy=49.5),
        "sleep/sleep_0": dict(dy=6, ear_droop=6, eyes="closed", mouth="flat", body_ry=7,
                              body_cy=53, tail="down", cheeks=False),
        "sleep/sleep_1": dict(dy=7, ear_droop=6, eyes="closed", mouth="flat", body_ry=6.5,
                              body_cy=53.5, tail="down", cheeks=False),
        "wake/wake_0": dict(dy=4, ear_droop=4, eyes="closed", mouth="yawn", body_ry=8,
                            body_cy=52, tail="down"),
        "wake/wake_1": dict(dy=-2, ear_droop=-2, eyes="squeeze", mouth="yawn", body_ry=10,
                            body_cy=49),
        "wake/wake_2": dict(eyes="open", mouth="smile"),
        "look/look_left": dict(look=-1),
        "look/look_right": dict(look=1),
    }
    anchors = {"pet": {}, "accessories": {}}
    for char, draw in CHARACTERS:
        anchors["pet"][char] = {}
        for name, kw in frames.items():
            cv, a = draw(**kw)
            save(cv, f"pets/{char}/{name}.png")
            anchors["pet"][char][name] = a

    accs = {
        "bow": acc_bow, "crown": acc_crown, "glasses": acc_glasses, "leaf_hat": acc_leaf_hat,
        "santa_hat": acc_santa_hat, "sleep_hat": acc_sleep_hat, "star_eyes": acc_star_eyes,
    }
    for name, fn in accs.items():
        cv, meta = fn()
        save(cv, f"accessories/{name}/{name}.png")
        anchors["accessories"][f"{name}/{name}"] = meta
    back, mb, front, mf = acc_cape()
    save(back, "accessories/cape/cape_back.png")
    save(front, "accessories/cape/cape_front.png")
    anchors["accessories"]["cape/cape_back"] = mb
    anchors["accessories"]["cape/cape_front"] = mf

    save(ui_wifi(True), "ui/wifi_on.png")
    save(ui_wifi(False), "ui/wifi_off.png")
    for i, s in enumerate((1, 2, 3)):
        save(ui_sparkle(s), f"ui/sparkle_{i}.png")
    save(ui_z(), "ui/zzz.png")
    save(ui_shadow(), "ui/shadow.png")
    save(ui_snack(), "ui/snack.png")
    save(ui_sweat(), "ui/sweat.png")
    save(ui_bolt(), "ui/bolt.png")
    for period in ("night", "dawn", "day", "dusk"):
        save(ui_meadow(period), f"ui/bg_{period}.png")
    save(ov_sun(), "ui/ov_sun.png")
    save(ov_moon(), "ui/ov_moon.png")
    save(ov_cloud((250, 252, 255), (196, 206, 224)), "ui/ov_cloud_white.png")
    save(ov_cloud((196, 202, 216), (150, 158, 178)), "ui/ov_cloud_grey.png")
    save(ov_cloud((120, 126, 146), (88, 94, 114)), "ui/ov_cloud_dark.png")
    save(ov_cloud((64, 78, 108), (50, 62, 90)), "ui/ov_cloud_night.png")
    save(ov_fog(), "ui/ov_fog.png")
    save(ui_fan(), "ui/fan.png")
    save(ui_scarf(), "ui/scarf.png")
    save(ui_butterfly(0), "ui/butterfly_0.png")
    save(ui_butterfly(1), "ui/butterfly_1.png")
    save(ui_walk_flower(), "ui/walk_flower.png")
    save(ui_hand(), "ui/hand.png")
    save(ui_heart(), "ui/heart.png")
    for k in ("sun", "partly", "cloud", "rain", "thunder", "snow", "fog"):
        save(ui_weather(k), f"ui/weather_{k}.png")

    with open(os.path.join(ASSETS, "anchors.json"), "w", encoding="utf-8") as f:
        json.dump(anchors, f, indent=2, ensure_ascii=False)
    print("draft art written to", ASSETS)


if __name__ == "__main__":
    main()

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

import sdcard  # 貼圖角色的去背 / 縮圖 (同一個 tools/ 資料夾)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

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


# 內建角色：(資料夾名稱, Q 版原圖)。順序 = 韌體裡的角色編號 (要跟 png2c.py 的 CHARACTERS 一致)
# 做法跟 tools/sdcard.py sticker 一樣：去背 → 縮成 64x64 → 12 格都用同一張 (睡覺往下沉 1 px)，
# png2c.py 會把相同的影格合併成一份，所以每個內建貼圖角色只佔約 8 KB。
# 其他角色請做成 SD 卡角色 (tools/sdcard.py sticker)，不佔韌體空間。
CHARACTERS = [("ayame", "example7.png")]


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
    anchors = {"pet": {}, "accessories": {}}
    for char, src in CHARACTERS:
        anchors["pet"][char] = {}
        for name, (im, a) in zip(sdcard.FRAMES, sdcard.sticker_frames(os.path.join(ROOT, src))):
            path = os.path.join(ASSETS, "pets", char, name + ".png")
            os.makedirs(os.path.dirname(path), exist_ok=True)
            im.save(path)
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

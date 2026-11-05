"""
準備 SD 卡內容 (輸出到 sdcard/deskpet/...，再整個複製到 SD 卡根目錄；或用 --out 直接指定 SD 卡磁碟機)

用法:
  角色   python tools/sdcard.py char <資料夾> [--name 顯示名稱] [--voice 0|1|2|3]
         讀 assets/pets/<資料夾>/ 的 12 張 64x64 PNG (規格見 assets/ASSETS.md) 與 assets/anchors.json
         → deskpet/chars/<資料夾>.dpc
         --voice = 用哪一組內建叫聲：0 水伊布、1 水晶燈火靈、2 達克萊伊、3 百鬼綾目
  貼圖   python tools/sdcard.py sticker <Q版圖> --folder <英數檔名> --name 顯示名稱 [--voice 0~3]
         單色背景的 Q 版圖 → 去背、縮成 64x64 像素貼圖 → deskpet/chars/<檔名>.dpc
         所有動作都用同一張圖，只會上下微微浮動 (睡覺時往下沉 1 px)
  照片   python tools/sdcard.py photo <圖片...>
         裁成正方形、縮成 240x240 → deskpet/photos/<檔名>.565
  音樂   python tools/sdcard.py music <xxx.wav...>
         轉成 16kHz 單聲道 16-bit → deskpet/music/<檔名>.wav (檔名就是歌名，可以用中文)
  音效   python tools/sdcard.py sound <xxx.wav> --as <名稱> [--char <角色資料夾>]
         覆蓋內建音效：名稱 = cry / cry_happy / cry_sleepy / step / step_rain / pat / happy / notify /
         timer / wake / flower / dress / hour / alarm …；不加 --char = 所有角色共用 (deskpet/sound/common/)
  上傳   python tools/sdcard.py push [--host 192.168.x.x] [檔名...]
         透過 Wi-Fi 把 sdcard/deskpet/ 裡的角色、照片、歌曲、音效傳到板子的 SD 卡 (不用拔卡)
         角色和歌曲馬上生效；音效要重新開機。韌體更新檔不能用這個方式上傳
  韌體   python tools/sdcard.py firmware <ESP32S3.ino.bin>
         → deskpet/update/firmware.bin (插卡開機就會自動更新)

共通參數: --out <資料夾>   輸出根目錄 (預設 = 專案的 sdcard/)
"""
import argparse
import array
import json
import os
import shutil
import struct
import sys
import wave

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
RATE = 16000

# 與 tools/png2c.py 的 REQUIRED_PET 順序一致 (= 韌體的 PetFrameId)
FRAMES = ["idle/idle_0", "idle/idle_1", "blink/blink_0", "happy/happy_0", "happy/happy_1",
          "sleep/sleep_0", "sleep/sleep_1", "wake/wake_0", "wake/wake_1", "wake/wake_2",
          "look/look_left", "look/look_right"]
SFX_NAMES = ["pat", "happy", "notify", "timer", "wake", "flower", "dress", "hour", "test", "cry", "cry_happy",
             "cry_sleepy", "step", "step_rain", "tick", "tick_accent", "hit", "alarm"]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def out_dir(args, *parts):
    d = os.path.join(args.out, "deskpet", *parts)
    os.makedirs(d, exist_ok=True)
    return d


# ---------------- 角色 ----------------
def default_anchor(name):
    closed = name.startswith(("blink", "sleep")) or name == "wake/wake_0"
    return {"head_top": [32, 17], "eyes": [32, 30], "neck": [32, 41], "eyes_open": not closed}


def cmd_char(args):
    folder = args.folder
    src = os.path.join(ASSETS, "pets", folder)
    with open(os.path.join(ASSETS, "anchors.json"), encoding="utf-8") as f:
        anchors = json.load(f).get("pet", {}).get(folder, {})
    name = (args.name or folder).encode("utf-8")
    if len(name) > 47:
        sys.exit("名稱太長 (UTF-8 最多 47 bytes，約 15 個中文字)")
    out = bytearray(b"DPC1")
    out += bytes([1, args.voice, len(name)]) + name + bytes([len(FRAMES)])
    for fr in FRAMES:
        path = os.path.join(src, fr + ".png")
        if not os.path.exists(path):
            sys.exit(f"缺少影格: {os.path.relpath(path, ROOT)}")
        im = Image.open(path).convert("RGBA")
        w, h = im.size
        if w > 64 or h > 64:
            sys.exit(f"{path}: 影格最大 64x64")
        pal, lut, px = [0], {}, []
        for y in range(h):
            for x in range(w):
                r, g, b, a = im.getpixel((x, y))
                if a < 128:
                    px.append(0)
                    continue
                c = rgb565(r, g, b)
                if c not in lut:
                    if len(pal) >= 255:
                        sys.exit(f"{path}: 顏色超過 254 種")
                    lut[c] = len(pal)
                    pal.append(c)
                px.append(lut[c])
        if (len(out) + 3) % 2:  # 調色盤要從偶數位址開始
            out += b"\0"
        out += bytes([w, h, len(pal)])
        out += struct.pack("<%dH" % len(pal), *pal)
        out += bytes(px)
        a = anchors.get(fr) or default_anchor(fr)
        for key in ("head_top", "eyes", "neck"):
            out += struct.pack("<bb", int(a[key][0]), int(a[key][1]))
        out += bytes([1 if a["eyes_open"] else 0])
    dst = os.path.join(out_dir(args, "chars"), folder + ".dpc")
    with open(dst, "wb") as f:
        f.write(out)
    print(f"角色 → {dst} ({len(out)} bytes)" + ("" if anchors else "  (anchors.json 沒有這個角色，用預設對齊點)"))


# ---------------- 貼圖角色 (一張 Q 版圖 → 12 格都用同一張，靠動畫上下浮動) ----------------
def cut_background(im, tol=80):
    """從四邊 flood fill 去掉單色背景 (含淡影子)，再只留最大的一塊 (去掉愛心、浮水印)"""
    from collections import deque
    im = im.convert("RGBA")
    W, H = im.size
    px = im.load()
    bg = px[2, 2][:3]
    if px[2, 2][3] < 128:  # 本來就是透明背景
        return im
    seen = bytearray(W * H)
    q = deque([(x, 0) for x in range(W)] + [(x, H - 1) for x in range(W)] +
              [(0, y) for y in range(H)] + [(W - 1, y) for y in range(H)])
    tol2 = tol * tol
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= W or y >= H or seen[y * W + x]:
            continue
        r, g, b, a = px[x, y]
        if (r - bg[0]) ** 2 + (g - bg[1]) ** 2 + (b - bg[2]) ** 2 > tol2:
            continue
        seen[y * W + x] = 1
        px[x, y] = (0, 0, 0, 0)
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    # 去掉邊緣跟背景混色的殘邊 (反鋸齒造成的淡色一圈)，深色外框會留下來
    fringe2 = (tol * 2) ** 2
    for _ in range(2):
        kill = []
        for y in range(H):
            for x in range(W):
                r, g, b, a = px[x, y]
                if a == 0 or (r - bg[0]) ** 2 + (g - bg[1]) ** 2 + (b - bg[2]) ** 2 > fringe2:
                    continue
                if any(0 <= x + dx < W and 0 <= y + dy < H and px[x + dx, y + dy][3] == 0
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    kill.append((x, y))
        for x, y in kill:
            px[x, y] = (0, 0, 0, 0)
    label = [0] * (W * H)
    best, bestn, cur = 0, 0, 0
    for y in range(H):
        for x in range(W):
            if px[x, y][3] == 0 or label[y * W + x]:
                continue
            cur += 1
            n, q = 0, deque([(x, y)])
            label[y * W + x] = cur
            while q:
                a, b = q.popleft()
                n += 1
                for c, d in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                    if 0 <= c < W and 0 <= d < H and not label[d * W + c] and px[c, d][3] != 0:
                        label[d * W + c] = cur
                        q.append((c, d))
            if n > bestn:
                best, bestn = cur, n
    for y in range(H):
        for x in range(W):
            if label[y * W + x] != best:
                px[x, y] = (0, 0, 0, 0)
    return im.crop(im.getbbox())


def sticker_frame(im, outline=(58, 36, 52), max_h=60, max_w=60, colors=48):
    """縮成 64x64 影格 (腳底在 y=61)，加一圈深色外框；回傳 (影格, 錨點)"""
    w, h = im.size
    s = min(max_h / h, max_w / w)
    nw, nh = max(1, round(w * s)), max(1, round(h * s))
    small = im.convert("RGBa").resize((nw, nh), Image.LANCZOS).convert("RGBA")
    rgb = Image.new("RGB", small.size, outline)
    rgb.paste(small, mask=small.split()[3])
    rgb = rgb.quantize(colors=colors, method=Image.MEDIANCUT).convert("RGB")
    frame = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    ox, oy = (64 - nw) // 2, 61 - nh
    mask = small.split()[3].point(lambda a: 255 if a >= 128 else 0)
    frame.paste(rgb, (ox, oy), mask)
    fp = frame.load()
    add = []
    for y in range(64):
        for x in range(64):
            if fp[x, y][3] == 0 and any(0 <= x + dx < 64 and 0 <= y + dy < 64 and fp[x + dx, y + dy][3] != 0
                                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                add.append((x, y))
    for x, y in add:
        fp[x, y] = outline + (255,)
    # 裝扮對齊點：Q 版大頭身的經驗比例 (實際疊皇冠 / 眼鏡 / 圍巾對過)，可用 --head/--eyes/--neck 覆蓋
    top = oy - 1
    anchors = {"head_top": [32, top + 5], "eyes": [32, top + round(nh * 0.35)],
               "neck": [32, top + round(nh * 0.54)], "eyes_open": False}  # 貼圖不疊「星星眼」這類特殊眼睛
    return frame, anchors


def write_dpc(path, name, voice, frames):
    """frames = [(RGBA 影格, 錨點)] x 12，順序同 FRAMES"""
    name = name.encode("utf-8")
    if len(name) > 47:
        sys.exit("名稱太長 (UTF-8 最多 47 bytes，約 15 個中文字)")
    out = bytearray(b"DPC1")
    out += bytes([1, voice, len(name)]) + name + bytes([len(frames)])
    for im, a in frames:
        w, h = im.size
        pal, lut, px = [0], {}, []
        for y in range(h):
            for x in range(w):
                r, g, b, al = im.getpixel((x, y))
                if al < 128:
                    px.append(0)
                    continue
                c = rgb565(r, g, b)
                if c not in lut:
                    if len(pal) >= 255:
                        sys.exit("顏色超過 254 種")
                    lut[c] = len(pal)
                    pal.append(c)
                px.append(lut[c])
        if (len(out) + 3) % 2:  # 調色盤要從偶數位址開始
            out += b"\0"
        out += bytes([w, h, len(pal)])
        out += struct.pack("<%dH" % len(pal), *pal)
        out += bytes(px)
        for key in ("head_top", "eyes", "neck"):
            out += struct.pack("<bb", int(a[key][0]), int(a[key][1]))
        out += bytes([1 if a["eyes_open"] else 0])
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def cmd_sticker(args):
    im = cut_background(Image.open(args.image), args.tol)
    frame, a = sticker_frame(im)
    for key, v in (("head_top", args.head), ("eyes", args.eyes), ("neck", args.neck)):
        if v is not None:  # 手動指定 (64x64 影格裡的 y)
            a[key] = [32, v]
    print(f"裝扮對齊點 y：head={a['head_top'][1]} eyes={a['eyes'][1]} neck={a['neck'][1]}"
          "（不準的話用 --head/--eyes/--neck 調整）")
    sleep = Image.new("RGBA", (64, 64), (0, 0, 0, 0))  # 睡覺：往下沉 1 px
    sleep.paste(frame.crop((0, 0, 64, 63)), (0, 1))
    sa = dict(a, head_top=[a["head_top"][0], a["head_top"][1] + 1], eyes=[a["eyes"][0], a["eyes"][1] + 1],
              neck=[a["neck"][0], a["neck"][1] + 1])
    frames = [((sleep, sa) if fr.startswith("sleep") else (frame, a)) for fr in FRAMES]
    folder = args.folder or os.path.splitext(os.path.basename(args.image))[0]
    dst = os.path.join(out_dir(args, "chars"), folder + ".dpc")
    n = write_dpc(dst, args.name or folder, args.voice | (0x80 if args.no_costume else 0), frames)  # 0x80 = 不疊裝扮
    frame.resize((128, 128), Image.NEAREST).save(os.path.join(out_dir(args, "chars"), folder + "_preview.png"))
    print(f"貼圖角色 → {dst} ({n} bytes)，預覽圖 {folder}_preview.png (不用複製到 SD 卡)")


# ---------------- 照片 ----------------
def cmd_photo(args):
    d = out_dir(args, "photos")
    for p in args.files:
        im = Image.open(p).convert("RGB")
        w, h = im.size
        s = min(w, h)
        im = im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s)).resize((240, 240), Image.LANCZOS)
        data = array.array("H", (rgb565(*im.getpixel((x, y))) for y in range(240) for x in range(240)))
        if sys.byteorder != "little":
            data.byteswap()
        dst = os.path.join(d, os.path.splitext(os.path.basename(p))[0] + ".565")
        with open(dst, "wb") as f:
            data.tofile(f)
        print(f"照片 → {dst}")


# ---------------- 聲音 ----------------
def read_wav_mono(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 1:
        s = [(b - 128) << 8 for b in raw]
    elif width == 2:
        s = array.array("h", raw)
        if sys.byteorder != "little":
            s.byteswap()
    elif width == 3:
        s = [int.from_bytes(raw[i:i + 3], "little", signed=True) >> 8 for i in range(0, len(raw), 3)]
    elif width == 4:
        s = array.array("i", raw)
        s = [v >> 16 for v in s]
    else:
        sys.exit(f"{path}: 不支援的取樣格式")
    if ch > 1:
        s = [sum(s[i:i + ch]) // ch for i in range(0, len(s), ch)]
    return list(s), rate


def resample(s, rate):
    if rate == RATE:
        return s
    n = int(len(s) * RATE / rate)
    out = [0] * n
    step = rate / RATE
    last = len(s) - 1
    for i in range(n):
        pos = i * step
        k = int(pos)
        a = s[k]
        b = s[k + 1] if k < last else a
        out[i] = int(a + (b - a) * (pos - k))
    return out


def write_wav(path, s):
    data = array.array("h", (max(-32768, min(32767, v)) for v in s))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())


def cmd_music(args):
    d = out_dir(args, "music")
    for p in args.files:
        s, rate = read_wav_mono(p)
        dst = os.path.join(d, os.path.splitext(os.path.basename(p))[0] + ".wav")
        write_wav(dst, resample(s, rate))
        print(f"音樂 → {dst}")


def cmd_sound(args):
    if args.as_ not in SFX_NAMES:
        sys.exit("--as 必須是: " + ", ".join(SFX_NAMES))
    s, rate = read_wav_mono(args.file)
    s = resample(s, rate)
    if len(s) > RATE * 3:
        print("提醒：音效最長 3 秒，超過的部分板子會截掉")
    d = out_dir(args, "sound", args.char or "common")
    dst = os.path.join(d, args.as_ + ".wav")
    write_wav(dst, s)
    print(f"音效 → {dst}")


# ---------------- 韌體 ----------------
def cmd_firmware(args):
    with open(args.bin, "rb") as f:
        if f.read(1) != b"\xE9":
            sys.exit("這不是 ESP32 韌體檔 (.bin 開頭應為 0xE9)；請用 ESP32S3.ino.bin，不要用 merged.bin")
    if os.path.getsize(args.bin) > 0x300000:
        sys.exit("韌體超過 3MB 分割區")
    dst = os.path.join(out_dir(args, "update"), "firmware.bin")
    shutil.copyfile(args.bin, dst)
    print(f"韌體 → {dst}（插卡開機會自動更新，完成後改名成 firmware.done）")


# ---------------- 透過 Wi-Fi 上傳到板子 ----------------
def upload(host, dirname, sub, path):
    import urllib.parse
    import urllib.request
    import uuid
    boundary = uuid.uuid4().hex
    name = os.path.basename(path)
    with open(path, "rb") as f:
        data = f.read()
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"f\"; filename=\"{name}\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n").encode("utf-8") + data + f"\r\n--{boundary}--\r\n".encode()
    q = {"dir": dirname, "api": "1"}
    if sub:
        q["sub"] = sub
    url = f"http://{host}/upload?" + urllib.parse.urlencode(q)
    req = urllib.request.Request(url, data=body, headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            return r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return "失敗：" + e.read().decode("utf-8", "replace")
    except OSError as e:
        return f"連不到板子：{e}"


def cmd_push(args):
    base = os.path.join(args.out, "deskpet")
    jobs = []
    for dirname, ext in (("chars", ".dpc"), ("photos", ".565"), ("music", ".wav")):
        d = os.path.join(base, dirname)
        if os.path.isdir(d):
            jobs += [(dirname, None, os.path.join(d, n)) for n in sorted(os.listdir(d)) if n.lower().endswith(ext)]
    sd = os.path.join(base, "sound")
    if os.path.isdir(sd):
        for sub in sorted(os.listdir(sd)):
            p = os.path.join(sd, sub)
            if os.path.isdir(p):
                jobs += [("sound", sub, os.path.join(p, n)) for n in sorted(os.listdir(p)) if n.lower().endswith(".wav")]
    if args.only:
        jobs = [j for j in jobs if os.path.basename(j[2]) in args.only]
    if not jobs:
        sys.exit(f"{base} 裡沒有可以上傳的檔案")
    for dirname, sub, path in jobs:
        rel = os.path.relpath(path, args.out)
        print(f"上傳 {rel} … ", end="", flush=True)
        print(upload(args.host, dirname, sub, path))
    if os.path.exists(os.path.join(base, "update", "firmware.bin")):
        print("（韌體更新檔不能從網頁上傳，請直接複製到 SD 卡）")


def main():
    ap = argparse.ArgumentParser(description="準備 DeskPet 的 SD 卡內容")
    ap.add_argument("--out", default=os.path.join(ROOT, "sdcard"), help="輸出根目錄 (預設 sdcard/)")
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("char")
    p.add_argument("folder")
    p.add_argument("--name")
    p.add_argument("--voice", type=int, default=0, choices=[0, 1, 2, 3])
    p.set_defaults(fn=cmd_char)
    p = sp.add_parser("sticker")
    p.add_argument("image")
    p.add_argument("--name")
    p.add_argument("--folder", help="檔名 (英數字；也是 SD 音效資料夾名稱)")
    p.add_argument("--voice", type=int, default=0, choices=[0, 1, 2, 3])
    p.add_argument("--tol", type=int, default=80, help="去背容許的顏色差 (背景的淡影子也會去掉)")
    p.add_argument("--head", type=int, help="頭頂 y (帽子、皇冠放這裡)")
    p.add_argument("--eyes", type=int, help="眼睛 y (眼鏡放這裡)")
    p.add_argument("--neck", type=int, help="脖子 y (圍巾、披風放這裡)")
    p.add_argument("--no-costume", action="store_true", help="這個角色不穿裝扮")
    p.set_defaults(fn=cmd_sticker)
    p = sp.add_parser("photo")
    p.add_argument("files", nargs="+")
    p.set_defaults(fn=cmd_photo)
    p = sp.add_parser("music")
    p.add_argument("files", nargs="+")
    p.set_defaults(fn=cmd_music)
    p = sp.add_parser("sound")
    p.add_argument("file")
    p.add_argument("--as", dest="as_", required=True)
    p.add_argument("--char")
    p.set_defaults(fn=cmd_sound)
    p = sp.add_parser("push")
    p.add_argument("--host", default="deskpet.local", help="板子的 IP 或 deskpet.local")
    p.add_argument("only", nargs="*", help="只上傳這些檔名 (不填 = 全部)")
    p.set_defaults(fn=cmd_push)
    p = sp.add_parser("firmware")
    p.add_argument("bin")
    p.set_defaults(fn=cmd_firmware)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()

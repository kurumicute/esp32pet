# 素材規格

目前的圖都是 `tools/gen_draft_art.py` 產生的**草稿**，可以直接換成你自己畫的 PNG。

## 換圖流程

1. 用 Aseprite、Piskel 等像素繪圖軟體編輯或覆蓋下面列出的 PNG（檔名不能改）
2. 如果角色位置或頭部位置變了，修改 `assets/anchors.json` 的對齊點
3. 執行 `python tools/png2c.py`（會重新產生 `src/assets/Sprites.h/.cpp`）
4. 重新編譯、燒錄

> ⚠️ 重新執行 `tools/gen_draft_art.py` 會把 PNG 和 anchors.json **覆蓋回草稿**，換成自己的圖之後就不要再執行它。

## 通用規則

| 項目 | 規格 |
|---|---|
| 格式 | PNG，RGBA |
| 透明 | alpha < 128 視為透明 |
| 顏色 | 每張圖最多 255 色（建議 16 色以內，比較有像素感） |
| 放大 | 螢幕上以 **2 倍** 最近鄰放大顯示 |
| 尺寸上限 | 255 x 255 |

## 寵物影格 `pets/<角色>/`

目前只有一個內建角色資料夾：`ayame`（百鬼綾目），由 `tools/gen_draft_art.py` 把 `example7.png` 用貼圖流程（同 `tools/sdcard.py sticker`）產生，12 格都是同一張圖（睡覺往下沉 1 px），`png2c.py` 會把相同影格合併成一份。每個資料夾都要有下表**全部**影格。
新增內建角色：在 `tools/gen_draft_art.py` 的 `CHARACTERS` 與 `tools/png2c.py` 的 `CHARACTERS` 加在最後面，在 `src/pet/Characters.cpp` 的 `BUILTIN_VOICE` 指定叫聲組，並在 `src/ui/Strings.h` 的 `Str::CHARACTER_NAMES` 加名稱（數量不一致會編譯失敗）。anchors.json 的 `pet` 也是依角色分組：`"pet": {"ayame": {"idle/idle_0": {...}}}`。

- 每張 **64 x 64**，每一張影格中的角色比例、位置要一致（只有姿勢不同）
- 畫面中寵物畫布為 64 x 68 邏輯像素，影格放在 y = 2，浮動動畫會上下移動 1~2 px
- 頭頂上方請留約 12 px 給帽子類裝扮

| 檔案 | 用途 |
|---|---|
| `idle/idle_0.png`、`idle/idle_1.png` | 待機（兩格之間尾巴擺動） |
| `blink/blink_0.png` | 眨眼（閉眼） |
| `happy/happy_0.png`、`happy/happy_1.png` | 開心（^^ 眼、張嘴），換裝動畫也會用到 |
| `sleep/sleep_0.png`、`sleep/sleep_1.png` | 睡覺（呼吸起伏） |
| `wake/wake_0.png` ~ `wake/wake_2.png` | 睡醒：打哈欠 → 伸懶腰 → 張眼 |
| `look/look_left.png`、`look/look_right.png` | 向左看、向右看 |

## 裝扮 `accessories/`

裝扮是疊在寵物上的獨立圖層，所有動畫共用同一張圖，靠錨點跟著頭部移動。

| 檔案 | 錨點 | 說明 |
|---|---|---|
| `bow/bow.png` | head_top | 蝴蝶結 |
| `crown/crown.png` | head_top | 皇冠 |
| `glasses/glasses.png` | eyes | 圓框眼鏡 |
| `leaf_hat/leaf_hat.png` | head_top | 葉子帽 |
| `santa_hat/santa_hat.png` | head_top | 聖誕帽 |
| `sleep_hat/sleep_hat.png` | head_top | 睡眠帽 |
| `cape/cape_back.png` | neck | 披風（畫在寵物**後方**） |
| `cape/cape_front.png` | neck | 披風扣環（畫在寵物前方） |
| `star_eyes/star_eyes.png` | eyes | 特殊眼睛（閉眼影格自動隱藏） |

## anchors.json

```jsonc
"pet": {
  "ayame": {                  // 角色資料夾名稱
    "idle/idle_0": {
      "head_top": [32, 17],   // 頭頂 (64x64 影格內座標)
      "eyes":     [32, 30],   // 兩眼中心
      "neck":     [32, 41],   // 頸環中心
      "eyes_open": true       // 是否睜眼
    }
  }
},
"accessories": {
  "crown/crown": {
    "anchor": "head_top",   // head_top / eyes / neck
    "pivot": [8, 10],       // 裝扮圖中要對齊錨點的那個像素
    "dx": 0, "dy": 2,       // 額外微調
    "eyes_open_only": false // true = 閉眼時不畫
  }
}
```

裝扮的實際位置 = 影格錨點 + (dx, dy) − pivot。

## UI `ui/`

| 檔案 | 尺寸 | 用途 |
|---|---|---|
| `wifi_on.png` / `wifi_off.png` | 13 x 10 | 右上角 Wi-Fi 狀態 |
| `sparkle_0~2.png` | 9 x 9 | 換裝閃光（小 → 大） |
| `zzz.png` | 6 x 6 | 睡覺的 Z |
| `shadow.png` | 34 x 6 | 寵物腳下的影子 |
| `snack.png` | 12 x 12 | 午餐 / 點心時間的餅乾 |
| `sweat.png` | 5 x 7 | 低電量汗滴 |
| `bolt.png` | 7 x 9 | 充電閃電（疊在電池圖示上） |
| `weather_sun/partly/cloud/rain/thunder/snow/fog.png` | 15 x 12 | 左上角天氣圖示 |

## 新增裝扮

1. 在 `assets/accessories/<名稱>/` 加 PNG，並在 `anchors.json` 加上設定
2. 在 `tools/png2c.py` 的 `REQUIRED_ACC` 加上名稱，然後執行轉換
3. 在 `src/costume/CostumeManager.cpp` 的 `COSTUMES` 表加一行

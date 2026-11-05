# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP32-S3 desktop clock pet ("DeskPet") for the **Waveshare ESP32-S3 1.54" LCD, non-touch** board (ST7789 240x240 SPI LCD — the user sometimes calls it "OLED"). Arduino framework; all code is under `src/` and `ESP32S3.ino` is an empty stub (Arduino compiles `src/**` recursively). Switchable characters: Vaporeon / Chandelure / Darkrai style built in, more (e.g. hololive Nakiri Ayame stickers) on the SD card. User-facing docs: `README.md` (developer, Traditional Chinese) and `使用手冊.md` (end-user manual) — keep both in sync when behavior, buttons, settings, or console commands change. `assets/ASSETS.md` documents the art spec. Reply to the user in Traditional Chinese.

## Build / flash / verify

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" --warnings default --output-dir <dir> .
esptool --chip esp32s3 --port <COM> --baud 115200 --before usb-reset --after watchdog-reset write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0xe000 <core>/tools/partitions/boot_app0.bin 0x10000 <dir>/ESP32S3.ino.bin
```

- Toolchain: Arduino-ESP32 core **3.3.12**, GFX Library for Arduino **1.6.9** (1.6.0 does not compile on core 3.3.x). The 3 MB app partition is ~88% full — every built-in character costs ~1.7%, so new characters should go on the SD card.
- `arduino-cli` may only exist inside Arduino IDE (`%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`). The serial port differs per PC (COM3 / COM6) — check `arduino-cli board list`.
- A full rebuild (Sprites.cpp / Font12.cpp are huge) takes 10+ minutes: run it in the background.
- Flash with esptool at a fixed **115200** baud as above. `arduino-cli --upload` often fails on this board's native USB-CDC ("Cannot configure port" when esptool switches baud). If even that fails, the user must hold BOOT while plugging USB in. If the board stays silent after flashing: `esptool --port <COM> --before no-reset --after watchdog-reset chip-id`.
- Always write `boot_app0.bin` at `0xe000` too: after an SD-card firmware update the board boots from **app1**, and writing only `0x10000` then silently has no effect.
- No unit tests. Verification = zero-warning compile + driving the device over USB serial (115200, pyserial with `dtr=False, rts=False` so opening the port doesn't reset it). Console commands: `status` (heap/PSRAM/min heap), `char [n]`, `sd`, `backup`, `restore`, `walk`, `diary`, `tap`, `sound on|off`, `sound`, `cry`, `song n`, `rhythm`, `alarm test` (really rings; reset to stop), `i2c`, `imu`. **`wifi` is not a query** — it starts interactive SSID/password entry (send a <8-char line to cancel). Decode serial output as UTF-8 and print with `ascii()`; the Windows console mangles Chinese.
- The web panel may or may not be reachable from the dev PC (try `curl http://<board-ip>/`). The screen can never be observed from here — say so instead of claiming visual results.

## Asset pipeline (generated code — don't hand-edit outputs)

- `python tools/gen_draft_art.py` regenerates **all** PNGs in `assets/` + `assets/anchors.json` (deterministic, overwrites). Characters are procedural draw functions listed in `CHARACTERS`; preview by compositing the PNGs, since there is no on-device screenshot.
- `python tools/png2c.py` → `src/assets/Sprites.h/.cpp`: palette-indexed sprites (index 0 = transparent), the `PetFrameId` enum (`PF_*`), `PET_FRAMES[char][PF_*]`, `PET_CHAR_IDS`. Its `CHARACTERS` order = built-in character ids and must match `Str::CHARACTER_NAMES` (static_assert). New UI sprites go in `REQUIRED_UI`.
- `python tools/gen_font.py` → `src/assets/Font12.*` (full Cubic 11 charset, ~31.7k glyphs; emoji render as boxes).
- Meadow backgrounds must stay **horizontally periodic over 120 px** (walk mode scrolls the ground rows ≥ y=52 with wrap-around).
- `tools/sdcard.py` prepares SD content into `sdcard/deskpet/…`: `char` (pixel character from `assets/pets/<id>/` → `.dpc`), `sticker` (one chibi image → background removed, 64x64, same frame for all animations; `--head/--eyes/--neck` costume anchors, `--voice 0..3`, `--no-costume`), `photo` (240x240 RGB565 `.565`), `music`/`sound` (16 kHz mono WAV), `firmware`, and `push --host <ip>` (uploads over Wi-Fi).

## Architecture

- `App` (`App.cpp`, plus `AppFun.cpp` for menu/alarm/rhythm/music/photo frame/cries, `Layout.h` for screen constants) is the only orchestrator: it owns every module, a `Screen` state machine (`enter()` redraws a screen; per-screen `update*`/`*Button`), sleep state (`Awake/Nap/Night`, main screen only), and top popups (`Popup::Passkey/Notify/Alarm` over y 0–59). Everything is non-blocking, driven from `loop()` with `millis()`.
- Rendering: `PixelCanvas` is a 64x68 logical canvas scaled 2x. `App::petY()` puts it at y=104 (flush with the bottom) on the main screen and y=60 elsewhere. The main screen has the date small at the top center, and messages/timer just below the clock (`MAIN_LINE_Y`). `Scene` composes a 120x68 RGB565 background (time of day + weather overlays) and the pet canvas copies its slice. `WalkMode` keeps its own scrolled copy (`pet_.setScene()`). Large buffers go in PSRAM via `allocLarge()`.
- Characters: `Characters::frame(c, PF_*)` is the only way frames are fetched. Built-ins come from `PET_FRAMES`; SD characters are `chars/*.dpc` parsed into PSRAM (format in `Characters.h`; voice byte low bits = synth voice, 0x80 = don't draw costumes). Animations (`PetAnimation.cpp`) reference `PF_*` ids, never sprites. Costumes are separate layers placed via per-frame anchors (head_top/eyes/neck). Cries/footsteps per synth voice live in `CHAR_SFX` (`SoundManager.cpp`, `SYNTH_VOICES`), selected each loop via `sound_.setVoice(synthVoice, slot)`.
- Menu UI uses `drawHeader` / `drawCard` / `drawToggle` / `drawKeyHints(left, mid, right)`. Physical buttons left→right are **BOOT (GPIO0), PWR (GPIO5), PLUS (GPIO4)**; key hints are three slots in that order (Chinese labels 左鍵/中鍵/右鍵). The activity menu scrolls (6 visible rows); `TARGET[]` must match `Str::MENU_ITEMS` (static_assert).
- Settings: `SettingsManager` (NVS namespace `deskpet`, writes only changed keys, `sanitize()` on load). Adding a setting touches: the `Settings` struct + load/save/sanitize, `SETTING_ITEMS` + `changeSetting()` + `drawSettings()` + the `settingGroup()` table in App.cpp, `Str::` labels, the web panel row **and** `handleSave()` validation, the `FIELDS` table in `SdArchive.cpp` (backup), and the README/manual settings tables.
- `Diary` keeps a fixed-size 7-day blob in NVS (`diary`/`days`, versioned; changing `DiaryDay`/`DiaryStat::Count` wipes saved diaries unless you bump/migrate `VERSION`). On every save it also calls the archive hook → SD.
- Background tasks: `WeatherService` (CWA F-C0032-001 over HTTPS with the cert bundle), the `SoundManager` playback task (queue; also streams SD music), and `AncsClient` (iPhone ANCS on raw NimBLE, client role on an inbound connection).
- `WebPanel` (port 80, mDNS `deskpet`) streams HTML from a fixed 768-byte buffer (`sendf` — it *sends*; use `snprintf(chunk, …)` to format headers). It also hosts the Wi-Fi captive portal when `WifiManager` opens the `DeskPet-XXXX` AP. While the AP is up, STA auto-reconnect is off and retries are every 5 min (STA scans make the AP hop channels and phones fail to join).
- SD card (`src/storage/`): `SdStore` mounts SDMMC 4-bit once at boot (pins in `BoardConfig.h`); everything under `/deskpet/` is optional, and every feature must no-op without a card. `SdArchive` handles the diary archive, the 10-min CSV log, and settings backup (never the Wi-Fi password or CWA key). `SdUpdate` flashes `update/firmware.bin` to the other OTA slot at boot. Web routes: `/history`, `/stats`, `/dl` (path-whitelisted), `/backup`, `/restore`, `/upload` + `/sddel` (chars/photos/music/sound only; firmware upload over the unauthenticated web is deliberately not allowed; `?api=1` returns plain text for `sdcard.py push`). Uploads of chars/music hot-reload via `App::webSdChanged`.
- All UI text lives in `src/ui/Strings.h`.

## Constraints and decisions from the user

- **Sound is default OFF.** Automatic effects (pat, notify, cries, footsteps, hourly) require the 音效 setting AND the pet awake. User-initiated features (alarm, music box, rhythm game, `cry` command) play regardless (`force=true`). Audio HW (I2S + ES8311) is initialized lazily; amp GPIO7 is HIGH only while playing. **Never initialize the microphone (ES7210)** without explicit user consent.
- No RPG battles, collection/unlock systems, in-game currency, or complex quests. Walk/rhythm events are cosmetic and only counted in the diary.
- Secrets (Wi-Fi password, CWA API key) live only in NVS — never in source, logs, SD files, or web output. The web panel has **no password by the user's choice**: validate every field on the backend, HTML-escape all output (including names read from SD files), and keep the upload whitelist.
- The "screen face-down = blank" feature was removed at the user's request; don't reintroduce it.
- Don't guess hardware details: pins/registers come from Waveshare's official repo `waveshareteam/ESP32-S3-Touch-LCD-1.54` (the non-touch repo URL 404s; SD pins from its `07_sd_card_test`) and Espressif's `esp_codec_dev` es8311 driver. No RTC on the board (I2C: 0x18 ES8311, 0x40 ES7210, 0x6B QMI8658).

## Gotchas

- Compare `millis()` timestamps with signed differences (`(int32_t)(now - t)`): web/serial handlers call `millis()` later than the loop's cached `now`, and unsigned underflow previously caused instant naps and absurd walk durations.
- Internal RAM is tight after the SD features (min free heap ~37 KB, which HTTPS needs). Keep new tables and buffers in PSRAM (`allocLarge`, `heap_caps_malloc(MALLOC_CAP_SPIRAM)`), not static arrays, and check `status` after changes.
- Tap/pat thresholds in `MotionSensor.cpp` and tilt axis/sign (`TILT_AXIS`, `TILT_SIGN` in `BoardConfig.h`) are not calibrated on hardware.
- Escapes in patch scripts: the Bash tool collapses `\\` to `\`, and in a heredoc `\n`/`\0` inside Python strings become real newlines/NUL bytes in the C++ output (this broke string literals several times). Write patch scripts to a file with the Write tool (use `chr(92)` for backslashes), or use Edit; afterwards scan `src/` for NUL bytes.

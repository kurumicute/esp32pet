#pragma once
#include <stdint.h>

// Waveshare ESP32-S3-LCD-1.54 (無觸控版) 腳位
// 來源：官方範例 ESP-IDF 01_factory (bsp_display.h / bsp_power_manager.c / main.c)
namespace Pins {
constexpr int8_t LCD_DC = 45;
constexpr int8_t LCD_CS = 21;
constexpr int8_t LCD_SCK = 38;
constexpr int8_t LCD_MOSI = 39;
constexpr int8_t LCD_RST = 40;
constexpr int8_t LCD_BL = 46;     // 背光 (PWM)

constexpr int8_t BTN_BOOT = 0;    // BOOT 鍵，按下為 LOW
constexpr int8_t BTN_PLUS = 4;    // PLUS 鍵，按下為 LOW
constexpr int8_t BTN_PWR = 5;     // PWR 鍵，按下為 LOW

constexpr int8_t I2C_SDA = 42;    // 板載 I2C：QMI8658 六軸感測器 (0x6B)
constexpr int8_t I2C_SCL = 41;

constexpr int8_t BAT_ADC = 1;       // 電池電壓，板上 1/3 分壓
constexpr int8_t BAT_CHARGING = 3;  // 充電指示，LOW = 充電中

constexpr int8_t POWER_LATCH = 2; // 電池供電時須保持 HIGH，拉 LOW = 關機
constexpr int8_t AMP_CTRL = 7;    // 喇叭功放致能 (HIGH = 開)；只在播放音效時拉 HIGH
// ES8311 音訊 DAC 的 I2S 腳位 (Waveshare 官方 bsp_codec.c)；麥克風 DIN = 11 不使用
constexpr int8_t I2S_MCLK = 8;
constexpr int8_t I2S_BCLK = 9;
constexpr int8_t I2S_LRCK = 10;
constexpr int8_t I2S_DOUT = 12;
// TF (microSD) 卡槽：SDMMC 4-bit，無插卡偵測腳
// 來源：官方 Arduino 範例 examples/ESP32-S3-LCD-1.54-demo/Arduino-3.2.0/examples/07_sd_card_test
constexpr int8_t SD_CLK = 16;
constexpr int8_t SD_CMD = 15;
constexpr int8_t SD_D0 = 17;
constexpr int8_t SD_D1 = 18;
constexpr int8_t SD_D2 = 13;
constexpr int8_t SD_D3 = 14;
}  // namespace Pins

constexpr uint32_t LCD_SPI_HZ = 40000000;

// 傾斜方向：用 QMI8658 的哪一軸代表「螢幕左右」，以及正負號。
// 若寵物看的方向跟你歪的方向相反，把 TILT_SIGN 改成 -1；完全沒反應就把 TILT_AXIS 改成 1 (y 軸)。
// 可用序列埠 imu 指令查看各軸數值。
constexpr uint8_t TILT_AXIS = 0;  // 0 = x, 1 = y
constexpr int8_t TILT_SIGN = 1;

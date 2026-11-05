// 由 tools/png2c.py 自動產生，請勿手動修改。素材來源: assets/
#pragma once
#include "SpriteTypes.h"

// 寵物影格編號 (每個角色都有同一組影格)
enum PetFrameId : uint8_t {
  PF_idle_0, PF_idle_1, PF_blink_0, PF_happy_0, PF_happy_1, PF_sleep_0, PF_sleep_1, PF_wake_0, PF_wake_1, PF_wake_2, PF_look_left, PF_look_right, PF_COUNT
};
constexpr uint8_t PET_CHAR_COUNT = 3;
extern const PetFrameAsset *const PET_FRAMES[PET_CHAR_COUNT][PF_COUNT];
extern const char *const PET_CHAR_IDS[PET_CHAR_COUNT];  // 資料夾名稱 (SD 卡音效資料夾也用這個)

extern const AccessoryLayer ACC_bow;
extern const AccessoryLayer ACC_crown;
extern const AccessoryLayer ACC_glasses;
extern const AccessoryLayer ACC_leaf_hat;
extern const AccessoryLayer ACC_santa_hat;
extern const AccessoryLayer ACC_sleep_hat;
extern const AccessoryLayer ACC_cape_back;
extern const AccessoryLayer ACC_cape_front;
extern const AccessoryLayer ACC_star_eyes;
extern const Sprite UI_wifi_on;
extern const Sprite UI_wifi_off;
extern const Sprite UI_sparkle_0;
extern const Sprite UI_sparkle_1;
extern const Sprite UI_sparkle_2;
extern const Sprite UI_zzz;
extern const Sprite UI_shadow;
extern const Sprite UI_snack;
extern const Sprite UI_sweat;
extern const Sprite UI_bolt;
extern const Sprite UI_weather_sun;
extern const Sprite UI_weather_partly;
extern const Sprite UI_weather_cloud;
extern const Sprite UI_weather_rain;
extern const Sprite UI_weather_thunder;
extern const Sprite UI_weather_snow;
extern const Sprite UI_weather_fog;
extern const Sprite UI_bg_night;
extern const Sprite UI_bg_dawn;
extern const Sprite UI_bg_day;
extern const Sprite UI_bg_dusk;
extern const Sprite UI_ov_sun;
extern const Sprite UI_ov_moon;
extern const Sprite UI_ov_cloud_white;
extern const Sprite UI_ov_cloud_grey;
extern const Sprite UI_ov_cloud_dark;
extern const Sprite UI_ov_cloud_night;
extern const Sprite UI_ov_fog;
extern const Sprite UI_fan;
extern const Sprite UI_scarf;
extern const Sprite UI_butterfly_0;
extern const Sprite UI_butterfly_1;
extern const Sprite UI_walk_flower;
extern const Sprite UI_hand;
extern const Sprite UI_heart;

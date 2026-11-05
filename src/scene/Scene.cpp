#include "Scene.h"

#include "../assets/Sprites.h"
#include "../display/LargeAlloc.h"

namespace {
struct Pos {
  int8_t x, y;
};
// 擺放位置與 assets 預覽一致
const Pos CLOUDS_FEW[] = {{14, 8}, {60, 14}};
const Pos CLOUDS_MANY[] = {{4, 6}, {34, 13}, {66, 5}, {94, 12}};
const Pos CLOUDS_STORM[] = {{4, 6}, {34, 13}, {66, 5}, {94, 12}, {50, 1}};
const Pos CLOUDS_FOG[] = {{10, 8}, {70, 10}};
}  // namespace

bool Scene::begin() {
  if (buf_ == nullptr) buf_ = allocLarge<uint16_t>((size_t)W * H);
  return buf_ != nullptr;
}

DayPeriod Scene::periodForHour(int hour) {
  if (hour >= 5 && hour < 7) return DayPeriod::Dawn;
  if (hour >= 7 && hour < 17) return DayPeriod::Day;
  if (hour >= 17 && hour < 19) return DayPeriod::Dusk;
  return DayPeriod::Night;
}

void Scene::blit(const Sprite &s, int16_t x, int16_t y) {
  for (int16_t sy = 0; sy < s.h; sy++) {
    const int16_t yy = y + sy;
    if (yy < 0 || yy >= H) continue;
    for (int16_t sx = 0; sx < s.w; sx++) {
      const int16_t xx = x + sx;
      const uint8_t idx = s.px[sy * s.w + sx];
      if (idx == 0 || xx < 0 || xx >= W) continue;
      buf_[yy * W + xx] = s.pal[idx];
    }
  }
}

bool Scene::build(DayPeriod period, WeatherKind kind, bool weatherFx) {
  if (buf_ == nullptr) return false;
  if (!weatherFx) kind = WeatherKind::Unknown;
  if (built_ && period == period_ && kind == kind_ && weatherFx == fx_) return false;
  built_ = true;
  period_ = period;
  kind_ = kind;
  fx_ = weatherFx;

  const Sprite *base = &UI_bg_night;
  if (period == DayPeriod::Dawn) base = &UI_bg_dawn;
  else if (period == DayPeriod::Day) base = &UI_bg_day;
  else if (period == DayPeriod::Dusk) base = &UI_bg_dusk;
  for (int32_t i = 0; i < W * H; i++) buf_[i] = 0;
  blit(*base, 0, 0);

  const bool night = period == DayPeriod::Night;
  const Sprite &grey = night ? UI_ov_cloud_night : UI_ov_cloud_grey;
  switch (kind) {
    case WeatherKind::Sun:
    case WeatherKind::Partly:
      if (night) blit(UI_ov_moon, 98, 5);
      else if (period == DayPeriod::Day) blit(UI_ov_sun, 96, 4);
      if (kind == WeatherKind::Partly)
        for (const Pos &p : CLOUDS_FEW) blit(night ? UI_ov_cloud_night : UI_ov_cloud_white, p.x, p.y);
      break;
    case WeatherKind::Cloud:
    case WeatherKind::Snow:
      for (const Pos &p : CLOUDS_MANY) blit(grey, p.x, p.y);
      break;
    case WeatherKind::Rain:
    case WeatherKind::Thunder:
      for (const Pos &p : CLOUDS_STORM) blit(night ? UI_ov_cloud_night : UI_ov_cloud_dark, p.x, p.y);
      break;
    case WeatherKind::Fog:
      for (const Pos &p : CLOUDS_FOG) blit(grey, p.x, p.y);
      blit(UI_ov_fog, 0, 34);
      break;
    default:  // 沒有天氣資料：晴朗的夜晚有月亮
      if (night) blit(UI_ov_moon, 98, 5);
      break;
  }
  return true;
}

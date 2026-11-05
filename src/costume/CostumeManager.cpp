#include "CostumeManager.h"

#include "../assets/Sprites.h"
#include "../ui/Strings.h"

namespace {
// 不需要解鎖、收集或貨幣，全部裝扮直接可選。
// 順序要與 CostumeId 及 Str::COSTUME_NAMES 一致。
const Costume COSTUMES[] = {
    {Str::COSTUME_NAMES[0], nullptr, nullptr},
    {Str::COSTUME_NAMES[1], nullptr, &ACC_bow},
    {Str::COSTUME_NAMES[2], nullptr, &ACC_crown},
    {Str::COSTUME_NAMES[3], nullptr, &ACC_glasses},
    {Str::COSTUME_NAMES[4], nullptr, &ACC_leaf_hat},
    {Str::COSTUME_NAMES[5], nullptr, &ACC_santa_hat},
    {Str::COSTUME_NAMES[6], nullptr, &ACC_sleep_hat},
    {Str::COSTUME_NAMES[7], &ACC_cape_back, &ACC_cape_front},
    {Str::COSTUME_NAMES[8], nullptr, &ACC_star_eyes},
};
constexpr uint8_t COUNT = sizeof(COSTUMES) / sizeof(COSTUMES[0]);
static_assert(COUNT == (uint8_t)CostumeId::Count, "COSTUMES 與 CostumeId 數量不一致");
static_assert(sizeof(Str::COSTUME_NAMES) / sizeof(Str::COSTUME_NAMES[0]) == COUNT,
              "Str::COSTUME_NAMES 數量不一致");
}  // namespace

uint8_t CostumeManager::count() const { return COUNT; }

const Costume &CostumeManager::get(uint8_t index) const {
  return COSTUMES[index < COUNT ? index : 0];
}

void CostumeManager::setCurrent(uint8_t index) { current_ = index < COUNT ? index : 0; }

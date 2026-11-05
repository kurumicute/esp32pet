#include "PetAnimation.h"

#include <stddef.h>

#include "../assets/Sprites.h"

namespace {
#define ANIM(name, loop) {name, (uint8_t)(sizeof(name) / sizeof(name[0])), loop}

// 待機：輕微上下浮動 + 尾巴擺動
const AnimFrame IDLE[] = {
    {PF_idle_0, 320, 0}, {PF_idle_0, 320, -1}, {PF_idle_1, 320, -1}, {PF_idle_1, 320, 0},
};
const AnimFrame BLINK[] = {{PF_blink_0, 140, 0}};
const AnimFrame HAPPY[] = {
    {PF_happy_0, 160, 0}, {PF_happy_1, 160, -2}, {PF_happy_0, 160, 0},
    {PF_happy_1, 160, -2}, {PF_happy_0, 400, 0},
};
// 睡覺：緩慢呼吸
const AnimFrame SLEEP[] = {{PF_sleep_0, 1100, 0}, {PF_sleep_1, 1100, 0}};
// 睡醒：打哈欠 → 伸懶腰 → 張眼
const AnimFrame WAKE[] = {
    {PF_wake_0, 500, 0}, {PF_wake_1, 450, -1}, {PF_wake_1, 450, -2},
    {PF_wake_1, 300, -1}, {PF_wake_2, 400, 0},
};
const AnimFrame LOOK_LEFT[] = {{PF_look_left, 1300, 0}};
const AnimFrame LOOK_RIGHT[] = {{PF_look_right, 1300, 0}};
// 換裝：開心跳兩下 (閃光特效由 Pet 疊加)
const AnimFrame DRESS[] = {
    {PF_happy_1, 140, -2}, {PF_happy_0, 140, 0}, {PF_happy_1, 140, -2},
    {PF_happy_0, 140, 0}, {PF_wake_2, 200, 0},
};
// 散步：面向右邊，一步一跳
const AnimFrame WALK[] = {{PF_look_right, 180, 0}, {PF_look_right, 180, -1}};
// 摸頭：被手壓一下、瞇眼 (手由 Pet 疊加，前 6 格)，最後開心跳一下
const AnimFrame PAT[] = {
    {PF_happy_0, 200, 1}, {PF_happy_0, 200, 0}, {PF_happy_0, 200, 1},
    {PF_happy_0, 200, 0}, {PF_happy_0, 200, 1}, {PF_happy_0, 200, 0},
    {PF_happy_1, 240, -2}, {PF_happy_0, 400, 0},
};

const Animation ANIMS[] = {
    ANIM(IDLE, true),       ANIM(BLINK, false), ANIM(HAPPY, false),
    ANIM(SLEEP, true),      ANIM(WAKE, false),  ANIM(LOOK_LEFT, false),
    ANIM(LOOK_RIGHT, false), ANIM(DRESS, false), ANIM(WALK, true), ANIM(PAT, false),
};
static_assert(sizeof(ANIMS) / sizeof(ANIMS[0]) == (size_t)AnimId::Count, "ANIMS 與 AnimId 數量不一致");
}  // namespace

const Animation &getAnimation(AnimId id) {
  const uint8_t i = (uint8_t)id;
  return ANIMS[i < (uint8_t)AnimId::Count ? i : 0];
}

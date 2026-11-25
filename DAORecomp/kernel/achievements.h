#pragma once
// The game's achievements: the title reports an unlocked one through XGI, the gamer services
// of the console's XAM (XMsgStartIORequest, app 0xFB, message 0x000B0008: XUserWriteAchievements).
// The runtime has no screen of its own for them; a front end that wants them installs a callback
// (the Android app: android/app/src/main/cpp/achievements_android.cpp) which is called, on the
// game's thread, with each achievement id the game writes. Without one nothing happens.

#include <cstdint>

namespace dao::kernel
{
    using AchievementCallback = void (*)(uint32_t achievementId);

    // Constant-initialised, so a front end's static initialiser may set it at any time.
    inline AchievementCallback g_achievementCallback = nullptr;
}

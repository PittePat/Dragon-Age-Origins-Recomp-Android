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

    // The achievements list a front end shows over the paused game (the START menu's Achievements
    // row, on a build that has one: ui/menu_movies.cpp). Called on the game's thread when the game
    // is paused for it, and again to close it if the game leaves (a load): the front end's list
    // tells the runtime when the player closes it (ui/cheats.h: ToggleCheats while it's open).
    // Without them the row does nothing.
    using AchievementsListFunction = void (*)();
    inline AchievementsListFunction g_showAchievementsList = nullptr;
    inline AchievementsListFunction g_hideAchievementsList = nullptr;
}

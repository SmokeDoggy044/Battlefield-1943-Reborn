// Reborn's own achievements (TekRantGaming, 16 Nov 2026: "add some custom achievements into our achievements, assign
// gamer score to them and give them custom images"), beside the game's 12:
//   101 I am reborn      10 G  loading into your first multiplayer match (any server, a custom match too)
//   102 I am forever     20 G  loading into a match on a community server (not a Reborn one, not your own match)
//   103 I am inevitable  30 G  loading into a custom match you are hosting
// "Loading into" is the soldier in the level (on foot or in a seat), not the join's pick (game_menu.cpp
// NoteRebornAchievements).
// And the vehicles (TekRantGaming, 18 Nov 2026: "add these new custom achievements and create photos for them too ...
// all of these should work regardless of being on US or Jap side"), each the first time, from any seat:
//   104 Steel Behemoth    getting into a tank
//   105 Road Warrior      getting into a jeep
//   106 Naval Warfare     getting into a boat
//   107 Air Superiority   getting into a fighter plane
//   108 Death From Above  calling in the air raid's bombers (flown from the airfield's bunker)
// Which vehicle it is comes from the game itself: the seat's vehicle (seat_info.h) and its data's name, as the game's
// data has it (LCVP, Corsair...: the names the server's time per vehicle counts by, server_stats.cpp), then its
// class (Light_Boat, Aircraft_Airframe_corsair); the seat's own control scheme says a plane's pilot or the air raid's
// bombers. The same on both sides.
//
// They sit in the SDK's own catalog (AchievementManager::RegisterAchievement, made for a recomp's own entries), so
// everything the game's 12 have, ours have: the unlock saved with theirs (<user>\achievements\5841097E.toml), the
// popup and chime (achievement_toast.cpp), the Achievements page and Stats & Rewards, and the account's awards that
// the backend counts for "how many players have it". The ids are 101 and up: the game's title data has 1 to 12, and
// the backend counts every id from 1 to 999 (plasma.py achievement_share), so no backend change is needed.
//
// The pictures are ours, drawn by tools/make_reborn_achievements.py into data/achievements/<id>.png next to the exe
// (64 x 64 RGBA PNGs, as the game's own pictures are).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/system/achievement_store.h>

namespace bf1943::reborn_achievements {

inline constexpr uint32_t kReborn = 101, kForever = 102, kInevitable = 103;
inline constexpr uint32_t kSteelBehemoth = 104, kRoadWarrior = 105, kNavalWarfare = 106, kAirSuperiority = 107,
                          kDeathFromAbove = 108;

// One of ours (their image ids are their ids).
bool IsOurs(uint32_t id);
// How many of ours there are (the Achievements page says so).
size_t Count();

// Ours as the catalog has them (also for the Achievements page before the game runs, achievement_toast.h).
std::vector<rex::system::AchievementInfo> Infos();

// Puts ours into the SDK's catalog once the game's own are loaded (it replaces the whole catalog when it loads the
// game's title data). Cheap: call it before anything that asks the catalog. True once ours are there.
bool Ensure();

// The picture for one of ours (data/achievements/<id>.png next to the exe); empty for any other.
std::filesystem::path IconFile(uint32_t image_id);

// Unlocks one of ours, with its popup and chime; `why` goes to the log. True when it was not unlocked before.
bool Unlock(uint32_t id, const char* why);

// The vehicle achievement a vehicle's name or class gives (as the game's data has them: "chi-ha", "Light_Boat"), or
// 0: a tank, a jeep, a boat, a fighter plane or the air raid. Guns and anything else give none.
uint32_t ForVehicle(const std::string& name);

// From the menus' tick: `in_level` while a match's level runs. Once each time the local soldier's seat changes
// (seat_info.h), the vehicle it is in, then the unlock with its popup. True when one was unlocked (the account's
// awards are sent then).
bool VehicleTick(bool in_level);

}  // namespace bf1943::reborn_achievements

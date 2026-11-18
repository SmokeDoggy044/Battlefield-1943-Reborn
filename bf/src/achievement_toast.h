// The achievement popup (TekRantGaming, 16 Nov 2026: "make it work like the king kong game had achievements with a
// popup and a sound"; his pick M1 "Medal Case", runs/ui-designs/out/m1_popup.png): when the game unlocks one of its 12
// achievements (the SDK's XGIUserWriteAchievements, AchievementManager), or one of Reborn's own is unlocked
// (reborn_achievements.h), a paper dispatch slip slides in at the top right with the achievement's own picture, its name in the game's headline font, its words and its gamerscore, and a
// short chime plays. Several at once queue, one after the other. As King Kong Recompiled's toast (kk/src/toast.cpp),
// in the Field Dispatch look.
//
// The chime is the Battlefield theme's hook made from the player's own game files (theme_chime.h; since 18 Nov 2026),
// King Kong's plain chime when those give none.
//
// Settings > Audio: bf_achievement_popups (on or off) and bf_achievement_volume (0 to 100, the chime's volume; 0 is
// silent). A WAV of the player's own, <user>\sounds\achievement.wav (16 bit PCM), plays in place of the chime.
#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <rex/ui/overlay/achievement_notification.h>

namespace bf1943 {

class AchievementToast final : public rex::ui::AchievementNotificationDialog {
 public:
  explicit AchievementToast(rex::ui::ImGuiDrawer* drawer);
  void Push(const rex::system::AchievementEvent& event) override;  // any thread

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Toast {
    std::string title, description;
    uint32_t gamerscore = 0, image_id = 0;
    double started = -1;
  };
  std::mutex mutex_;
  std::deque<Toast> queue_;
};

// Shows a popup as if the game had unlocked achievement `id` (the Achievements page's Y, "Test the popup"; and
// BF_DEV_ACHIEVEMENT_TEST in dev builds). Nothing is unlocked or saved.
void TestAchievementPopup(uint32_t id);

// The achievement's own picture (the game's title data, its image `image_id`), written once into the user's cache as
// a PNG for the menus (kit::DrawImageFile). Empty while there is none.
std::filesystem::path AchievementIconFile(uint32_t image_id);

// Every achievement, the game's and Reborn's own, with this player's unlocks, also before the game runs (the
// Achievements page on Home: the SDK has no catalog until the game starts, at the first join). From the SDK once it
// has its catalog; else from the copy of the game's list kept in the user's cache (achievements\catalog.txt beside
// the pictures, written each run once the game has it, NoteAchievementCatalog) with the SDK's own unlock file.
// `from_game`: whether the SDK's catalog answered; `has_games`: whether the game's own are in the list (false before
// the game has ever run on this user folder: only Reborn's own then).
struct AchievementEntry {
  rex::system::AchievementInfo info;
  bool unlocked = false;
  uint64_t when = 0;  // FILETIME
};
std::vector<AchievementEntry> AchievementList(bool* from_game = nullptr, bool* has_games = nullptr);

// The chime (or the player's achievement.wav), at the achievement_sound volume.
void PlayAchievementSound();

// The Battlefield theme's chime (theme_chime.h) made into `extracted` (user\cache\extracted) from the menu music
// there, unless it is there already; true when it is there after the call. The first start's worker makes it after
// the menu music (shader_prep.cpp ExtractGameArt), so an unlock only reads it; any thread.
bool MakeAchievementChime(const std::filesystem::path& extracted);

// Where the user folder is (bf1943_app.h, at setup): the settings file and sounds\achievement.wav.
void SetAchievementUserDir(const std::filesystem::path& user_dir);

}  // namespace bf1943

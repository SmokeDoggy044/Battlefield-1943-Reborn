// Stats & Rewards, Leaderboards and Achievements (TekRantGaming, 16 Nov 2026: "update stats and rewards ui, update
// leaderboard ui and expand upon what stats we collect like kills and K/D ... achievements ... in a list just like
// kong"). His picks of the six mockups (runs/ui-designs/build_progress.py, out/*.png):
//   S1 "Service Record": one page, everything at once: the soldier and the next rank, eight big numbers, time by kit
//      and by vehicle, results by map, the latest rewards and the next achievements to earn.
//   L1 "Roll of Honour": the server browser's table, a folder tab per stat, every column, your own row pinned under
//      it, the chosen soldier's card at the right.
//   M1 "Medal Case": the game's 12 achievements in two columns, locked ones grey with a padlock and how far along you
//      are, unlocked ones with when; the popup in game is achievement_toast.cpp.
// Included inside GameMenu (game_menu.cpp), after game_menu_launcher.inc. Sizes are the mockups' pixels / 1.5 on the
// 1280 x 720 stage.
//
// Where the numbers come from: our dedicated servers count each player's kills, deaths, captures and the rest from the
// game's own bookkeeping (server_stats.cpp) and send them to the backend (server/player_stats.py); a page asks the
// backend for a player's record ("player_card") or a board ("leaderboard" with a sort and a period). The game's own
// score events on this PC (stat.<label> in the settings, counted since before the servers did) fill in until then: each
// number shown is the larger of the two.

  // ---- a player's record from the backend

  struct ProgCard {
    bool found = false;
    std::string name;
    std::map<std::string, long long> n;  // kills, deaths, ... (player_stats.py COUNTS)
    int best_streak = 0;
    struct MapRow {
      long long rounds = 0, wins = 0, kills = 0, deaths = 0;
    };
    std::map<std::string, MapRow> maps;                // wake, iwo, guadalcanal, coral
    std::map<std::string, std::pair<int, int>> places; // board -> (place, of)
    long long Get(const char* k) const {
      auto it = n.find(k);
      return it == n.end() ? 0 : it->second;
    }
  };
  static constexpr const char* kProgCounts[] = {
      "kills",        "deaths",       "teamkills", "suicides", "headshots", "melee",    "assists",  "captures",
      "defends",      "repairs",      "heals",     "score",    "rounds",    "wins",     "losses",   "seconds",
      "kit_infantry", "kit_rifleman", "kit_scout", "veh_plane", "veh_tank", "veh_jeep", "veh_boat", "vk_plane",
      "vk_tank",      "vk_jeep",      "vk_boat"};
  static long long JsonNumber(const std::string& obj, const char* key) {
    const std::string v = JsonField(obj, key);
    return v.empty() ? 0 : std::atoll(v.c_str());
  }
  static ProgCard ParseCard(const std::string& reply) {
    ProgCard c;
    c.found = JsonField(reply, "found") == "true";
    c.name = JsonField(reply, "name");
    // The counts are the record's own keys, before its "maps" (whose rows say kills and deaths too).
    const size_t maps_at = reply.find("\"maps\"");
    const std::string head = reply.substr(0, maps_at);
    for (const char* k : kProgCounts) c.n[k] = std::max(0LL, JsonNumber(head, k));
    c.best_streak = int(JsonNumber(reply, "best_streak"));
    for (const char* m : {"wake", "iwo", "guadalcanal", "coral"}) {
      const size_t at = maps_at == std::string::npos ? maps_at : reply.find(std::string("\"") + m + "\": {", maps_at);
      if (at == std::string::npos) continue;
      const std::string obj = reply.substr(at, reply.find('}', at) - at + 1);
      c.maps[m] = {JsonNumber(obj, "rounds"), JsonNumber(obj, "wins"), JsonNumber(obj, "kills"), JsonNumber(obj, "deaths")};
    }
    if (const size_t pl = reply.find("\"places\""); pl != std::string::npos) {
      for (const char* b : {"score", "kills", "kd", "captures", "headshots", "vk_plane", "vk_tank"}) {
        const size_t at = reply.find(std::string("\"") + b + "\": [", pl);
        if (at == std::string::npos) continue;
        const char* p = reply.c_str() + reply.find('[', at) + 1;
        char* end = nullptr;
        const long place = std::strtol(p, &end, 10);
        const long of = end && *end == ',' ? std::strtol(end + 1, nullptr, 10) : 0;
        if (place > 0) c.places[b] = {int(place), int(of)};
      }
    }
    return c;
  }

  // One request at a time to the backend, on a worker; its last reply kept.
  struct ProgFetch {
    std::mutex mutex;
    std::string key, reply;  // what was asked (a name, a board), the reply to it
    bool ok = false, answered = false;
    std::atomic<bool> busy{false};
    double asked_at = -100;
  };
  void ProgAsk(ProgFetch& f, const std::string& key, const std::string& request, double every) {
    bool same;
    {
      std::lock_guard lock(f.mutex);
      same = f.key == key;
    }
    if (f.busy.load() || (same && ImGui::GetTime() - f.asked_at < every)) return;
    f.asked_at = ImGui::GetTime();
    f.busy = true;
    if (!same) {
      std::lock_guard lock(f.mutex);
      f.key = key, f.reply.clear(), f.ok = false, f.answered = false;
    }
    std::thread([&f, key, request] {
      std::string reply;
      const bool ok = RegistryRequest(request, reply);
      {
        std::lock_guard lock(f.mutex);
        if (f.key == key) f.reply = reply, f.ok = ok, f.answered = true;
      }
      f.busy = false;
    }).detach();
  }
  ProgFetch prog_me_, prog_board_, prog_other_, prog_share_;

  static std::string JsonQ(const std::string& s) {
    std::string out = "\"";
    for (char c : s)
      if (c != '"' && c != '\\' && static_cast<unsigned char>(c) >= 32) out += c;
    return out + "\"";
  }

  // The player's own record: the backend's, each count the larger of it and this PC's own score events.
  ProgCard MyRecord(bool& from_servers) {
    const std::string me = settings_.Get("player_name", "");
    if (!me.empty())
      ProgAsk(prog_me_, me, "{\"op\": \"player_card\", \"name\": " + JsonQ(me) + "}", 15);
    ProgCard c;
    {
      std::lock_guard lock(prog_me_.mutex);
      if (prog_me_.ok && prog_me_.key == me) c = ParseCard(prog_me_.reply);
    }
    from_servers = c.found;
    const Settings& record = test_profile_active_ ? test_profile_ : settings_;
    auto local = [&](std::initializer_list<const char*> labels) {
      long long n = 0;
      for (const char* l : labels) n += std::max(0LL, std::atoll(record.Get(std::string("stat.") + l, "0").c_str()));
      return n;
    };
    auto keep = [&](const char* k, long long v) { c.n[k] = std::max(c.Get(k), v); };
    keep("kills", local({"StringForKill", "kill"}));
    keep("headshots", local({"StringForHeadshot", "headshot"}));
    keep("melee", local({"StringForMeleeKill", "melee kill"}));
    keep("captures", local({"CQ_StringForFlagCapture"}));
    keep("defends", local({"StringForDefensiveKillBonus", "defensive kill bonus"}));
    keep("repairs", local({"StringForRepairTeamVehicle", "StringForRepairSquadVehicle", "team vehicle repair", "squad vehicle repair"}));
    keep("heals", local({"StringForSquadHealing", "StringForTeamHealing", "squad healing", "team healing"}));
    keep("assists", local({"StringForKillAssist", "StringForSquadKillAssist", "kill assist", "squad kill assist"}));
    keep("teamkills", local({"StringForTeamKill", "team kill"}));
    double total = 0;
    for (const auto& [key, value] : record.values)
      if (key.rfind("stat.", 0) == 0)
        if (const size_t comma = value.find(','); comma != std::string::npos) total += std::atof(value.c_str() + comma + 1);
    keep("score", std::llround(std::max(0.0, total)));
    const double secs = std::atof(record.Get("match_seconds", "0").c_str());
    keep("seconds", std::isfinite(secs) ? std::llround(std::clamp(secs, 0.0, 2147483647.0)) : 0);
    return c;
  }

  static std::string Thousands(long long v) {
    std::string s = std::to_string(std::llabs(v)), out;
    for (size_t i = 0; i < s.size(); ++i) {
      if (i && (s.size() - i) % 3 == 0) out += ',';
      out += s[i];
    }
    return (v < 0 ? "-" : "") + out;
  }
  static std::string HoursMinutes(long long seconds) {
    const long long m = seconds / 60;
    if (m < 60) return fmt::format("{} min", m);
    return fmt::format("{} h {:02d} min", m / 60, int(m % 60));
  }
  static std::string KdText(long long kills, long long deaths) {
    if (!kills && !deaths) return "\xE2\x80\x94";
    return fmt::format("{:.2f}", double(kills) / double(std::max(1LL, deaths)));
  }
  int RankOfSeconds(double seconds) {
    static const Settings none;
    return Rank(none, seconds);
  }
  // How far `seconds` in matches are from the rank they give to the next (0 to 1), and the minutes left (as
  // RankProgress, for any time: the servers' count of it).
  static float RankProgressOf(double seconds, int& minutes_left) {
    static constexpr int kMinutes[] = {0,   10,  25,  45,  70,   100,  140,  190,  250,  320,  400, 500,
                                       620, 760, 920, 1100, 1300, 1550, 1850, 2200, 2600, 3100, 3700};
    const double minutes = seconds / 60.0;
    int r = 0;
    for (int i = 0; i < int(std::size(kMinutes)); ++i)
      if (minutes >= kMinutes[i]) r = i;
    minutes_left = 0;
    if (r + 1 >= int(std::size(kMinutes))) return 1.0f;
    minutes_left = int(std::ceil(kMinutes[r + 1] - minutes));
    return float((minutes - kMinutes[r]) / double(kMinutes[r + 1] - kMinutes[r]));
  }

  // The game's achievements, and how far this player is along the ones our numbers can tell (our own count, which
  // the game's may differ from: the game decides the unlock).
  struct AchRow {
    rex::system::AchievementInfo info;
    bool got = false;
    uint64_t when = 0;  // FILETIME
    long long have = 0, need = 0;  // need 0: no count of ours
  };
  // The game's 12 and Reborn's own 3, also before the game runs (AchievementList: the copy of the game's catalog
  // from an earlier run and the unlock file; until the game has run once on this user folder, Reborn's own only).
  bool ach_has_games_ = true;
  std::vector<AchRow> AchievementRows(const ProgCard& c) {
    std::vector<AchRow> rows;
    SetAchievementUserDir(UserDir());  // the unlock file and the copy, before the game's launch set it
    const std::string default_state = test_profile_.Get("achievements", "actual");
    for (const AchievementEntry& e : AchievementList(nullptr, &ach_has_games_)) {
      const auto& a = e.info;
      AchRow r;
      r.info = a;
      while (!r.info.label.empty() && r.info.label.back() == ' ') r.info.label.pop_back();
      r.got = e.unlocked;
      if (test_profile_active_) {
        const std::string state = test_profile_.Get("achievement." + std::to_string(a.id), default_state);
        if (state == "unlocked") r.got = true;
        if (state == "locked") r.got = false;
      }
      r.when = r.got ? e.when : 0;
      auto maps_won = [&] {
        long long n = 0;
        for (const char* m : {"wake", "iwo", "guadalcanal"})
          if (auto it = c.maps.find(m); it != c.maps.end() && it->second.wins > 0) ++n;
        return n;
      };
      switch (a.id) {  // the 12 of the game's title data (runs/ui-designs notes)
        case 1: r.have = c.Get("captures"), r.need = 5; break;                       // Attacker
        case 3: r.have = c.Get("rounds"), r.need = 100; break;                       // Tour of Duty II
        case 4: r.have = c.Get("defends"), r.need = 5; break;                        // Defender
        case 6: r.have = c.Get("veh_plane") / 60, r.need = 10; break;                // Milkrun, minutes
        case 7:                                                                      // Motorman
          r.have = (c.Get("vk_jeep") > 0) + (c.Get("vk_tank") > 0) + (c.Get("vk_plane") > 0), r.need = 3;
          break;
        case 9: r.have = maps_won(), r.need = 3; break;                              // The Pacific Campaign
        case 10: r.have = c.Get("rounds"), r.need = 30; break;                       // Tour of Duty I
        case 11: r.have = c.Get("captures"), r.need = 25; break;                     // Veteran
        case 12: r.have = c.Get("melee"), r.need = 1; break;                         // Melee Man
        default: break;
      }
      r.have = std::min(r.have, r.need);
      rows.push_back(r);
    }
    return rows;
  }
  // An achievement whose counted progress is complete, unlocked (issue 37, 17 Nov 2026: Attacker at 5 of 5 on this
  // page and still locked). The page counts from our servers' stats and this PC's own score events, but only the
  // game's own code unlocked anything, and it never sees progress from before 0.4.7 or from another PC. The game's
  // words for the nine counted ones are lifetime totals ("Capture five flags", "Play 30 matches"), as ours are. Every
  // 10 s while a menu is open (a match ends in one), once the game's list is in; with its popup, as the game's own.
  double counted_checked_at_ = -100;
  void CountedAchievementsTick() {
    if (test_profile_active_ || ImGui::GetTime() - counted_checked_at_ < 10) return;
    counted_checked_at_ = ImGui::GetTime();
    auto* k = REX_KERNEL_STATE();
    if (!k || k->achievements().ListAchievements().empty()) return;
    bool from_servers = false;
    const ProgCard c = MyRecord(from_servers);
    for (const AchRow& r : AchievementRows(c)) {
      if (r.got || r.need <= 0 || r.have < r.need) continue;
      const auto result = k->achievements().UnlockAchievement(r.info.id, rex::system::AchievementNotification::kShow);
      REXLOG_INFO("BF achievements: {} unlocked from its counted progress ({} of {}{}): {}", r.info.label, r.have,
                  r.need, from_servers ? ", the servers' record" : ", this PC's own",
                  result == rex::system::AchievementUnlockResult::kUnlocked ? "done" : "not taken");
    }
  }
  static std::string WhenText(uint64_t filetime) {
    if (filetime < 116444736000000000ULL) return {};
    const std::time_t t = std::time_t((filetime - 116444736000000000ULL) / 10000000ULL);
    std::tm at{}, now{};
    const std::time_t n = std::time(nullptr);
#if defined(_WIN32)
    localtime_s(&at, &t), localtime_s(&now, &n);
#else
    localtime_r(&t, &at), localtime_r(&n, &now);
#endif
    static const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const bool today = at.tm_year == now.tm_year && at.tm_yday == now.tm_yday;
    return today ? fmt::format("TODAY \xC2\xB7 {:02d}:{:02d}", at.tm_hour, at.tm_min)
                 : fmt::format("{} {} \xC2\xB7 {:02d}:{:02d}", at.tm_mday, kMonths[at.tm_mon % 12], at.tm_hour, at.tm_min);
  }

  // The achievement's picture (the game's own), with a green tick or a dark padlock in its corner.
  void AchIcon(ImDrawList* d, const Stage& st, ImVec2 a, float size, uint32_t image_id, bool got, float badge = 1) {
    const ImVec2 b(a.x + size, a.y + size);
    d->AddRectFilled(ImVec2(a.x + st(1), a.y + st(2)), ImVec2(b.x + st(1), b.y + st(2)), IM_COL32(0, 0, 0, 60));
    const fs::path icon = AchievementIconFile(image_id);
    const ImU32 tint = got ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 118, 108, 255);
    if (icon.empty() || !kit::DrawImageFile(d, icon, a, b, true, false, 0, tint)) d->AddRectFilled(a, b, kit::kPaperDim);
    if (badge <= 0) return;
    const ImVec2 c(b.x - st(2), b.y - st(2));
    const float r = st(9.5f) * badge;
    d->AddCircleFilled(c, r, got ? kit::kGreen : kit::kInkSoft, 20);
    if (got) {
      d->AddLine(ImVec2(c.x - r * 0.42f, c.y + r * 0.05f), ImVec2(c.x - r * 0.1f, c.y + r * 0.36f), IM_COL32(255, 255, 255, 255), st(2) * badge);
      d->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.36f), ImVec2(c.x + r * 0.45f, c.y - r * 0.33f), IM_COL32(255, 255, 255, 255), st(2) * badge);
    } else {
      const float w = r * 0.9f;
      d->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - r * 0.12f), ImVec2(c.x + w * 0.5f, c.y + r * 0.5f), kit::kCream, st(1));
      d->PathArcTo(ImVec2(c.x, c.y - r * 0.12f), w * 0.32f, 3.14159265f, 6.2831853f, 10);
      d->PathStroke(kit::kCream, 0, st(1.6f) * badge);
    }
  }

  // A thin bar from x0 to x1 at y, `p` of it filled.
  static void ProgBar(ImDrawList* d, ImVec2 a, ImVec2 b, float p, ImU32 fill = kit::kGreen, ImU32 back = IM_COL32(34, 36, 27, 34)) {
    d->AddRectFilled(a, b, back);
    if (p > 0) d->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * std::clamp(p, 0.0f, 1.0f), b.y), fill);
  }

  // A section label on the paper with its rule under it.
  void PaperLabel(ImDrawList* d, const Stage& st, ImVec2 at, float w, const std::string& t, const std::string& right = {}) {
    const kit::Fonts& f = kit::GetFonts();
    kit::Spaced(d, f.typed, st(8.5f), at, kit::kInkSection, t, st(2.4f));
    if (!right.empty())
      kit::Spaced(d, f.typed, st(8.5f), ImVec2(at.x + w - kit::SpacedWidth(f.typed, st(8.5f), right, st(2.4f)), at.y),
                  kit::kInkSection, right, st(2.4f));
    d->AddRectFilled(ImVec2(at.x, at.y + st(15)), ImVec2(at.x + w, at.y + st(16.5f)), kit::kInk);
  }

  // ======================================================== S1: Service Record

  void StatsPage(ImDrawList* d, ImGuiIO& io, const Input& in_raw) {
    NotePage("stats and rewards");
    Input in = in_raw;
    const Stage st = StageOf(io);
    const kit::Fonts& f = kit::GetFonts();
    MenuBackdrop(d, io.DisplaySize, st);
    bool online_ok = false;
    MenuChrome(d, io, st, OnlineStatus(online_ok), online_ok);
    std::vector<Cue> cues = {{kit::Button::kLB, "ACHIEVEMENTS"}, {kit::Button::kRB, "LEADERBOARDS"}, {kit::Button::kB, "BACK"}};
    Prompts(d, st, st.P(48, 662), cues, in, 15);
    MenuFoot(d, st);
    bool servers = false;
    const ProgCard c = MyRecord(servers);
    const Settings& record = test_profile_active_ ? test_profile_ : settings_;
    const double secs = double(c.Get("seconds"));
    const int rank = test_profile_active_ ? Rank(record, secs) : RankOfSeconds(secs);
    kit::Spaced(d, f.typed, st(10.5f), st.P(48, 91), kit::kKhakiDim,
                test_profile_active_ ? "STATS & REWARDS \xC2\xB7 TEST PROFILE"
                : servers ? "STATS & REWARDS \xC2\xB7 COUNTED BY THE REBORN SERVERS"
                          : "STATS & REWARDS \xC2\xB7 FROM THIS PC UNTIL YOUR NEXT MATCH ON THE REBORN SERVERS",
                st(2));
    kit::GameText(d, kit::GameFont::kHeadline, st(40), st.P(48, 105), kit::kCream, "SERVICE RECORD");

    // ---- the soldier: portrait, insignia, rank, the next one, the ladder, time in the field
    {
      const ImVec2 a = st.P(48, 159), b = st.P(361, 640);
      kit::PaperArt(d, a, b);
      d->PushClipRect(a, b, true);
      kit::ArtAt(d, "portrait_us_rifleman", ImVec2(a.x - st(4), a.y + st(19) + st(100)), st(200));
      d->PopClipRect();
      RankInsignia(d, st, ImVec2(b.x - st(57), a.y + st(57)), st(80), rank);
      const float tx = a.x + st(133);
      kit::Spaced(d, f.typed, st(8.5f), ImVec2(tx, a.y + st(107)), kit::kInkSection, fmt::format("RANK {}", rank), st(2.4f));
      d->AddText(f.ui_bold, st(20), ImVec2(tx, a.y + st(118)), kit::kInk, kRanks[rank - 1]);
      std::string who = settings_.Get("player_name", "Soldier");
      if (const std::string tag = settings_.Get("clan_tag", ""); !tag.empty()) who += "  \xC2\xB7  [" + tag + "]";
      d->AddText(f.ui, st(11.5f), ImVec2(tx, a.y + st(145)), kit::kInkFaint, who.c_str());
      float y = a.y + st(226);
      const float x0 = a.x + st(17), x1 = b.x - st(17);
      int minutes_left = 0;
      const float p = test_profile_active_ ? 1.0f : RankProgressOf(secs, minutes_left);
      if (rank < int(std::size(kRanks))) {
        kit::Spaced(d, f.typed, st(8.5f), ImVec2(x0, y), kit::kInkSection, "NEXT: " + Upper(kRanks[rank]), st(2.4f));
        const std::string left = fmt::format("{} MIN TO GO", minutes_left);
        kit::Spaced(d, f.typed, st(8.5f), ImVec2(x1 - kit::SpacedWidth(f.typed, st(8.5f), left, st(2.4f)), y), kit::kInkSection, left, st(2.4f));
        ProgBar(d, ImVec2(x0, y + st(17)), ImVec2(x1, y + st(25)), p);
        d->AddText(f.ui, st(11), ImVec2(x0, y + st(31)), kit::kInkFaint, "Ranks come with time in matches.");
      } else {
        kit::Spaced(d, f.typed, st(8.5f), ImVec2(x0, y), kit::kInkSection, "THE HIGHEST RANK", st(2.4f));
        ProgBar(d, ImVec2(x0, y + st(17)), ImVec2(x1, y + st(25)), 1);
      }
      y += st(64);
      kit::Spaced(d, f.typed, st(8.5f), ImVec2(x0, y), kit::kInkSection,
                  fmt::format("RANK LADDER \xC2\xB7 {} OF {}", rank, std::size(kRanks)), st(2.4f));
      {  // the insignia around this one: earned in colour, the next ones faint
        const int first = std::clamp(rank - 6, 1, int(std::size(kRanks)) - 10);
        float lx = x0 + st(4);
        for (int r = first; r < first + 11 && r <= int(std::size(kRanks)); ++r) {
          const bool now = r == rank;
          const float h = now ? st(34) : st(23);
          char name[16];
          std::snprintf(name, sizeof(name), "rank_%02d", r);
          const ImVec2 cc(lx + h * 0.5f, y + st(40));
          if (now) d->AddRect(ImVec2(cc.x - h * 0.5f - st(3), cc.y - h * 0.5f - st(3)), ImVec2(cc.x + h * 0.5f + st(3), cc.y + h * 0.5f + st(3)),
                              kit::kStamp, 0, 0, st(2));
          const float w = kit::ArtAt(nullptr, name, cc, h, IM_COL32(255, 255, 255, 255), false);
          if (w > 0) kit::ArtAt(d, name, ImVec2(cc.x - w * 0.5f, cc.y), h, r > rank ? IM_COL32(150, 150, 140, 70) : IM_COL32(255, 255, 255, 255));
          lx += h + st(now ? 8 : 2);
        }
      }
      y += st(80);
      kit::Spaced(d, f.typed, st(8.5f), ImVec2(x0, y), kit::kInkSection, "TIME IN THE FIELD", st(2.4f));
      d->AddText(f.ui_bold, st(27), ImVec2(x0, y + st(12)), kit::kInk, HoursMinutes(c.Get("seconds")).c_str());
      std::string sub;
      if (c.Get("rounds")) {
        const char* best = nullptr;
        long long most = 0;
        for (const auto& [m, row] : c.maps)
          if (row.rounds > most) most = row.rounds, best = m == "wake" ? "Wake Island" : m == "iwo" ? "Iwo Jima" : m == "guadalcanal" ? "Guadalcanal" : "Coral Sea";
        sub = fmt::format("{} rounds", c.Get("rounds")) + (best ? fmt::format(" \xC2\xB7 favourite map {}", best) : "");
      } else {
        sub = "Rounds and maps count from your next match.";
      }
      d->AddText(f.ui, st(11), ImVec2(x0, y + st(50)), kit::kInkFaint, sub.c_str());
    }

    // ---- the eight numbers, time by kit and by vehicle, results by map
    {
      const ImVec2 a = st.P(377, 159), b = st.P(924, 640);
      kit::PaperArt(d, a, b);
      const long long kills = c.Get("kills"), deaths = c.Get("deaths"), rounds = c.Get("rounds"), wins = c.Get("wins");
      const char* dash = "\xE2\x80\x94";
      struct Tile {
        std::string label, big, unit, sub;
      };
      const double minutes = std::max(1.0, double(c.Get("seconds")) / 60.0);
      const Tile tiles[8] = {
          {"KILLS", Thousands(kills), "", c.best_streak ? fmt::format("Best streak {}", c.best_streak) : "Kills by you"},
          {"DEATHS", servers ? Thousands(deaths) : dash, "", rounds ? fmt::format("{:.1f} a round", double(deaths) / double(rounds)) : "Counted by the servers"},
          {"K / D", servers ? KdText(kills, deaths) : dash, "", "Kills for each death"},
          {"SCORE", Thousands(c.Get("score")), "", fmt::format("{:.0f} a minute", double(c.Get("score")) / minutes)},
          {"FLAGS CAPTURED", Thousands(c.Get("captures")), "", fmt::format("{} defended", Thousands(c.Get("defends")))},
          {"ROUNDS WON", servers ? Thousands(wins) : dash, rounds ? "/ " + Thousands(rounds) : "",
           rounds ? fmt::format("{} % won", std::llround(100.0 * double(wins) / double(rounds))) : "Counted by the servers"},
          {"HEADSHOTS", Thousands(c.Get("headshots")), "",
           kills ? fmt::format("{} % of kills", std::llround(100.0 * double(c.Get("headshots")) / double(kills))) : "Of your kills"},
          {"REPAIRS", Thousands(c.Get("repairs")), "", fmt::format("{} heals", Thousands(c.Get("heals")))}};
      const float tw = (b.x - a.x) / 4, th = st(88);
      for (int i = 0; i < 8; ++i) {
        const ImVec2 t0(a.x + tw * float(i % 4), a.y + th * float(i / 4));
        if (i % 4) d->AddRectFilled(ImVec2(t0.x, t0.y), ImVec2(t0.x + 1, t0.y + th), kit::kRule);
        if (i < 4) d->AddRectFilled(ImVec2(t0.x, t0.y + th - 1), ImVec2(t0.x + tw, t0.y + th), kit::kRule);
        kit::Spaced(d, f.typed, st(8), ImVec2(t0.x + st(15), t0.y + st(13)), kit::kInkSection, tiles[i].label, st(2.2f));
        d->AddText(f.ui_bold, st(31), ImVec2(t0.x + st(14), t0.y + st(22)), kit::kInk, tiles[i].big.c_str());
        if (!tiles[i].unit.empty())
          d->AddText(f.ui_bold, st(15), ImVec2(t0.x + st(16) + kit::TextWidth(f.ui_bold, st(31), tiles[i].big), t0.y + st(37)),
                     kit::kInkSection, tiles[i].unit.c_str());
        d->AddText(f.ui, st(10.5f), ImVec2(t0.x + st(15), t0.y + st(64)), kit::kInkFaint, tiles[i].sub.c_str());
      }
      d->AddRectFilled(ImVec2(a.x, a.y + th * 2 - st(1)), ImVec2(b.x, a.y + th * 2 + st(1)), kit::kInk);
      // Time by kit and by vehicle: a bar each (of the longest), the time and the kills from it.
      const float cy = a.y + th * 2 + st(16), cw = (b.x - a.x - st(17) * 2 - st(22)) / 2;
      struct Bar {
        const char* art;
        const char* name;
        long long secs, kills;
        bool kills_known;
      };
      const Bar kits[] = {{"class_infantry", "Infantry", c.Get("kit_infantry"), 0, false},
                          {"class_rifleman", "Rifleman", c.Get("kit_rifleman"), 0, false},
                          {"class_scout", "Scout", c.Get("kit_scout"), 0, false}};
      const Bar vehicles[] = {{"icon_plane", "Fighter", c.Get("veh_plane"), c.Get("vk_plane"), true},
                              {"icon_tank", "Tank", c.Get("veh_tank"), c.Get("vk_tank"), true},
                              {"icon_jeep", "Jeep", c.Get("veh_jeep"), c.Get("vk_jeep"), true},
                              {"icon_boat", "Boat", c.Get("veh_boat"), c.Get("vk_boat"), true}};
      auto bars = [&](float x, const char* title, const Bar* list, int count, float icon_h) {
        PaperLabel(d, st, ImVec2(x, cy), cw, title);
        long long most = 1, any = 0;
        for (int i = 0; i < count; ++i) most = std::max(most, list[i].secs), any += list[i].secs;
        float y = cy + st(22);
        for (int i = 0; i < count; ++i) {
          const Bar& r = list[i];
          kit::ArtAt(d, r.art, ImVec2(x + st(2), y + st(18)), icon_h, IM_COL32(34, 36, 27, 255));
          d->AddText(f.ui_bold, st(13), ImVec2(x + st(38), y + st(4)), kit::kInk, r.name);
          const std::string v = !any ? "\xE2\x80\x94" : r.kills_known ? fmt::format("{} \xC2\xB7 {} KILLS", Upper(HoursMinutes(r.secs)), r.kills) : Upper(HoursMinutes(r.secs));
          kit::Spaced(d, f.typed, st(7.5f), ImVec2(x + cw - kit::SpacedWidth(f.typed, st(7.5f), v, st(1)), y + st(8)), kit::kInkFaint, v, st(1));
          ProgBar(d, ImVec2(x + st(38), y + st(24)), ImVec2(x + cw, y + st(29)), float(double(r.secs) / double(most)));
          d->AddRectFilled(ImVec2(x, y + st(36)), ImVec2(x + cw, y + st(37)), kit::kRule);
          y += st(37);
        }
        if (!any) d->AddText(f.ui, st(10), ImVec2(x, y + st(4)), kit::kInkFaint, "Counted from your next match on the Reborn servers.");
      };
      bars(a.x + st(17), "TIME BY KIT", kits, 3, st(20));
      bars(a.x + st(17) + cw + st(22), "TIME BY VEHICLE", vehicles, 4, st(9));
      // By map: its picture, the rounds won and the K/D there.
      const float my = a.y + st(377), mw = (b.x - a.x - st(34) - st(30)) / 4;
      PaperLabel(d, st, ImVec2(a.x + st(17), my), b.x - a.x - st(34), "BY MAP");
      static const std::pair<const char*, const char*> kMaps[] = {
          {"wake", "Wake Island"}, {"iwo", "Iwo Jima"}, {"guadalcanal", "Guadalcanal"}, {"coral", "Coral Sea"}};
      for (int i = 0; i < 4; ++i) {
        const float x = a.x + st(17) + float(i) * (mw + st(10));
        MapPicture(d, st.u * 0.6f, ImVec2(x, my + st(24)), ImVec2(x + mw, my + st(64)), kMaps[i].second);
        d->AddText(f.ui_bold, st(12), ImVec2(x, my + st(68)), kit::kInk, kMaps[i].second);
        const auto it = c.maps.find(kMaps[i].first);
        const std::string line = it == c.maps.end() || !it->second.rounds
                                     ? std::string("No rounds yet")
                                     : fmt::format("{} of {} won \xC2\xB7 K/D {}", it->second.wins, it->second.rounds,
                                                   KdText(it->second.kills, it->second.deaths));
        d->AddText(f.ui, st(9.5f), ImVec2(x, my + st(84)), kit::kInkFaint, line.c_str());
      }
    }

    // ---- rewards: the latest unlocks, the achievement total, the next ones to earn
    {
      const ImVec2 a = st.P(940, 159), b = st.P(1232, 640);
      kit::PaperArt(d, a, b);
      const float x0 = a.x + st(16), w = b.x - a.x - st(32);
      std::vector<AchRow> ach = AchievementRows(c);
      PaperLabel(d, st, ImVec2(x0, a.y + st(16)), w, "REWARDS \xC2\xB7 LATEST");
      float y = a.y + st(38);
      std::vector<const AchRow*> latest;
      for (const AchRow& r : ach)
        if (r.got) latest.push_back(&r);
      std::sort(latest.begin(), latest.end(), [](const AchRow* p, const AchRow* q) { return p->when > q->when; });
      auto reward_row = [&](std::function<void(ImVec2)> picture, const std::string& title, const std::string& sub, const std::string& when) {
        picture(ImVec2(x0, y + st(7)));
        d->AddText(f.ui_bold, st(13), ImVec2(x0 + st(46), y + st(6)), kit::kInk, title.c_str());
        d->AddText(f.ui, st(10.5f), ImVec2(x0 + st(46), y + st(24)), kit::kInkFaint, sub.c_str());
        if (!when.empty())
          kit::Spaced(d, f.typed, st(7), ImVec2(x0 + w - kit::SpacedWidth(f.typed, st(7), when, st(1.4f)), y + st(9)), kit::kInkSection, when, st(1.4f));
        d->AddRectFilled(ImVec2(x0, y + st(50)), ImVec2(x0 + w, y + st(51)), kit::kRule);
        y += st(52);
      };
      reward_row([&](ImVec2 p) { RankInsignia(d, st, ImVec2(p.x + st(18), p.y + st(18)), st(34), rank); },
                 std::string("Rank: ") + kRanks[rank - 1], fmt::format("Rank {} of {}", rank, std::size(kRanks)), "");
      for (size_t i = 0; i < latest.size() && i < 2; ++i) {
        const AchRow& r = *latest[i];
        std::string when = WhenText(r.when);
        if (const size_t dot = when.find(" \xC2\xB7"); dot != std::string::npos) when = when.substr(0, dot);
        reward_row([&](ImVec2 p) { AchIcon(d, st, p, st(36), r.info.image_id, true, 0); }, "Achievement: " + r.info.label,
                   r.info.description, when);
      }
      int got = 0, score = 0, total = 0;
      for (const AchRow& r : ach) total += int(r.info.gamerscore), got += r.got, score += r.got ? int(r.info.gamerscore) : 0;
      y += st(10);
      PaperLabel(d, st, ImVec2(x0, y), w, "ACHIEVEMENTS");
      y += st(24);
      d->AddText(f.ui_bold, st(24), ImVec2(x0, y), kit::kInk, std::to_string(got).c_str());
      d->AddText(f.ui_bold, st(14), ImVec2(x0 + kit::TextWidth(f.ui_bold, st(24), std::to_string(got)) + st(5), y + st(9)), kit::kInkSection,
                 fmt::format("of {}", ach.size()).c_str());
      const std::string gs = std::to_string(score), gt = fmt::format("/ {} G", total);
      const float gtw = kit::TextWidth(f.ui_bold, st(14), gt);
      d->AddText(f.ui_bold, st(24), ImVec2(x0 + w - gtw - st(5) - kit::TextWidth(f.ui_bold, st(24), gs), y), kit::kInk, gs.c_str());
      d->AddText(f.ui_bold, st(14), ImVec2(x0 + w - gtw, y + st(9)), kit::kInkSection, gt.c_str());
      ProgBar(d, ImVec2(x0, y + st(36)), ImVec2(x0 + w, y + st(44)), total ? float(score) / float(total) : 0);
      y += st(62);
      PaperLabel(d, st, ImVec2(x0, y), w, "NEXT UP");
      y += st(22);
      std::vector<const AchRow*> next;
      for (const AchRow& r : ach)  // not the ones our count says are done: the game decides
        if (!r.got && !(r.need && r.have >= r.need)) next.push_back(&r);
      std::stable_sort(next.begin(), next.end(), [](const AchRow* p, const AchRow* q) {
        const double fp = p->need ? double(p->have) / double(p->need) : -1, fq = q->need ? double(q->have) / double(q->need) : -1;
        return fp > fq;
      });
      for (size_t i = 0; i < next.size() && i < 2; ++i) {
        const AchRow& r = *next[i];
        reward_row([&](ImVec2 p) { AchIcon(d, st, p, st(36), r.info.image_id, false, 0); }, r.info.label,
                   r.need ? fmt::format("{} of {} \xC2\xB7 {}", r.have, r.need, r.info.description) : r.info.description, "");
      }
      if (next.empty()) d->AddText(f.ui, st(11), ImVec2(x0, y + st(4)), kit::kInkFaint, "Every achievement is yours.");
      if (ach.empty()) d->AddText(f.ui, st(11), ImVec2(x0, y + st(4)), kit::kInkFaint, "The game's achievements load with the game.");
    }
    if (in.prev_tab) {
      page_ = Page::kAchievements, ach_scroll_ = 0, ach_from_pause_ = false;
      return;
    }
    if (in.next_tab) {
      page_ = Page::kLeaderboards, lb_from_pause_ = false, lb_asked_at_ = -100;
      return;
    }
    if (in.back) BackToMenu(kRowStats, 4);  // back on the menu's Stats & Awards row
  }

  // ======================================================== L1: Roll of Honour

  struct BoardRow {
    std::string name, tag;
    long long score = 0, kills = 0, deaths = 0, captures = 0, seconds = 0, headshots = 0, wins = 0, rounds = 0;
    int place = 0;
  };
  static BoardRow ParseBoardRow(const std::string& obj) {
    BoardRow r;
    r.name = JsonField(obj, "name"), r.tag = JsonField(obj, "tag");
    r.score = JsonNumber(obj, "score"), r.kills = JsonNumber(obj, "kills"), r.deaths = JsonNumber(obj, "deaths");
    r.captures = JsonNumber(obj, "captures"), r.seconds = JsonNumber(obj, "seconds"), r.headshots = JsonNumber(obj, "headshots");
    r.wins = JsonNumber(obj, "wins"), r.rounds = JsonNumber(obj, "rounds"), r.place = int(JsonNumber(obj, "place"));
    return r;
  }
  int lb_tab_ = 0, lb_period_ = 0, lb_row_ = 0;
  float lb_scroll_ = 0;

  void LeaderboardsPage(ImDrawList* d, ImGuiIO& io, const Input& in_raw) {
    NotePage("leaderboards");
    Input in = in_raw;
    const Stage st = StageOf(io);
    const kit::Fonts& f = kit::GetFonts();
    static const std::pair<const char*, const char*> kTabs[] = {{"Score", "score"}, {"Kills", "kills"}, {"K / D", "kd"},
                                                                 {"Flags", "captures"}, {"Time", "seconds"}};
    static const std::pair<const char*, const char*> kPeriods[] = {{"ALL TIME", "all"}, {"THIS MONTH", "month"}, {"THIS WEEK", "week"}};
    constexpr int kTabCount = int(std::size(kTabs));
    MenuBackdrop(d, io.DisplaySize, st);
    bool online_ok = false;
    MenuChrome(d, io, st, OnlineStatus(online_ok), online_ok);
    std::vector<Cue> cues = {{kit::Button::kLB, "STAT"}, {kit::Button::kX, "ALL TIME / MONTH / WEEK"}, {kit::Button::kB, "BACK"}};
    Prompts(d, st, st.P(48, 662), cues, in, 15);
    MenuFoot(d, st);
    if (in.prev_tab) lb_tab_ = (lb_tab_ + kTabCount - 1) % kTabCount;
    if (in.next_tab) lb_tab_ = (lb_tab_ + 1) % kTabCount;
    if (in.x) lb_period_ = (lb_period_ + 1) % 3;
    if (in.prev_tab || in.next_tab || in.x) lb_row_ = 0, lb_scroll_ = 0;
    // The board, asked again every 15 s while the page is open (and at once when the stat or the period changes).
    const std::string me = settings_.Get("player_name", "");
    const std::string board_key = std::string(kTabs[lb_tab_].second) + "|" + kPeriods[lb_period_].second;
    if (lb_asked_at_ < 0) prog_board_.asked_at = -100, lb_asked_at_ = ImGui::GetTime();
    ProgAsk(prog_board_, board_key,
            fmt::format("{{\"op\": \"leaderboard\", \"sort\": \"{}\", \"period\": \"{}\", \"me\": {}}}", kTabs[lb_tab_].second,
                        kPeriods[lb_period_].second, JsonQ(me)),
            15);
    std::vector<BoardRow> rows;
    BoardRow mine;
    bool have_mine = false, ok = false, answered = false;
    int total = 0;
    {
      std::lock_guard lock(prog_board_.mutex);
      ok = prog_board_.ok && prog_board_.key == board_key, answered = prog_board_.answered && prog_board_.key == board_key;
      if (ok) {
        const std::string& reply = prog_board_.reply;
        total = int(JsonNumber(reply, "total"));
        const size_t rows_at = reply.find("\"rows\"");
        const size_t rows_end = reply.find(']', rows_at == std::string::npos ? 0 : rows_at);
        size_t at = rows_at;
        while (at != std::string::npos && rows.size() < 50) {
          const size_t a = reply.find('{', at), b = a == std::string::npos ? a : reply.find('}', a);
          if (a == std::string::npos || b == std::string::npos || (rows_end != std::string::npos && a > rows_end)) break;
          BoardRow r = ParseBoardRow(reply.substr(a, b - a + 1));
          if (!r.name.empty()) {
            if (!r.place) r.place = int(rows.size()) + 1;
            rows.push_back(r);
          }
          at = b + 1;
        }
        if (const size_t m = reply.find("\"me\": {"); m != std::string::npos) {
          mine = ParseBoardRow(reply.substr(m + 6, reply.find('}', m) - m - 5));
          have_mine = !mine.name.empty();
        }
      }
    }
    // ---- the title block
    kit::Spaced(d, f.typed, st(10.5f), st.P(48, 91), kit::kKhakiDim,
                !answered ? "ASKING THE SERVERS..."
                : !ok     ? "THE SERVERS ARE NOT REACHABLE RIGHT NOW"
                          : fmt::format("EVERY PLAYER ON THE REBORN SERVERS \xC2\xB7 {} {}", Thousands(total), total == 1 ? "SOLDIER" : "SOLDIERS"),
                st(2));
    kit::GameText(d, kit::GameFont::kHeadline, st(40), st.P(48, 105), kit::kCream, "LEADERBOARDS");
    // ---- the folder tabs, one a stat
    {
      const float y0 = st.P(0, 170).y, h = st(34), size = st(13);
      std::vector<std::pair<ImVec2, ImVec2>> boxes;
      float x = st.P(48, 0).x;
      for (int t = 0; t < kTabCount; ++t) {
        const float w = st(22) + kit::TextWidth(f.ui_bold, size, kTabs[t].first) + st(22);
        boxes.push_back({ImVec2(x, y0), ImVec2(x + w, y0 + h)});
        x += w - st(3);
      }
      auto draw_tab = [&](int t) {
        const bool cur = t == lb_tab_;
        const ImVec2 a(boxes[t].first.x, boxes[t].first.y + (cur ? 0 : st(4))), b2 = boxes[t].second;
        const bool over = !cur && ImGui::IsMouseHoveringRect(a, b2);
        kit::FolderTab(d, a, b2, st.u, cur, over);
        d->AddText(f.ui_bold, size, ImVec2(a.x + st(22), a.y + (b2.y - a.y - size * 1.45f) * 0.5f), cur ? kit::kInk : IM_COL32(34, 36, 27, 215),
                   kTabs[t].first);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) lb_tab_ = t, lb_row_ = 0, lb_scroll_ = 0;
      };
      for (int t = kTabCount - 1; t >= 0; --t)
        if (t != lb_tab_) draw_tab(t);
      draw_tab(lb_tab_);
      // The periods, as chips from the right.
      float cx = st.P(1232, 0).x;
      for (int p = 2; p >= 0; --p) {
        cx -= ChipWidth(st, kPeriods[p].first);
        bool clicked = false;
        const bool on = p == lb_period_;
        Chip(d, st, ImVec2(cx, st.P(0, 176).y), st(20), kPeriods[p].first, on ? kit::kStrip : kit::kPaper, on ? kit::kStripText : kit::kInk, !on, &clicked);
        if (clicked) lb_period_ = p, lb_row_ = 0, lb_scroll_ = 0;
        cx -= st(8);
      }
    }
    // ---- the table: every column, the board's stat marked; your own row pinned under it
    const ImVec2 ta = st.P(48, 204), tb = st.P(872, 640);
    kit::PaperArt(d, ta, tb);
    const float head = st(30), rh = st(36);
    // #, soldier, score, kills, deaths, K/D, flags, time: their right edges (numbers right aligned), the soldier's left.
    const float colr[8] = {st(40), st(56), st(400), st(474), st(548), st(612), st(680), st(760)};
    static const char* kHeads[8] = {"#", "SOLDIER", "SCORE", "KILLS", "DEATHS", "K/D", "FLAGS", "TIME"};
    static const int kColOfTab[] = {2, 3, 5, 6, 7};
    const int sorted = kColOfTab[lb_tab_];
    {  // the sorted column: a faint band down it and the stamp's line under its head
      const float x1 = ta.x + colr[sorted] + st(10), x0 = ta.x + colr[sorted - 1] + st(14);
      d->AddRectFilled(ImVec2(x0, ta.y), ImVec2(x1, tb.y), IM_COL32(34, 36, 27, 14));
      d->AddRectFilled(ImVec2(x0, ta.y + head - st(3)), ImVec2(x1, ta.y + head), kit::kStamp);
    }
    for (int c = 0; c < 8; ++c) {
      const float w = kit::GameTextWidth(kit::GameFont::kListHeader, st(10), kHeads[c], st(0.6f));
      const float x = c == 1 ? ta.x + colr[1] : ta.x + colr[c] - w;
      kit::GameText(d, kit::GameFont::kListHeader, st(10), ImVec2(x, ta.y + (head - st(10)) * 0.5f), c == sorted ? kit::kInk : kit::kInkFaint,
                    kHeads[c], st(0.6f));
    }
    d->AddRectFilled(ImVec2(ta.x, ta.y + head - st(2)), ImVec2(tb.x, ta.y + head), kit::kInk);
    const int n = int(rows.size());
    if (n) {
      if (in.up) lb_row_ = (lb_row_ + n - 1) % n;
      if (in.down) lb_row_ = (lb_row_ + 1) % n;
    }
    lb_row_ = std::clamp(lb_row_, 0, std::max(0, n - 1));
    const bool pin = have_mine;
    const float top = ta.y + head, bottom = tb.y - (pin ? rh + st(12) : 0), area = bottom - top;
    if (ImGui::IsMouseHoveringRect(ta, tb) && io.MouseWheel != 0) lb_scroll_ -= io.MouseWheel * rh * 2;
    if (in.up || in.down) {
      const float ry = float(lb_row_) * rh;
      if (ry < lb_scroll_) lb_scroll_ = ry;
      if (ry + rh > lb_scroll_ + area) lb_scroll_ = ry + rh - area;
    }
    lb_scroll_ = std::clamp(lb_scroll_, 0.0f, std::max(0.0f, float(n) * rh - area));
    auto draw_row = [&](const BoardRow& r, float y, bool sel, bool is_me) {
      const ImVec2 r0(ta.x, y), r1(tb.x, y + rh);
      if (sel) d->AddRectFilled(r0, r1, kit::kGreen), d->AddRectFilled(r0, ImVec2(r0.x + st(5), r1.y), kit::kStamp);
      else if (is_me) d->AddRectFilled(r0, r1, kit::kInkSoft), d->AddRectFilled(r0, ImVec2(r0.x + st(5), r1.y), kit::kStamp);
      if (sel || is_me) {
        const float x1 = ta.x + colr[sorted] + st(10), x0 = ta.x + colr[sorted - 1] + st(14);
        d->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + rh), sel ? IM_COL32(0, 0, 0, 30) : IM_COL32(255, 255, 255, 12));
      }
      d->AddLine(ImVec2(ta.x, r1.y - 0.5f), ImVec2(tb.x, r1.y - 0.5f), IM_COL32(34, 36, 27, 30), 1.0f);
      const ImU32 ink = sel || is_me ? kit::kCream : kit::kInk;
      const float cap = st(12), ty = y + (rh - cap) * 0.5f;
      auto right = [&](int c, const std::string& t) {
        kit::GameText(d, kit::GameFont::kText, cap, ImVec2(ta.x + colr[c] - kit::GameTextWidth(kit::GameFont::kText, cap, t), ty), ink, t);
      };
      right(0, std::to_string(r.place));
      RankInsignia(d, st, ImVec2(ta.x + colr[1] + st(10), y + rh * 0.5f), st(20), RankOfSeconds(double(r.seconds)));
      float nx = ta.x + colr[1] + st(26);
      if (!r.tag.empty()) {
        const std::string t = "[" + r.tag + "]";
        kit::Spaced(d, f.typed, st(8.5f), ImVec2(nx, y + (rh - st(8.5f) * 1.45f) * 0.5f), sel || is_me ? kit::kKhaki : kit::kInkSection, t, st(0.6f));
        nx += kit::SpacedWidth(f.typed, st(8.5f), t, st(0.6f)) + st(6);
      }
      kit::GameText(d, kit::GameFont::kText, cap, ImVec2(nx, ty), ink, is_me ? r.name + "  (you)" : r.name);
      right(2, Thousands(r.score));
      const bool counted = r.kills || r.deaths;  // a player the servers have not counted yet: their own totals only
      right(3, counted ? Thousands(r.kills) : "\xE2\x80\x94");
      right(4, counted ? Thousands(r.deaths) : "\xE2\x80\x94");
      right(5, KdText(r.kills, r.deaths));
      right(6, Thousands(r.captures));
      right(7, fmt::format("{} h", r.seconds / 3600));
    };
    d->PushClipRect(ImVec2(ta.x, top), ImVec2(tb.x, bottom), true);
    int clicked = -1;
    for (int i = 0; i < n; ++i) {
      const float y = top + float(i) * rh - lb_scroll_;
      if (y + rh < top || y > bottom) continue;
      const bool is_me = !me.empty() && Lower(rows[size_t(i)].name) == Lower(me);
      draw_row(rows[size_t(i)], y, i == lb_row_, is_me && i != lb_row_);
      if (ImGui::IsMouseHoveringRect(ImVec2(ta.x, y), ImVec2(tb.x, y + rh)) && ImGui::IsMouseHoveringRect(ImVec2(ta.x, top), ImVec2(tb.x, bottom)) &&
          ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        clicked = i;
    }
    d->PopClipRect();
    if (clicked >= 0) lb_row_ = clicked;
    if (pin) {
      const float y = tb.y - rh - st(4);
      kit::Spaced(d, f.typed, st(9), ImVec2(ta.x + (tb.x - ta.x) * 0.5f - st(14), y - st(10)), kit::kInkSection, "\xC2\xB7 \xC2\xB7 \xC2\xB7", st(3));
      draw_row(mine, y, false, true);
    }
    if (!n) {
      const std::string why = !answered ? "Asking the servers..."
                              : !ok     ? "The leaderboard is not reachable right now. It is asked again every 15 seconds."
                              : lb_period_ ? "Nobody has played on the Reborn servers in this period yet."
                                           : "Nobody on this board yet: play a match on a Reborn server.";
      kit::Wrapped(d, f.ui, st(13.75f), ImVec2(ta.x + st(20), top + st(20)), kit::kInkFaint, why, tb.x - ta.x - st(40));
    }
    // ---- the chosen soldier's card
    const ImVec2 pa = st.P(887, 204), pb = st.P(1232, 640);
    kit::PaperArt(d, pa, pb);
    const BoardRow* sel = n ? &rows[size_t(lb_row_)] : have_mine ? &mine : nullptr;
    ProgCard card;
    if (sel) {
      ProgAsk(prog_other_, sel->name, "{\"op\": \"player_card\", \"name\": " + JsonQ(sel->name) + "}", 30);
      std::lock_guard lock(prog_other_.mutex);
      if (prog_other_.ok && prog_other_.key == sel->name) card = ParseCard(prog_other_.reply);
    }
    const ImVec2 p1(pb.x, pa.y + st(158));
    d->AddRectFilled(pa, p1, kit::kStrip);
    d->PushClipRect(pa, p1, true);
    kit::Art(d, "backdrop", pa, p1);
    d->AddRectFilledMultiColor(ImVec2(pa.x, p1.y - st(100)), p1, IM_COL32(20, 20, 16, 0), IM_COL32(20, 20, 16, 0), IM_COL32(20, 20, 16, 230),
                               IM_COL32(20, 20, 16, 230));
    const char* kit_art = "portrait_us_infantry";
    if (sel) {
      const long long ki = card.Get("kit_infantry"), kr = card.Get("kit_rifleman"), ks = card.Get("kit_scout");
      kit_art = kr > ki && kr >= ks ? "portrait_us_rifleman" : ks > ki && ks > kr ? "portrait_us_scout" : "portrait_us_infantry";
      kit::ArtAt(d, kit_art, ImVec2(pa.x + st(10), p1.y - st(76) + st(6)), st(156));
    }
    d->PopClipRect();
    if (sel) {
      const int r = RankOfSeconds(double(std::max(sel->seconds, card.Get("seconds"))));
      RankInsignia(d, st, ImVec2(pb.x - st(42), pa.y + st(40)), st(66), r);
      const float tx = pa.x + st(124);
      kit::Spaced(d, f.typed, st(8.5f), ImVec2(tx, p1.y - st(62)), kit::kKhaki,
                  fmt::format("#{} \xC2\xB7 {}", sel->place, Upper(kTabs[lb_tab_].first)), st(2.4f));
      d->AddText(f.ui_bold, st(21), ImVec2(tx, p1.y - st(50)), kit::kCream, sel->name.c_str());
      kit::Spaced(d, f.typed, st(8), ImVec2(tx, p1.y - st(20)), kit::kKhakiDim, fmt::format("RANK {} \xC2\xB7 {}", r, Upper(kRanks[r - 1])), st(2));
      const float x0 = pa.x + st(17), w = pb.x - pa.x - st(34);
      float y = p1.y + st(12);
      const long long kills = std::max(sel->kills, card.Get("kills")), deaths = std::max(sel->deaths, card.Get("deaths"));
      const std::pair<const char*, std::string> big[] = {{"KILLS", Thousands(kills)}, {"K / D", KdText(kills, deaths)},
                                                         {"SCORE", Thousands(std::max(sel->score, card.Get("score")))}};
      for (int i = 0; i < 3; ++i) {
        const float x = x0 + w / 3 * float(i);
        kit::Spaced(d, f.typed, st(8), ImVec2(x, y), kit::kInkSection, big[i].first, st(2.2f));
        d->AddText(f.ui_bold, st(23), ImVec2(x, y + st(10)), kit::kInk, big[i].second.c_str());
      }
      y += st(44);
      d->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + st(1.5f)), kit::kInk);
      y += st(4);
      auto kv = [&](const std::string& k, const std::string& v) {
        d->AddText(f.ui, st(12), ImVec2(x0, y + st(6)), kit::kInkFaint, k.c_str());
        d->AddText(f.ui_bold, st(12), ImVec2(x0 + w - kit::TextWidth(f.ui_bold, st(12), v), y + st(6)), kit::kInk, v.c_str());
        d->AddRectFilled(ImVec2(x0, y + st(29)), ImVec2(x0 + w, y + st(30)), kit::kRule);
        y += st(30);
      };
      const char* dash = "\xE2\x80\x94";
      {
        const long long ki = card.Get("kit_infantry"), kr = card.Get("kit_rifleman"), ks = card.Get("kit_scout");
        kv("Favourite kit", !(ki || kr || ks) ? dash : kr > ki && kr >= ks ? "Rifleman" : ks > ki && ks > kr ? "Scout" : "Infantry");
      }
      {
        const std::pair<const char*, const char*> v[] = {{"veh_plane", "Fighter"}, {"veh_tank", "Tank"}, {"veh_jeep", "Jeep"}, {"veh_boat", "Boat"}};
        const char* best = nullptr;
        long long most = 0;
        for (const auto& [k, name] : v)
          if (card.Get(k) > most) most = card.Get(k), best = name;
        const std::string vk = best ? std::string("vk_") + (std::string(best) == "Fighter" ? "plane" : Lower(best)) : "";
        kv("Favourite vehicle", best ? fmt::format("{} \xC2\xB7 {} kills", best, card.Get(vk.c_str())) : std::string(dash));
      }
      kv("Headshots", card.found ? Thousands(card.Get("headshots")) : dash);
      kv("Rounds won", card.found && card.Get("rounds") ? fmt::format("{} of {}", card.Get("wins"), card.Get("rounds")) : std::string(dash));
      {
        const char* best = nullptr;
        long long most = 0;
        for (const auto& [m, row] : card.maps)
          if (row.wins > most) most = row.wins, best = m == "wake" ? "Wake Island" : m == "iwo" ? "Iwo Jima" : m == "guadalcanal" ? "Guadalcanal" : "Coral Sea";
        kv("Best map", best ? best : dash);
      }
      kv("Time in the field", HoursMinutes(std::max(sel->seconds, card.Get("seconds"))));
    } else {
      kit::Wrapped(d, f.ui, st(13), ImVec2(pa.x + st(17), p1.y + st(16)), kit::kInkFaint, "Choose a soldier in the list.", pb.x - pa.x - st(34));
    }
    if (in.back) {
      if (lb_from_pause_) page_ = Page::kPause, pause_row_ = kPauseLeaderboards, lb_from_pause_ = false;
      else BackToMenu(kRowLeaderboards, 7);
    }
  }

  // ======================================================== M1: Medal Case

  int ach_filter_ = 0, ach_row_ = 0;  // all, unlocked, locked; the chosen one

  void AchievementsPage(ImDrawList* d, ImGuiIO& io, const Input& in_raw) {
    NotePage("achievements");
    Input in = in_raw;
    const Stage st = StageOf(io);
    const kit::Fonts& f = kit::GetFonts();
    MenuBackdrop(d, io.DisplaySize, st);
    bool online_ok = false;
    MenuChrome(d, io, st, OnlineStatus(online_ok), online_ok);
    std::vector<Cue> cues = {{kit::Button::kX, "ALL / UNLOCKED / LOCKED"}, {kit::Button::kY, "TEST THE POPUP"}, {kit::Button::kB, "BACK"}};
    Prompts(d, st, st.P(48, 662), cues, in, 15);
    MenuFoot(d, st);
    bool servers = false;
    const ProgCard c = MyRecord(servers);
    std::vector<AchRow> all = AchievementRows(c);
    int got = 0, score = 0, total = 0;
    for (const AchRow& r : all) total += int(r.info.gamerscore), got += r.got, score += r.got ? int(r.info.gamerscore) : 0;
    // The players with each (the backend's accounts), asked once a minute.
    ProgAsk(prog_share_, "share", "{\"op\": \"achievement_share\"}", 60);
    std::map<uint32_t, int> share;  // id -> percent
    {
      std::lock_guard lock(prog_share_.mutex);
      if (prog_share_.ok) {
        const long long players = JsonNumber(prog_share_.reply, "players");
        const size_t have = prog_share_.reply.find("\"have\"");
        if (players > 0 && have != std::string::npos) {
          const std::string obj = prog_share_.reply.substr(have);
          for (const AchRow& r : all) {
            const std::string k = std::to_string(r.info.id);
            if (const std::string v = JsonField(obj, k.c_str()); !v.empty())
              share[r.info.id] = int(std::llround(100.0 * double(std::atoll(v.c_str())) / double(players)));
          }
        }
      }
    }
    if (in.x) ach_filter_ = (ach_filter_ + 1) % 3, ach_row_ = 0;
    // Unlocked first (the newest first), then the locked ones by how far along.
    std::vector<const AchRow*> list;
    for (const AchRow& r : all)
      if (ach_filter_ == 0 || (ach_filter_ == 1) == r.got) list.push_back(&r);
    std::stable_sort(list.begin(), list.end(), [](const AchRow* p, const AchRow* q) {
      if (p->got != q->got) return p->got;
      if (p->got) return p->info.id < q->info.id;
      const double fp = p->need ? double(p->have) / double(p->need) : -1, fq = q->need ? double(q->have) / double(q->need) : -1;
      return fp > fq;
    });
    // Two columns of six rows on screen; with more than 12 (the game's 12 and Reborn's own) each column is longer and
    // scrolls: the chosen row is kept in sight, the mouse wheel scrolls too.
    constexpr int kShown = 6;
    const int n = int(list.size()), per_col = std::max(kShown, (n + 1) / 2);
    const bool moved = in.up || in.down || in.left || in.right;
    if (n) {  // up and down within a column, left and right across
      if (in.up) ach_row_ = (ach_row_ + n - 1) % n;
      if (in.down) ach_row_ = (ach_row_ + 1) % n;
      if (in.left && ach_row_ >= per_col) ach_row_ -= per_col;
      if (in.right && ach_row_ + per_col < n) ach_row_ += per_col;
    }
    ach_row_ = std::clamp(ach_row_, 0, std::max(0, n - 1));
    if (moved) {
      const int k = ach_row_ % per_col;
      if (k < ach_scroll_) ach_scroll_ = k;
      if (k >= ach_scroll_ + kShown) ach_scroll_ = k - kShown + 1;
    }
    if (io.MouseWheel != 0 && ImGui::IsMouseHoveringRect(st.P(48, 237), st.P(1232, 640)))
      ach_scroll_ -= io.MouseWheel > 0 ? 1 : -1;
    ach_scroll_ = std::clamp(ach_scroll_, 0, per_col - kShown);
    if (in.y && n) TestAchievementPopup(list[size_t(ach_row_)]->info.id);
    kit::Spaced(d, f.typed, st(10.5f), st.P(48, 91), kit::kKhakiDim,
                test_profile_active_ ? "BATTLEFIELD 1943 \xC2\xB7 TEST PROFILE"
                : NameProtected()   ? "BATTLEFIELD 1943 \xC2\xB7 THE GAME'S OWN 12 AND REBORN'S 3 \xC2\xB7 SAVED TO YOUR NAME"
                                    : "BATTLEFIELD 1943 \xC2\xB7 THE GAME'S OWN 12 AND REBORN'S 3 \xC2\xB7 SAVED ON THIS PC",
                st(2));
    kit::GameText(d, kit::GameFont::kHeadline, st(40), st.P(48, 105), kit::kCream, "ACHIEVEMENTS");
    // ---- the summary strip
    {
      const ImVec2 a = st.P(48, 159), b = st.P(1232, 228);
      kit::PaperArt(d, a, b);
      float x = a.x + st(19);
      const float y = a.y + st(15);
      auto block = [&](const char* label, const std::string& big, const std::string& unit) {
        kit::Spaced(d, f.typed, st(8), ImVec2(x, y), kit::kInkSection, label, st(2.2f));
        d->AddText(f.ui_bold, st(25), ImVec2(x, y + st(10)), kit::kInk, big.c_str());
        const float bw = kit::TextWidth(f.ui_bold, st(25), big);
        d->AddText(f.ui_bold, st(14), ImVec2(x + bw + st(5), y + st(20)), kit::kInkSection, unit.c_str());
        x += std::max(bw + st(5) + kit::TextWidth(f.ui_bold, st(14), unit), kit::SpacedWidth(f.typed, st(8), label, st(2.2f))) + st(30);
      };
      block("UNLOCKED", std::to_string(got), fmt::format("of {}", all.size()));
      block("GAMERSCORE", std::to_string(score), fmt::format("/ {} G", total));
      const float bx1 = x + st(330);
      ProgBar(d, ImVec2(x, y + st(8)), ImVec2(bx1, y + st(17)), total ? float(score) / float(total) : 0);
      std::string next;
      for (const AchRow* r : list)
        if (!r->got && r->need && r->have < r->need) {
          next = fmt::format("NEXT: {} \xC2\xB7 {} OF {}", Upper(r->info.label), r->have, r->need);
          break;
        }
      if (next.empty()) next = !ach_has_games_ ? "THE GAME'S OWN 12 SHOW ONCE IT HAS STARTED"
                             : got == int(all.size()) && got ? "EVERY ONE OF THEM IS YOURS" : "";
      kit::Spaced(d, f.typed, st(8), ImVec2(x, y + st(27)), kit::kInkSection, next, st(2.2f));
      float cx = b.x - st(19);
      const std::string chips[3] = {fmt::format("ALL {}", all.size()), fmt::format("UNLOCKED {}", got), fmt::format("LOCKED {}", int(all.size()) - got)};
      for (int i = 2; i >= 0; --i) {
        cx -= ChipWidth(st, chips[i]);
        bool clicked = false;
        const bool on = i == ach_filter_;
        Chip(d, st, ImVec2(cx, (a.y + b.y) * 0.5f - st(10)), st(20), chips[i], on ? kit::kStrip : kit::kPaper, on ? kit::kStripText : kit::kInk, !on, &clicked);
        if (clicked) ach_filter_ = i, ach_row_ = 0;
        cx -= st(6);
      }
    }
    // ---- the two columns
    const float rh = st(64);
    for (int col = 0; col < 2; ++col) {
      const ImVec2 a = st.P(col ? 648 : 48, 237), b = st.P(col ? 1232 : 632, 640);
      kit::PaperArt(d, a, b);
      for (int k = 0; k < kShown; ++k) {
        const int i = col * per_col + ach_scroll_ + k;
        if (ach_scroll_ + k >= per_col || i >= n) break;
        const AchRow& r = *list[size_t(i)];
        const float y = a.y + float(k) * rh;
        const ImVec2 r0(a.x, y), r1(b.x, y + rh);
        const bool sel = i == ach_row_;
        const bool hover = ImGui::IsMouseHoveringRect(r0, r1);
        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ach_row_ = i;
        if (sel) d->AddRectFilled(r0, r1, kit::kGreen), d->AddRectFilled(r0, ImVec2(r0.x + st(5), r1.y), kit::kStamp);
        else if (hover) d->AddRectFilled(r0, r1, IM_COL32(34, 36, 27, 14));
        d->AddRectFilled(ImVec2(a.x, r1.y - 1), ImVec2(b.x, r1.y), kit::kRule);
        AchIcon(d, st, ImVec2(a.x + st(10), y + st(8)), st(48), r.info.image_id, r.got);
        const float tx = a.x + st(73), gw = st(70);
        const ImU32 ink = sel ? kit::kCream : r.got ? kit::kInk : kit::kInkSection;
        d->PushClipRect(ImVec2(tx, y), ImVec2(b.x - gw - st(8), y + rh), true);
        d->AddText(f.ui_bold, st(14), ImVec2(tx, y + st(7)), ink, r.info.label.c_str());
        if (reborn_achievements::IsOurs(r.info.id))  // ours, beside the game's (reborn_achievements.h)
          kit::Spaced(d, f.typed, st(7), ImVec2(tx + kit::TextWidth(f.ui_bold, st(14), r.info.label) + st(8), y + st(11)),
                      sel ? kit::kCream : kit::kStamp, "REBORN", st(1.4f));
        const std::string& desc = r.got || r.info.unachieved_description.empty() ? r.info.description : r.info.unachieved_description;
        d->AddText(f.ui, st(10.5f), ImVec2(tx, y + st(26)), sel ? kit::kCream : kit::kInkFaint, desc.c_str());
        const ImU32 faint = sel ? kit::kCream : kit::kInkSection;
        if (r.got) {
          kit::Spaced(d, f.typed, st(7.5f), ImVec2(tx, y + st(46)), faint, "UNLOCKED " + WhenText(r.when), st(1.6f));
        } else if (r.need) {
          ProgBar(d, ImVec2(tx, y + st(49)), ImVec2(tx + st(100), y + st(53)), float(r.have) / float(r.need), sel ? kit::kCream : kit::kGreen,
                  sel ? IM_COL32(255, 255, 255, 60) : IM_COL32(34, 36, 27, 34));
          kit::Spaced(d, f.typed, st(7.5f), ImVec2(tx + st(108), y + st(46)), faint, fmt::format("{} / {}", r.have, r.need), st(1.6f));
        } else {
          kit::Spaced(d, f.typed, st(7.5f), ImVec2(tx, y + st(46)), faint, "LOCKED", st(1.6f));
        }
        d->PopClipRect();
        const std::string g = fmt::format("{} G", r.info.gamerscore);
        d->AddText(f.ui_bold, st(16), ImVec2(b.x - st(15) - kit::TextWidth(f.ui_bold, st(16), g), y + st(15)), sel ? kit::kCream : r.got ? kit::kInk : kit::kInkSection,
                   g.c_str());
        if (auto it = share.find(r.info.id); it != share.end()) {
          const std::string h = fmt::format("{} % HAVE IT", it->second);
          kit::Spaced(d, f.typed, st(7), ImVec2(b.x - st(15) - kit::SpacedWidth(f.typed, st(7), h, st(1.4f)), y + st(38)), faint, h, st(1.4f));
        }
      }
    }
    if (per_col > kShown) {  // the scroll's place, at the right column's foot, with an arrow each way
      const std::string where = fmt::format("ROWS {} TO {} OF {}", ach_scroll_ + 1, ach_scroll_ + kShown, per_col);
      const float y = st.P(0, 640).y - st(14), x1 = st.P(1232, 0).x - st(15);
      const float x0 = x1 - kit::SpacedWidth(f.typed, st(7), where, st(1.4f));
      kit::Spaced(d, f.typed, st(7), ImVec2(x0, y), kit::kInkSection, where, st(1.4f));
      const bool up = ach_scroll_ > 0, down = ach_scroll_ < per_col - kShown;
      const float ax = x0 - st(30), ay = y + st(4.5f), s = st(4);
      d->AddTriangleFilled(ImVec2(ax, ay + s * 0.6f), ImVec2(ax + s * 2, ay + s * 0.6f), ImVec2(ax + s, ay - s * 0.8f), up ? kit::kInk : kit::kRule);
      d->AddTriangleFilled(ImVec2(ax + st(13), ay - s * 0.6f), ImVec2(ax + st(13) + s * 2, ay - s * 0.6f), ImVec2(ax + st(13) + s, ay + s * 0.8f), down ? kit::kInk : kit::kRule);
    }
    // Before the game has ever run on this user folder there is no copy of its catalog yet: Reborn's own only.
    if (!all.empty() && !ach_has_games_ && ach_filter_ != 1)
      kit::Wrapped(d, f.ui, st(13.75f), st.P(668, 257), kit::kInkFaint,
                   "The game's own 12 are listed here once the game has started: join a match once, and they stay here "
                   "from then on.", st(540));
    if (all.empty())
      kit::Wrapped(d, f.ui, st(13.75f), st.P(68, 257), kit::kInkFaint, "The game's achievements load with the game: open this page again in a moment.",
                   st(540));
    else if (!n)
      kit::Wrapped(d, f.ui, st(13.75f), st.P(68, 257), kit::kInkFaint, ach_filter_ == 1 ? "None unlocked yet." : "Every achievement is yours.", st(540));
    if (in.back) {  // back where it was opened: the pause, or the main menu's Achievements row (issue 19)
      if (ach_from_pause_) page_ = Page::kPause, pause_row_ = kPauseAchievements, ach_from_pause_ = false;
      else BackToMenu(kRowAchievements, 6);
    }
  }

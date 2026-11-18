// The news window (TekRantGaming, 15 Nov 2026: "a news window that opens on the main menu at every launch, like the
// news screens other games use for their shops and battle passes"; his pick of two mockups: N1 "Dispatch Board",
// runs/ui-designs/build_news.py n1_news, "build this one"). Included inside GameMenu (game_menu.cpp), after
// game_menu_home.inc. docs/NEWS.md has the feed and the rules.
//
// A paper window over the main menu: the lead story at the top left (its YouTube video playing in the embedded
// player, news_video.cpp, or its picture), its title, text and buttons beside it with the Reborn servers' state live
// from the server list, the other items as cards under it, the coming up list at the right. The mockup's 1600 x 900
// at (160, 90) on 1920 x 1080 is (107, 60) to (1173, 660) on the 1280 x 720 Stage.
//
// When: once a run, on the first main menu, when this run's feed has arrived (news.cpp: never the cached one, so a
// PC offline starts as before) within 30 seconds and the player is still there; never over another pop-up (What's
// new after an update first: the news waits for it to close), never in a scripted test run (BF_DEV_NEWS=1 asks).
// The News row opens it again (the cached feed when this run's did not come).

  bool news_open_ = false, news_auto_done_ = false, news_started_ = false;
  double news_menu_first_ = -1, news_menu_last_ = -1;
  std::shared_ptr<const news::Feed> news_feed_;
  std::vector<int> news_order_;  // the items' indices: [0] in the lead, the rest the cards in their order
  int news_focus_ = 0;           // 0 the lead's button, n > 0 the card news_order_[n]
  int news_first_card_ = 1;      // the first card shown (the row scrolls)
  bool news_muted_ = false;
  int news_new_count_ = 0;
  std::set<std::string> news_new_ids_;
  // The News row's "3 NEW": counted again only when the feed or the seen list changes.
  const news::Feed* news_unseen_for_ = nullptr;
  std::string news_unseen_seen_;
  int news_unseen_ = 0;

  static std::string NewsKey(const std::string& id) {  // an item's id as kept in the settings (short, no commas)
    uint32_t h = 2166136261u;
    for (unsigned char c : id) h = (h ^ c) * 16777619u;
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return buf;
  }
  std::set<std::string> NewsSeen() {
    std::set<std::string> seen;
    const std::string s = settings_.Get("news_seen", "");
    for (size_t at = 0; at < s.size();) {
      const size_t end = std::min(s.find(',', at), s.size());
      if (end > at) seen.insert(s.substr(at, end - at));
      at = end + 1;
    }
    return seen;
  }
  std::shared_ptr<const news::Feed> NewsFeedNow() {
    auto f = news::Fresh();
    return f ? f : news::Cached();
  }
  int NewsUnseen() {
    const auto f = NewsFeedNow();
    if (!f) return 0;
    const std::string seen_s = settings_.Get("news_seen", "");
    if (f.get() != news_unseen_for_ || seen_s != news_unseen_seen_) {
      news_unseen_for_ = f.get(), news_unseen_seen_ = seen_s;
      const auto seen = NewsSeen();
      news_unseen_ = 0;
      for (const news::Item& it : f->items) news_unseen_ += !seen.count(NewsKey(it.id));
    }
    return news_unseen_;
  }

  // Every frame on the main menu (MenuPage): the feed's worker once, the automatic opening.
  void NewsTick() {
    const double now = ImGui::GetTime();
    if (!news_started_) {
      news_started_ = true, news_menu_first_ = now;
      news::Start(UserDir() / "cache");
#if defined(BF_DEV_TOOLS)
      const char* scripted = std::getenv("BF_DEV_SCRIPTED");
      const char* ask = std::getenv("BF_DEV_NEWS");
      if (scripted && *scripted == '1' && !(ask && *ask == '1')) {
        news_auto_done_ = true;
        REXLOG_INFO("BF news: a scripted test run: the news does not open by itself (BF_DEV_NEWS=1 asks)");
      }
#endif
    }
    if (!news_open_) news::VideoHide();
    const bool came_back = news_menu_last_ >= 0 && now - news_menu_last_ > 0.5;  // another page was up meanwhile
    news_menu_last_ = now;
    if (news_auto_done_ || news_open_) return;
    // Never over another pop-up; their time does not count.
    if (whats_new_open_ || update_popup_ || new_version_popup_ || notice_popup_ || quit_popup_ || steam_ask_ || steam_job_ ||
        own_popup_ || exp_popup_ || tick_popup_ || pw_popup_) {
      news_menu_first_ = now;
      return;
    }
    if (came_back || now - news_menu_first_ > 30) {
      news_auto_done_ = true;
      REXLOG_INFO("BF news: not opened by itself ({})", came_back ? "the player left the main menu first" : "no feed within 30 s");
      return;
    }
    const auto fresh = news::Fresh();
    if (!fresh) {
      if (news::Done()) {
        news_auto_done_ = true;
        REXLOG_INFO("BF news: no feed this run: the menu as it is");
      }
      return;
    }
    if (whats_new_pending_) return;  // What's new after an update comes first
    news_auto_done_ = true;
    if (fresh->items.empty() && fresh->upcoming.empty()) {
      REXLOG_INFO("BF news: the feed has nothing to show: not opened");
      return;
    }
    OpenNews("the first main menu of this run");
  }

  bool OpenNews(const char* why) {
    news_feed_ = NewsFeedNow();
    if (!news_feed_) {
      menu_error_ = news::Done() ? "No news right now: it could not be reached." : "The news is on its way. Try again in a moment.";
      menu_error_at_ = ImGui::GetTime();
      REXLOG_INFO("BF news: News asked for ({}): no feed yet", why);
      return false;
    }
    news_order_.clear();
    for (int i = 0; i < int(news_feed_->items.size()); ++i) news_order_.push_back(i);
    news_focus_ = 0, news_first_card_ = 1;
    news_muted_ = settings_.Get("news_muted", "0") == "1";
    // New since the last visit: counted now, then every item counts as seen (the newest 64 ids kept).
    const auto seen = NewsSeen();
    news_new_ids_.clear();
    std::string keep;
    int kept = 0;
    for (const news::Item& it : news_feed_->items) {
      const std::string k = NewsKey(it.id);
      if (!seen.count(k)) news_new_ids_.insert(it.id);
      if (kept++ < 64) keep += (keep.empty() ? "" : ",") + k;
    }
    for (const std::string& k : seen)
      if (kept < 64 && keep.find(k) == std::string::npos) keep += (keep.empty() ? "" : ",") + k, ++kept;
    news_new_count_ = int(news_new_ids_.size());
    settings_.values["news_seen"] = keep;
    settings_.Save();
    news_open_ = true;
    REXLOG_INFO("BF news: opened ({}): {} items, {} new, {} coming up{}", why, news_feed_->items.size(), news_new_count_,
                news_feed_->upcoming.size(), news_feed_ == news::Fresh() ? "" : " (the cached feed)");
    return true;
  }
  void CloseNews(const char* how) {
    news_open_ = false;
    news::VideoHide();
    REXLOG_INFO("BF news: closed ({})", how);
  }

  // Words wrapped into at most `max_lines` lines (the last one cut with an ellipsis when there is more).
  static std::vector<std::string> NewsWrap(ImFont* f, float size, const std::string& text, float width, int max_lines) {
    std::vector<std::string> lines;
    std::string line;
    bool more = false;
    for (size_t i = 0; i <= text.size() && !more;) {
      const size_t end = std::min(text.find_first_of(" \n", i), text.size());
      const std::string word = text.substr(i, end - i);
      const std::string tried = line.empty() ? word : line + " " + word;
      if (!line.empty() && kit::TextWidth(f, size, tried) > width) {
        lines.push_back(line), line = word;
      } else {
        line = tried;
      }
      if (end < text.size() && text[end] == '\n' && !line.empty()) lines.push_back(line), line.clear();
      if (int(lines.size()) >= max_lines) more = true;
      i = end + 1;
    }
    if (!more && !line.empty()) lines.push_back(line);
    if (int(lines.size()) > max_lines) lines.resize(size_t(max_lines)), more = true;
    if (more && !lines.empty()) {
      std::string& last = lines.back();
      while (!last.empty() && kit::TextWidth(f, size, last + "...") > width) last.pop_back();
      last += "...";
    }
    return lines;
  }
  static std::string NewsFit(ImFont* f, float size, std::string t, float width) {
    if (kit::TextWidth(f, size, t) <= width) return t;
    while (!t.empty() && kit::TextWidth(f, size, t + "...") > width) t.pop_back();
    return t + "...";
  }
  static std::string NewsKicker(const news::Item& it) {
    std::string k = it.kicker;
    if (k.empty()) {
      k = it.kind == "video" ? "Video" : it.kind == "update" ? "Patch notes" : it.kind == "event" ? "Upcoming" : "News";
      if (const std::string d = news::ShortDate(it.date); !d.empty()) k += " \xC2\xB7 " + d;
    }
    return Upper(k);
  }
  // The item's picture over a..b (its image, else its video's thumbnail), the menu's own backdrop until it is there.
  void NewsPicture(ImDrawList* d, const news::Item& it, ImVec2 a, ImVec2 b, int max_width) {
    if (!news::PictureUrl(it).empty() && kit::DrawImageFile(d, news::PictureFile(UserDir() / "cache", it), a, b, false, false, max_width))
      return;
    d->AddRectFilled(a, b, IM_COL32(28, 32, 24, 255));
    if (kit::Art(d, "backdrop", a, b)) d->AddRectFilled(a, b, IM_COL32(18, 22, 14, 120));
  }
  // YouTube's play mark (a red rounded box with a white triangle), centred at c, h high.
  static void NewsPlayMark(ImDrawList* d, ImVec2 c, float h) {
    const float w = h * 1.42f;
    d->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), IM_COL32(255, 0, 0, 235), h * 0.24f);
    d->AddTriangleFilled(ImVec2(c.x - h * 0.17f, c.y - h * 0.24f), ImVec2(c.x - h * 0.17f, c.y + h * 0.24f), ImVec2(c.x + h * 0.27f, c.y),
                         IM_COL32(255, 255, 255, 255));
  }
  // The lead's primary action: its link, else its video on YouTube.
  void NewsPrimary(const news::Item& it) {
    if (!it.link.empty()) return OpenUrl(it.link);
    if (!it.youtube.empty()) {
      if (news::VideoShowing() && !news_muted_) news_muted_ = true, news::VideoSetMuted(true);  // not two sounds
      OpenUrl(news::WatchUrl(it.youtube));
    }
  }
  void NewsToggleMute() {
    news_muted_ = !news_muted_;
    news::VideoSetMuted(news_muted_);
    settings_.values["news_muted"] = news_muted_ ? "1" : "0";
    settings_.Save();
    REXLOG_INFO("BF news: video {}", news_muted_ ? "muted" : "sound on");
  }
  void NewsShow(int k) {  // card k into the lead (they swap places)
    if (k <= 0 || k >= int(news_order_.size())) return;
    std::swap(news_order_[0], news_order_[size_t(k)]);
    news_focus_ = 0;
    REXLOG_INFO("BF news: \"{}\" in the lead", news_feed_->items[size_t(news_order_[0])].title);
  }

  void NewsPopup(ImDrawList* d, ImGuiIO& io, Input in) {
    if (!news_feed_) return CloseNews("no feed");
    const Stage st = StageOf(io);
    const kit::Fonts& f = kit::GetFonts();
    const kit::Style style = PromptStyle();
    const news::Feed& feed = *news_feed_;
    auto P = [&](float x, float y) { return st.P(107 + x, 60 + y); };
    const ImVec2 wa = P(0, 0), wb = P(1066, 600);
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool moved = io.MouseDelta.x + io.MouseDelta.y != 0;
    bool close = false;
    // The menu dimmed round it, the window's shadow and paper.
    d->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(10, 12, 8, 184));
    d->AddRectFilled(ImVec2(wa.x + st(4), wa.y + st(10)), ImVec2(wb.x + st(4), wb.y + st(14)), IM_COL32(0, 0, 0, 90));
    kit::PaperArt(d, wa, wb);
    if (clicked && !ImGui::IsMouseHoveringRect(wa, wb)) close = true;  // a click outside
    // ---- the header: FIELD DISPATCH, the edition, the new count, Close
    d->AddRectFilled(wa, P(1066, 56), kit::kStrip);
    d->AddRectFilled(P(0, 56), P(1066, 60), kit::kStamp);
    float hx = P(20, 0).x;
    hx += kit::GameText(d, kit::GameFont::kHeadline, st(25), ImVec2(hx, P(0, 16).y), kit::kCream, "FIELD DISPATCH") + st(20);
    std::string edition = feed.edition;
    if (edition.empty() && !feed.items.empty()) {
      std::string newest;
      for (const news::Item& it : feed.items) newest = std::max(newest, it.date);
      if (!newest.empty()) edition = news::ShortDate(newest) + " edition";
    }
    kit::Spaced(d, f.typed, st(10), ImVec2(hx, P(0, 24).y), kit::kGold, "NEWS" + (edition.empty() ? "" : " \xC2\xB7 " + Upper(edition)), st(2.5f));
    {  // Close: its glyph, the word, the cross; a click
      const float gh = st(18);
      const float gw = kit::Glyph(nullptr, style, kit::Button::kB, KeyFor(kit::Button::kB), ImVec2(0, 0), gh, false);
      const float tw = kit::SpacedWidth(f.ui_bold, st(12), "CLOSE", st(1.5f));
      const ImVec2 c1 = P(1066 - 14, 46), c0(c1.x - (st(10) + gw + st(10) + tw + st(12) + st(10) + st(10)), P(0, 10).y);
      const bool hover = ImGui::IsMouseHoveringRect(c0, c1);
      d->AddRectFilled(c0, c1, hover ? IM_COL32(64, 66, 54, 255) : IM_COL32(43, 45, 36, 255));
      float x = c0.x + st(10);
      x += kit::Glyph(d, style, kit::Button::kB, KeyFor(kit::Button::kB), ImVec2(x, (c0.y + c1.y) * 0.5f), gh) + st(10);
      kit::Spaced(d, f.ui_bold, st(12), ImVec2(x, (c0.y + c1.y) * 0.5f - st(8)), kit::kStripText, "CLOSE", st(1.5f));
      x += tw + st(12);
      const ImVec2 xc(x + st(5), (c0.y + c1.y) * 0.5f);
      d->AddLine(ImVec2(xc.x - st(5), xc.y - st(5)), ImVec2(xc.x + st(5), xc.y + st(5)), kit::kStripText, st(2));
      d->AddLine(ImVec2(xc.x - st(5), xc.y + st(5)), ImVec2(xc.x + st(5), xc.y - st(5)), kit::kStripText, st(2));
      if (hover && clicked) close = true;
      if (news_new_count_ > 0) {
        const std::string n = fmt::format("{} NEW SINCE YOUR LAST VISIT", news_new_count_);
        kit::Spaced(d, f.typed, st(9.5f), ImVec2(c0.x - st(24) - kit::SpacedWidth(f.typed, st(9.5f), n, st(2)), P(0, 24).y),
                    kit::kKhakiDim, n, st(2));
      }
    }
    const bool has_items = !news_order_.empty();
    const news::Item* lead = has_items ? &feed.items[size_t(news_order_[0])] : nullptr;
    // ---- the lead's video or picture
    const ImVec2 va = P(27, 80), vb = P(27 + 560, 80 + 315);
    const bool video = lead && !lead->youtube.empty();
    bool embedded = false;
    if (video && news::VideoAvailable()) {
      d->AddRectFilled(va, vb, IM_COL32(0, 0, 0, 255));
      const std::string loading = "LOADING THE VIDEO";
      kit::Spaced(d, f.typed, st(10), ImVec2((va.x + vb.x - kit::SpacedWidth(f.typed, st(10), loading, st(2))) * 0.5f, (va.y + vb.y) * 0.5f - st(6)),
                  kit::kKhakiDim, loading, st(2));
      void* hwnd = window_ && window_() ? window_()->GetNativeWindowHandle() : nullptr;
      news::VideoShow(hwnd, UserDir() / "cache", lead->youtube, va.x, va.y, vb.x - va.x, vb.y - va.y, io.DisplaySize.x,
                      io.DisplaySize.y, news_muted_);
      embedded = news::VideoAvailable();  // false once it failed: the thumbnail from the next frame
    } else {
      news::VideoHide();
      if (lead) NewsPicture(d, *lead, va, vb, 1024);
      else d->AddRectFilled(va, vb, IM_COL32(28, 32, 24, 255));
      if (video) {  // the thumbnail: a click or A plays it on YouTube
        const bool hover = ImGui::IsMouseHoveringRect(va, vb);
        d->AddRectFilled(va, vb, IM_COL32(0, 0, 0, hover ? 30 : 60));
        NewsPlayMark(d, ImVec2((va.x + vb.x) * 0.5f, (va.y + vb.y) * 0.5f), st(hover ? 52 : 46));
        const std::string cap = "PLAYS ON YOUTUBE";
        kit::Spaced(d, f.typed, st(9.5f), ImVec2((va.x + vb.x - kit::SpacedWidth(f.typed, st(9.5f), cap, st(2))) * 0.5f, vb.y - st(26)),
                    kit::kCream, cap, st(2));
        if (hover && clicked) NewsPrimary(*lead);
      }
    }
    // ---- beside it: the kicker, the title, the text, the buttons
    const float rx = P(603, 0).x, rw = st(436);
    if (lead) {
      const std::string chip = order_is_lead() ? "LEAD STORY" : NewsKicker(*lead);
      const float cw = kit::SpacedWidth(f.ui_bold, st(10), chip, st(1.2f));
      const ImVec2 k0(rx, P(0, 80).y), k1(rx + st(10) + st(8) + cw + st(10), P(0, 100).y);
      d->AddRectFilled(k0, k1, kit::kStamp);
      d->AddCircleFilled(ImVec2(k0.x + st(9), (k0.y + k1.y) * 0.5f), st(2.5f), IM_COL32(255, 255, 255, 255));
      kit::Spaced(d, f.ui_bold, st(10), ImVec2(k0.x + st(17), k0.y + st(3)), IM_COL32(255, 255, 255, 255), chip, st(1.2f));
      if (const std::string date = Upper(news::ShortDate(lead->date)); !date.empty())
        kit::Spaced(d, f.typed, st(10), ImVec2(k1.x + st(12), k0.y + st(5)), kit::kInkSection, date, st(2.5f));
      if (news_new_ids_.count(lead->id)) {
        const float dx = k1.x + st(12) + (lead->date.empty() ? 0 : kit::SpacedWidth(f.typed, st(10), Upper(news::ShortDate(lead->date)), st(2.5f)) + st(12));
        kit::Spaced(d, f.typed, st(10), ImVec2(dx, k0.y + st(5)), kit::kGreenDark, "NEW", st(2.5f));
      }
      // The title in the game's lettering, as large as fits (two lines at most).
      const std::string title = Upper(lead->title);
      float cap = st(26);
      while (cap > st(15) && kit::GameTextWidth(kit::GameFont::kHeadline, cap, title) > rw) cap -= st(1);
      float ty = P(0, 112).y;
      if (kit::GameTextWidth(kit::GameFont::kHeadline, cap, title) <= rw) {
        kit::GameText(d, kit::GameFont::kHeadline, cap, ImVec2(rx, ty), kit::kInk, title);
        ty += cap + st(12);
      } else {  // still too long: two lines, by words
        std::string l1, l2;
        for (size_t i = 0; i < title.size();) {
          const size_t end = std::min(title.find(' ', i), title.size());
          const std::string w = title.substr(i, end - i);
          if (l2.empty() && kit::GameTextWidth(kit::GameFont::kHeadline, cap, l1.empty() ? w : l1 + " " + w) <= rw) l1 = l1.empty() ? w : l1 + " " + w;
          else l2 = l2.empty() ? w : l2 + " " + w;
          i = end + 1;
        }
        kit::GameText(d, kit::GameFont::kHeadline, cap, ImVec2(rx, ty), kit::kInk, l1);
        kit::GameText(d, kit::GameFont::kHeadline, cap, ImVec2(rx, ty + cap + st(6)), kit::kInk, l2);
        ty += 2 * cap + st(18);
      }
      const int text_lines = std::max(1, int((P(0, 238).y - ty) / st(21)));
      for (const std::string& l : NewsWrap(f.ui, st(15.5f), lead->text, rw, text_lines)) {
        d->AddText(f.ui, st(15.5f), ImVec2(rx, ty), kit::kInkFaint, l.c_str());
        ty += st(21);
      }
      // The buttons: the link or Watch on YouTube; Mute while the video plays here.
      const bool has_primary = !lead->link.empty() || video;
      if (has_primary) {
        const std::string label = !lead->button.empty() ? Upper(lead->button) : !lead->link.empty() ? "OPEN" : "WATCH ON YOUTUBE";
        const float lw = kit::GameTextWidth(kit::GameFont::kText, st(14), label, st(1));
        const ImVec2 b0(rx, P(0, 246).y), b1(rx + st(12) + st(20) + st(10) + lw + st(16), P(0, 280).y);
        if (GreenButton(d, st, b0, b1, kit::Button::kA, label)) NewsPrimary(*lead);
        if (news_focus_ == 0) d->AddRect(ImVec2(b0.x - st(3), b0.y - st(3)), ImVec2(b1.x + st(3), b1.y + st(6)), kit::kInk, 0, 0, st(2));
        if (embedded) {
          const std::string m = news_muted_ ? "SOUND ON" : "MUTE";
          const float mw = kit::GameTextWidth(kit::GameFont::kText, st(14), m, st(1));
          const ImVec2 m0(b1.x + st(12), b0.y), m1(m0.x + st(12) + st(20) + st(10) + mw + st(14), b1.y);
          const bool hover = ImGui::IsMouseHoveringRect(m0, m1);
          d->AddRect(m0, m1, hover ? kit::kInk : kit::kBorder, 0, 0, st(2));
          const float gw = kit::Glyph(d, style, kit::Button::kY, KeyFor(kit::Button::kY), ImVec2(m0.x + st(12), (m0.y + m1.y) * 0.5f), st(20));
          kit::GameText(d, kit::GameFont::kText, st(14), ImVec2(m0.x + st(12) + gw + st(10), (m0.y + m1.y) * 0.5f - st(7)), kit::kInk, m, st(1));
          if (hover && clicked) NewsToggleMute();
        }
      }
    } else {
      kit::GameText(d, kit::GameFont::kHeadline, st(26), ImVec2(rx, P(0, 112).y), kit::kInk, "NO NEWS RIGHT NOW");
      d->AddText(f.ui, st(13.5f), ImVec2(rx, P(0, 150).y), kit::kInkFaint, "The servers' state and what is coming up are below.");
    }
    // ---- the Reborn servers, live from the server list
    {
      const ImVec2 s0 = P(603, 292), s1 = P(1039, 395);
      d->AddRectFilled(s0, s1, IM_COL32(34, 36, 27, 14));
      d->AddRectFilled(s0, ImVec2(s0.x + st(4), s1.y), kit::kGreen);
      std::vector<ServerInfo> ours;
      for (const ServerInfo& sv : list_.servers())
        if (sv.official) ours.push_back(sv);
      std::stable_sort(ours.begin(), ours.end(), [](const ServerInfo& a, const ServerInfo& b) { return a.players > b.players; });
      int playing = 0;
      for (const ServerInfo& sv : ours) playing += sv.players;
      const ServerNotice notice = list_.notice();
      const float x0 = s0.x + st(16), x1 = s1.x - st(14);
      kit::Spaced(d, f.typed, st(9.5f), ImVec2(x0, s0.y + st(9)), kit::kInkSection, "REBORN SERVERS \xC2\xB7 NOW", st(2.5f));
      std::string right;
      ImU32 right_col = kit::kGreenDark;
      if (notice.active()) right = "CLOSED", right_col = kit::kStamp;
      else if (!list_.asked()) right = "ASKING";
      else if (!list_.reachable()) right = "NOT REACHABLE", right_col = kit::kStamp;
      else right = fmt::format("{} PLAYING", playing);
      kit::Spaced(d, f.typed, st(9.5f), ImVec2(x1 - kit::SpacedWidth(f.typed, st(9.5f), right, st(2.5f)), s0.y + st(9)), right_col, right, st(2.5f));
      float y = s0.y + st(26);
      if (notice.active()) {
        for (const std::string& l : NewsWrap(f.ui_bold, st(12.5f), notice.title.empty() ? std::string("Maintenance") : notice.title, x1 - x0, 1))
          d->AddText(f.ui_bold, st(12.5f), ImVec2(x0, y), kit::kStamp, l.c_str()), y += st(17);
        for (const std::string& l : NewsWrap(f.ui, st(11.5f), notice.text, x1 - x0, 2))
          d->AddText(f.ui, st(11.5f), ImVec2(x0, y), kit::kInkFaint, l.c_str()), y += st(15);
      } else {
        for (size_t i = 0; i < std::min<size_t>(3, ours.size()); ++i) {
          const ServerInfo& sv = ours[i];
          std::string name = sv.name;
          if (const size_t colon = name.find(": "); colon != std::string::npos) name = name.substr(colon + 2);
          if (!sv.region.empty()) name = Upper(sv.region.substr(0, sv.region.find('-'))) +" \xC2\xB7 " + name;
          const std::string count = fmt::format("{}/{}", sv.players, sv.max_players);
          const float cw = kit::SpacedWidth(f.typed, st(9.5f), count, st(1));
          float rw = cw;  // the count and the mark before it
          if (sv.promoted) {  // a promoted community server (Promoted, plasma.py): marked, never passed off as ours
            const char* mark = "TRUSTED";  // a Trusted Community Server, said short in this small box
            const float mw = kit::SpacedWidth(f.typed, st(8.5f), mark, st(1.5f));
            kit::Spaced(d, f.typed, st(8.5f), ImVec2(x1 - cw - st(10) - mw, y + st(3.5f)), IM_COL32(138, 112, 40, 255), mark, st(1.5f));
            rw += mw + st(10);
          }
          d->AddCircleFilled(ImVec2(x0 + st(3), y + st(8)), st(3), sv.players >= sv.max_players ? IM_COL32(217, 160, 58, 255) : kit::kGreen);
          d->AddText(f.ui, st(12.5f), ImVec2(x0 + st(12), y), kit::kInk, NewsFit(f.ui, st(12.5f), name, x1 - x0 - st(24) - rw).c_str());
          kit::Spaced(d, f.typed, st(9.5f), ImVec2(x1 - cw, y + st(3)), kit::kInkSection, count, st(1));
          d->AddRectFilled(ImVec2(x0, y + st(17.5f)), ImVec2(x1, y + st(18)), kit::kRule);
          y += st(19);
        }
        if (ours.empty() && list_.reachable())
          d->AddText(f.ui, st(12.5f), ImVec2(x0, y), kit::kInkFaint, "No Reborn server is listed right now."), y += st(19);
      }
      std::string foot = ours.size() > 3 && !notice.active() ? fmt::format("+ {} MORE", ours.size() - 3) : "";
      if (!feed.status.empty()) foot += (foot.empty() ? "" : " \xC2\xB7 ") + Upper(feed.status);
      if (!foot.empty())
        kit::Spaced(d, f.typed, st(8.5f), ImVec2(x0, s1.y - st(16)), kit::kInkSection, NewsFit(f.typed, st(8.5f), foot, (x1 - x0) * 0.82f), st(1.5f));
    }
    // ---- the cards (the other items), and the coming up list
    const bool upcoming = !feed.upcoming.empty();
    const int slots = upcoming ? 3 : 4;
    const int cards = std::max(0, int(news_order_.size()) - 1);
    const float row_x0 = P(27, 0).x, row_x1 = upcoming ? P(587, 0).x : P(1039, 0).x;
    const float gap = st(12), card_w = (row_x1 - row_x0 - gap * float(slots - 1)) / float(slots);
    news_first_card_ = std::clamp(news_first_card_, 1, std::max(1, cards - slots + 1));
    for (int j = 0; j < slots && news_first_card_ + j <= cards; ++j) {
      const int k = news_first_card_ + j;
      const news::Item& it = feed.items[size_t(news_order_[size_t(k)])];
      const ImVec2 c0(row_x0 + float(j) * (card_w + gap), P(0, 411).y), c1(c0.x + card_w, P(0, 540).y);
      const bool hover = ImGui::IsMouseHoveringRect(c0, c1);
      if (hover && moved) news_focus_ = k;
      d->AddRectFilled(ImVec2(c0.x + st(2), c0.y + st(4)), ImVec2(c1.x + st(2), c1.y + st(4)), IM_COL32(0, 0, 0, 34));
      d->AddRectFilled(c0, c1, kit::kPaperLight);
      NewsPicture(d, it, c0, ImVec2(c1.x, c0.y + st(58)), 512);
      if (!it.youtube.empty()) NewsPlayMark(d, ImVec2((c0.x + c1.x) * 0.5f, c0.y + st(29)), st(20));
      if (news_new_ids_.count(it.id)) {
        const ImVec2 n0(c1.x - st(36), c0.y + st(6)), n1(c1.x - st(6), c0.y + st(20));
        d->AddRectFilled(n0, n1, kit::kGreen);
        kit::Spaced(d, f.ui_bold, st(8.5f), ImVec2(n0.x + st(5), n0.y + st(1.5f)), IM_COL32(255, 255, 255, 255), "NEW", st(1));
      }
      const float tx = c0.x + st(12), tw = card_w - st(24);
      kit::Spaced(d, f.typed, st(8.5f), ImVec2(tx, c0.y + st(66)), kit::kStamp, NewsFit(f.typed, st(8.5f), NewsKicker(it), tw * 0.8f), st(1.8f));
      d->AddText(f.ui_bold, st(13), ImVec2(tx, c0.y + st(80)), kit::kInk, NewsFit(f.ui_bold, st(13), it.title, tw).c_str());
      float ly = c0.y + st(99);
      for (const std::string& l : NewsWrap(f.ui, st(10.5f), it.text, tw, 2)) {
        d->AddText(f.ui, st(10.5f), ImVec2(tx, ly), kit::kInkFaint, l.c_str());
        ly += st(13.5f);
      }
      if (news_focus_ == k) d->AddRect(c0, c1, kit::kStamp, 0, 0, st(3));
      else if (hover) d->AddRect(c0, c1, kit::kGreen, 0, 0, st(2));
      if (hover && clicked) NewsShow(k);
    }
    if (cards > slots) {  // more cards either side: small arrows at the row's ends
      const float cy = P(0, 475).y;
      if (news_first_card_ > 1)
        d->AddTriangleFilled(ImVec2(row_x0 - st(4), cy - st(8)), ImVec2(row_x0 - st(4), cy + st(8)), ImVec2(row_x0 - st(14), cy), kit::kStamp);
      if (news_first_card_ + slots - 1 < cards)
        d->AddTriangleFilled(ImVec2(row_x1 + st(4), cy - st(8)), ImVec2(row_x1 + st(4), cy + st(8)), ImVec2(row_x1 + st(14), cy), kit::kStamp);
    }
    if (cards == 0 && has_items) {
      d->AddText(f.ui, st(12.5f), ImVec2(row_x0, P(0, 420).y), kit::kInkFaint, "More news soon.");
    }
    if (upcoming) {
      const ImVec2 u0 = P(603, 411), u1 = P(1039, 540);
      d->AddRectFilled(ImVec2(u0.x + st(2), u0.y + st(4)), ImVec2(u1.x + st(2), u1.y + st(4)), IM_COL32(0, 0, 0, 34));
      d->AddRectFilled(u0, u1, kit::kPaperLight);
      kit::Spaced(d, f.typed, st(9.5f), ImVec2(u0.x + st(14), u0.y + st(11)), kit::kInkSection, "COMING UP", st(2.5f));
      float y = u0.y + st(30);
      const size_t rows = std::min<size_t>(4, feed.upcoming.size());
      for (size_t i = 0; i < rows; ++i) {
        const news::Upcoming& u = feed.upcoming[i];
        d->AddText(f.ui_bold, st(12.5f), ImVec2(u0.x + st(14), y), kit::kInk, NewsFit(f.ui_bold, st(12.5f), Upper(u.label), st(58)).c_str());
        d->AddText(f.ui, st(12.5f), ImVec2(u0.x + st(78), y), kit::kInk, NewsFit(f.ui, st(12.5f), u.text, u1.x - u0.x - st(92)).c_str());
        if (i + 1 < rows) d->AddRectFilled(ImVec2(u0.x + st(14), y + st(21)), ImVec2(u1.x - st(14), y + st(21.5f)), kit::kRule);
        y += st(25);
      }
    }
    // ---- the foot: the prompts, how to move, how to open it again
    d->AddRectFilled(P(0, 556), P(1066, 557), kit::kRule);
    Input foot;
    std::vector<Cue> cues = {{kit::Button::kA, news_focus_ == 0 ? "OPEN" : "SHOW"}};
    if (embedded) cues.push_back({kit::Button::kY, news_muted_ ? "SOUND ON" : "MUTE"});
    if (cards > 0) cues.push_back({kit::Button::kLB, "ITEMS"});
    cues.push_back({kit::Button::kB, "CLOSE"});
    const float pw = Prompts(d, st, P(27, 563), cues, foot, 13);
    kit::Spaced(d, f.typed, st(8.5f), ImVec2(P(27, 0).x + pw + st(10), P(0, 574).y), kit::kInkSection,
                "STICK OR D-PAD MOVES \xC2\xB7 MOUSE CLICKS ANYTHING", st(1.8f));
    const std::string reopen = "REOPEN: NEWS ON THE MAIN MENU";
    kit::Spaced(d, f.typed, st(8.5f), ImVec2(P(1039, 0).x - kit::SpacedWidth(f.typed, st(8.5f), reopen, st(1.8f)), P(0, 574).y),
                kit::kInkSection, reopen, st(1.8f));
    // ---- input: the controller and keyboard (the menu's own), the wheel, Esc pressed inside the player
    in.accept |= foot.accept, in.back |= foot.back, in.y |= foot.y, in.prev_tab |= foot.prev_tab;
    if (news::VideoTakeEscape()) in.back = true;
    if (io.MouseWheel != 0 && cards > slots) news_first_card_ -= io.MouseWheel > 0 ? 1 : -1;
    if (in.back || close) return CloseNews(in.back ? "B or Esc" : "the mouse");
    if (in.y && embedded) NewsToggleMute();
    if (cards > 0) {
      if (in.down && news_focus_ == 0) news_focus_ = news_first_card_;
      else if (in.up && news_focus_ > 0) news_focus_ = 0;
      else if ((in.right || in.next_tab) && news_focus_ < cards) news_focus_ = std::max(1, news_focus_ + 1);
      else if ((in.left || in.prev_tab) && news_focus_ > 1) --news_focus_;
      else if (in.left && news_focus_ == 1) news_focus_ = 0;
      if (news_focus_ > 0) {  // the focused card in view
        if (news_focus_ < news_first_card_) news_first_card_ = news_focus_;
        if (news_focus_ > news_first_card_ + slots - 1) news_first_card_ = news_focus_ - slots + 1;
      }
    }
    if (in.accept) {
      if (news_focus_ == 0 && lead) NewsPrimary(*lead);
      else NewsShow(news_focus_);
    }
  }
  // True when the lead is the feed's own lead story (its first item after sorting).
  bool order_is_lead() const { return !news_order_.empty() && news_order_[0] == 0; }

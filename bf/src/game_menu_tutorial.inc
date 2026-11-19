// The video tutorial on the first start's game files page (TekRantGaming, 18 Nov 2026, for 0.4.7.4: "a long horizontal
// button that says watch video tutorial ... opens, still within the launcher ... auto plays ... has audio"; then "fully
// credit this youtuber with his logo too, button to subscribe ... a notice that's clear at the top that this is a fan
// made tutorial and the individual is not associated with the project's development").
//
// The video is Leotseil Gaming's (YouTube channel UCT3ekgo7WY7SeoivHxY2k9w), shown with YouTube's own embedded player
// in the news window's WebView2 view (news_video.cpp: its player page, autoplay with sound, Esc, links out to the
// browser). The window round it is ours: the fan made notice at the top, the video, the channel's logo, name and
// Subscribe (the channel's page in the browser, with YouTube's subscribe prompt). The logo and the video's thumbnail are
// fetched once into the user cache (cache/tutorial/) from YouTube's picture hosts; until then a lettered disc.
//
// Linux, the Steam Deck, Windows without WebView2, and no internet (the player says it failed): the thumbnail with a
// short message and the link; A or a click opens the video on YouTube in the browser, as the news window does there.
// Included inside GameMenu (game_menu.cpp) after game_menu_news.inc.

  static constexpr const char* kTutorialVideo = "JULHVN05jkc";
  static constexpr const char* kTutorialWatch = "https://www.youtube.com/watch?v=JULHVN05jkc";
  static constexpr const char* kTutorialChannel = "https://www.youtube.com/@LeotseilGaming";
  // The channel's page with YouTube's own "Subscribe to Leotseil Gaming?" prompt.
  static constexpr const char* kTutorialSubscribe = "https://www.youtube.com/@LeotseilGaming?sub_confirmation=1";
  // The channel's picture (its og:image, 18 Nov 2026), 176 px; a JPEG for a client that asks for no WebP.
  static constexpr const char* kTutorialLogo =
      "https://yt3.googleusercontent.com/4JEM0-l4kthsmmoO58e7sl_hngYPratYthndMtmAHELAZOboDLNU6tLy58ets6RAlkd2yhvQ6II="
      "s176-c-k-c0x00ffffff-no-rj";
  static constexpr const char* kTutorialThumb = "https://i.ytimg.com/vi/JULHVN05jkc/hqdefault.jpg";

  bool tutorial_open_ = false, tutorial_failed_ = false, tutorial_fetching_ = false;

  fs::path TutorialDir() { return UserDir() / "cache" / "tutorial"; }

  // The logo and the thumbnail, each once (kept in the cache for the next start), on a thread of their own.
  void TutorialFetch() {
    if (tutorial_fetching_) return;
    tutorial_fetching_ = true;
    std::thread([dir = TutorialDir()] {
      std::error_code ec;
      fs::create_directories(dir, ec);
      const std::pair<const char*, const char*> kFiles[] = {{kTutorialLogo, "logo.jpg"}, {kTutorialThumb, "thumb.jpg"}};
      for (const auto& [url, name] : kFiles) {
        const fs::path file = dir / name;
        if (fs::exists(file, ec)) continue;
        std::vector<uint8_t> bytes;
        int w = 0, h = 0;
        const bool ok = HttpGet(url, bytes, nullptr, nullptr, 10000) && !bytes.empty() && bytes.size() <= (4u << 20) &&
                        !DecodePicture(bytes.data(), bytes.size(), w, h).empty() && w > 0;
        if (!ok) {
          REXLOG_INFO("BF setup: video tutorial: {} could not be fetched (no internet?)", name);
          continue;
        }
        const fs::path part = dir / (std::string(name) + ".part");  // whole or not at all: the menu reads it meanwhile
        {
          std::ofstream out(part, std::ios::binary | std::ios::trunc);
          out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        fs::rename(part, file, ec);
        REXLOG_INFO("BF setup: video tutorial: {} ({}x{})", name, w, h);
      }
    }).detach();
  }

  void OpenTutorial(const char* how) {
    tutorial_open_ = true, tutorial_failed_ = false;
    settle_until_ = ImGui::GetTime() + 0.25;  // the press that opened it does not act in it
    TutorialFetch();
    REXLOG_INFO("BF setup: video tutorial opened ({}; {})", how,
                news::VideoAvailable() ? "the embedded player" : "no embedded player here: the thumbnail and the link");
  }
  void CloseTutorial(const char* how) {
    tutorial_open_ = false;
    news::VideoHide();
    settle_until_ = ImGui::GetTime() + 0.25;  // the press that closed it does not act on the page
    REXLOG_INFO("BF setup: video tutorial closed ({})", how);
  }

  // The window over the game files page (that page keeps its state underneath).
  void TutorialPopup(ImDrawList* d, ImGuiIO& io, Input in) {
    const Stage st = StageOf(io);
    const kit::Fonts& f = kit::GetFonts();
    const kit::Style style = PromptStyle();
    if (!Settled()) in = {};
    auto P = [&](float x, float y) { return st.P(140 + x, 22 + y); };
    const ImVec2 wa = P(0, 0), wb = P(1000, 676);
    const bool clicked = Settled() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool close = false;
    // The page dimmed round it, the window's shadow and paper.
    d->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(10, 12, 8, 184));
    d->AddRectFilled(ImVec2(wa.x + st(4), wa.y + st(10)), ImVec2(wb.x + st(4), wb.y + st(14)), IM_COL32(0, 0, 0, 90));
    kit::PaperArt(d, wa, wb);
    if (clicked && !ImGui::IsMouseHoveringRect(wa, wb)) close = true;  // a click outside
    // ---- the header: VIDEO TUTORIAL, what it shows, Close
    d->AddRectFilled(wa, P(1000, 52), kit::kStrip);
    d->AddRectFilled(P(0, 52), P(1000, 56), kit::kStamp);
    float hx = P(20, 0).x;
    hx += kit::GameText(d, kit::GameFont::kHeadline, st(24), ImVec2(hx, P(0, 15).y), kit::kCream, "VIDEO TUTORIAL") + st(18);
    kit::Spaced(d, f.typed, st(10), ImVec2(hx, P(0, 22).y), kit::kGold, "HOW TO SET UP THE GAME FILES", st(2.5f));
    {  // Close: its glyph, the word, the cross; a click
      const float gh = st(18);
      const float gw = kit::Glyph(nullptr, style, kit::Button::kB, KeyFor(kit::Button::kB), ImVec2(0, 0), gh, false);
      const float tw = kit::SpacedWidth(f.ui_bold, st(12), "CLOSE", st(1.5f));
      const ImVec2 c1 = P(1000 - 12, 42), c0(c1.x - (st(10) + gw + st(10) + tw + st(12) + st(10) + st(10)), P(0, 10).y);
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
    }
    // ---- the notice: a fan made video, its maker not one of us
    {
      const ImVec2 s0 = P(24, 70), s1 = P(976, 134);
      d->AddRectFilled(s0, s1, kit::kPaperDim);
      d->AddRectFilled(s0, ImVec2(s0.x + st(5), s1.y), kit::kStamp);
      kit::Spaced(d, f.typed, st(12), ImVec2(s0.x + st(18), s0.y + st(10)), kit::kStamp,
                  "FAN MADE TUTORIAL \xC2\xB7 NOT FROM THE BATTLEFIELD 1943 REBORN TEAM", st(2));
      kit::Wrapped(d, f.ui_bold, st(16), ImVec2(s0.x + st(18), s0.y + st(30)), kit::kInk,
                   "This video was made by Leotseil Gaming, a player in the community. Leotseil Gaming is not associated "
                   "with the development of Battlefield 1943 Reborn.",
                   s1.x - s0.x - st(36));
    }
    // ---- the video: the embedded player, else the thumbnail with the link
    const ImVec2 va = P(109, 146), vb = P(109 + 782, 146 + 440);
    if (news::VideoFailed() && !tutorial_failed_) {
      tutorial_failed_ = true;  // shown as the link from now on (not tried again until it is opened again)
      REXLOG_INFO("BF setup: video tutorial: the player could not load the video: the link instead");
    }
    const bool embedded = news::VideoAvailable() && !tutorial_failed_;
    if (embedded) {
      d->AddRectFilled(va, vb, IM_COL32(0, 0, 0, 255));
      const std::string loading = "LOADING THE VIDEO";
      kit::Spaced(d, f.typed, st(10), ImVec2((va.x + vb.x - kit::SpacedWidth(f.typed, st(10), loading, st(2))) * 0.5f, (va.y + vb.y) * 0.5f - st(6)),
                  kit::kKhakiDim, loading, st(2));
      void* hwnd = window_ && window_() ? window_()->GetNativeWindowHandle() : nullptr;
      news::VideoShow(hwnd, UserDir() / "cache", kTutorialVideo, va.x, va.y, vb.x - va.x, vb.y - va.y, io.DisplaySize.x,
                      io.DisplaySize.y, false);
    } else {
      news::VideoHide();
      d->AddRectFilled(va, vb, IM_COL32(28, 32, 24, 255));
      kit::DrawImageFile(d, TutorialDir() / "thumb.jpg", va, vb, false, false, 640);
      const bool hover = ImGui::IsMouseHoveringRect(va, vb);
      d->AddRectFilled(va, vb, IM_COL32(0, 0, 0, hover ? 135 : 165));
      const ImVec2 c((va.x + vb.x) * 0.5f, (va.y + vb.y) * 0.5f - st(24));
      NewsPlayMark(d, c, st(hover ? 58 : 52));
      const std::string head = tutorial_failed_ ? "THE VIDEO COULD NOT LOAD HERE" : "PLAYS ON YOUTUBE";
      kit::Spaced(d, f.typed, st(12), ImVec2(c.x - kit::SpacedWidth(f.typed, st(12), head, st(2)) * 0.5f, c.y + st(46)), kit::kCream, head, st(2));
      const std::string line = tutorial_failed_ ? "Check your internet connection. A or a click opens it in your browser:"
                                                : "A or a click opens it in your browser:";
      const float lw = kit::TextWidth(f.ui, st(14), line);
      d->AddText(f.ui, st(14), ImVec2(c.x - lw * 0.5f, c.y + st(70)), kit::kCream, line.c_str());
      const std::string link = "youtube.com/watch?v=JULHVN05jkc";
      const float kw = kit::TextWidth(f.ui_bold, st(15), link);
      d->AddText(f.ui_bold, st(15), ImVec2(c.x - kw * 0.5f, c.y + st(92)), kit::kGold, link.c_str());
      if ((hover && clicked) || in.accept) OpenUrl(kTutorialWatch);
    }
    // ---- the credit: the channel's logo and name, Subscribe
    const float cy = P(0, 600).y;
    {
      const ImVec2 l0(va.x, cy), l1(va.x + st(60), cy + st(60));
      ImVec2 uv0, uv1;
      int lw = 0, lh = 0;
      if (kit::ImageFileUv(TutorialDir() / "logo.jpg", uv0, uv1, lw, lh)) {
        d->AddImageRounded(io.Fonts->TexRef, l0, l1, uv0, uv1, IM_COL32(255, 255, 255, 255), st(30));
      } else {  // until the logo is there: the channel's letter on a disc
        d->AddCircleFilled(ImVec2(l0.x + st(30), l0.y + st(30)), st(30), IM_COL32(28, 24, 40, 255), 40);
        const float w = kit::GameTextWidth(kit::GameFont::kHeadline, st(28), "L");
        kit::GameText(d, kit::GameFont::kHeadline, st(28), ImVec2(l0.x + st(30) - w * 0.5f, l0.y + st(16)), kit::kCream, "L");
      }
      const bool hover = ImGui::IsMouseHoveringRect(l0, ImVec2(va.x + st(420), l1.y));
      const float tx = l1.x + st(16);
      kit::Spaced(d, f.typed, st(9.5f), ImVec2(tx, cy + st(2)), kit::kInkSection, "VIDEO AND TUTORIAL BY", st(2));
      kit::GameText(d, kit::GameFont::kHeadline, st(22), ImVec2(tx, cy + st(17)), hover ? kit::kGreenDark : kit::kInk, "LEOTSEIL GAMING");
      d->AddText(f.ui, st(13), ImVec2(tx, cy + st(43)), kit::kInkFaint, "youtube.com/@LeotseilGaming");
      if (hover && clicked) OpenUrl(kTutorialChannel);
    }
    {  // Subscribe, as YouTube draws it: a white pill, black lettering
      const std::string label = "SUBSCRIBE";
      const float gw = kit::Glyph(nullptr, style, kit::Button::kY, KeyFor(kit::Button::kY), ImVec2(0, 0), st(20), false);
      const float tw = kit::GameTextWidth(kit::GameFont::kText, st(15), label, st(1));
      const ImVec2 s1(vb.x, cy + st(50)), s0(s1.x - (st(16) + gw + st(10) + tw + st(22)), cy + st(10));
      const bool hover = ImGui::IsMouseHoveringRect(s0, s1);
      d->AddRectFilled(ImVec2(s0.x, s0.y + st(3)), ImVec2(s1.x, s1.y + st(3)), IM_COL32(0, 0, 0, 60), st(20));
      d->AddRectFilled(s0, s1, hover ? IM_COL32(229, 229, 229, 255) : IM_COL32(255, 255, 255, 255), st(20));
      d->AddRect(s0, s1, kit::kBorder, st(20), 0, 1.0f);
      kit::Glyph(d, style, kit::Button::kY, KeyFor(kit::Button::kY), ImVec2(s0.x + st(16), (s0.y + s1.y) * 0.5f), st(20));
      kit::GameText(d, kit::GameFont::kText, st(15), ImVec2(s0.x + st(16) + gw + st(10), (s0.y + s1.y) * 0.5f - st(7.5f)),
                    IM_COL32(15, 15, 15, 255), label, st(1));
      const std::string note = "OPENS YOUTUBE IN YOUR BROWSER";
      kit::Spaced(d, f.typed, st(8.5f), ImVec2(s1.x - kit::SpacedWidth(f.typed, st(8.5f), note, st(1.5f)), s1.y + st(9)),
                  kit::kInkFaint, note, st(1.5f));
      if ((hover && clicked) || in.y) {
        REXLOG_INFO("BF setup: video tutorial: Subscribe");
        OpenUrl(kTutorialSubscribe);
      }
    }
    if (news::VideoTakeEscape()) close = true;  // Esc while the player had the keyboard
    if (in.back) close = true;
    if (close) CloseTutorial(in.back ? "B or Esc" : "a click");
  }

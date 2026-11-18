// The scoreboards' speaker icon (voice_mute.h), drawn as shapes so it is sharp at any size (720p to 4K): a speaker,
// its two sound waves while the player is heard (brightening in turn, out from the cone), a slash through it once
// muted. An optional halo (a thin outline in another colour) keeps it readable on the game's dark and green row bars
// as well as on its paper. Our own drawing.

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "voice_mute.h"

namespace bf1943::mute {
namespace {

ImU32 WithAlpha(ImU32 c, float a) {
  const int base = int((c >> IM_COL32_A_SHIFT) & 0xFF);
  const int out = std::clamp(int(base * std::clamp(a, 0.0f, 1.0f) + 0.5f), 0, 255);
  return (c & ~IM_COL32_A_MASK) | (ImU32(out) << IM_COL32_A_SHIFT);
}

// The shapes, in the icon's box (h x h, centred on c): the speaker spans -0.44h to +0.46h across with its waves,
// -0.42h to +0.42h down. `grow` widens every shape by that many pixels (the halo).
void Shapes(ImDrawList* d, const ImVec2& c, float h, Voice v, ImU32 body, ImU32 wave_ink, ImU32 strike, double seconds,
            float grow, float wave_alpha_scale) {
  auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
  // The magnet box and the cone.
  d->AddRectFilled(ImVec2(P(-0.44f, 0).x - grow, P(0, -0.16f).y - grow), ImVec2(P(-0.20f, 0).x + grow, P(0, 0.16f).y + grow),
                   body, 0.04f * h);
  if (grow > 0) {
    // The cone grown: the same four corners pushed out from its middle.
    const ImVec2 mid = P(-0.09f, 0.0f);
    ImVec2 pts[4] = {P(-0.22f, -0.16f), P(0.04f, -0.42f), P(0.04f, 0.42f), P(-0.22f, 0.16f)};
    for (ImVec2& p : pts) {
      const float dx = p.x - mid.x, dy = p.y - mid.y, len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
      p.x += dx / len * grow * 1.4f, p.y += dy / len * grow * 1.4f;
    }
    d->AddConvexPolyFilled(pts, 4, body);
  } else {
    const ImVec2 cone[4] = {P(-0.22f, -0.16f), P(0.04f, -0.42f), P(0.04f, 0.42f), P(-0.22f, 0.16f)};
    d->AddConvexPolyFilled(cone, 4, body);
  }
  const float th = std::max(1.0f, 0.085f * h) + 2 * grow;
  if (v == Voice::kTalking) {
    for (int i = 0; i < 2; ++i) {
      const float r = (0.20f + 0.19f * float(i)) * h;
      const float phase = float(std::fmod(seconds * 3.0 - 0.33 * i + 10.0, 1.0));
      const float lit = 0.40f + 0.60f * std::max(0.0f, std::sin(phase * 3.14159265f));
      d->PathArcTo(P(0.06f, 0.0f), r, -0.9f, 0.9f, 14);
      d->PathStroke(WithAlpha(wave_ink, std::min(1.0f, lit * wave_alpha_scale)), ImDrawFlags_None, th);
    }
  } else if (v == Voice::kMuted) {
    d->AddLine(P(-0.40f, -0.44f), P(0.42f, 0.44f), strike, std::max(1.5f, 0.12f * h) + 2 * grow);
  }
}

}  // namespace

void DrawIcon(ImDrawList* d, const ImVec2& c, float h, Voice v, unsigned ink, unsigned accent, unsigned strike,
              double seconds, float alpha, unsigned halo) {
  if (!d || h < 2) return;
  const float quiet = v == Voice::kQuiet ? 0.6f : 1.0f;
  if (halo) {
    const float g = std::max(1.0f, 0.07f * h);
    const ImU32 hc = WithAlpha(halo, alpha * (v == Voice::kQuiet ? 0.7f : 0.95f));
    Shapes(d, c, h, v, hc, hc, hc, seconds, g, 1.0f);
  }
  Shapes(d, c, h, v, WithAlpha(ink, alpha * quiet), WithAlpha(accent, alpha), WithAlpha(strike, alpha), seconds, 0.0f, 1.0f);
}

}  // namespace bf1943::mute

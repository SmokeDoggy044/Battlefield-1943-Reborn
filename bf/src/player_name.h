// Soldier names: letters, digits and underscores, and since v0.4.7.4 one trademark sign at the end (TekRantGaming,
// 18 Nov 2026: ZALIAS, a developer on the project, plays as "ZALIAS™" and needs it to work in game). The name is UTF-8
// everywhere in our code and in the game's own buffers (XamUserGetName's 16 bytes: at most 15 and the end), so the
// sign takes 3 of the 15 bytes. Shared by the name page, the start's own cleaning and the clan code.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace bf1943::player_name {

inline constexpr std::string_view kTm = "\xE2\x84\xA2";  // U+2122 in UTF-8
inline constexpr char32_t kTmCodePoint = 0x2122;
inline constexpr size_t kMaxBytes = 15;

inline bool Plain(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool EndsTm(std::string_view s) { return s.size() >= kTm.size() && s.substr(s.size() - kTm.size()) == kTm; }
// The characters a player sees (the sign is one).
inline size_t Chars(std::string_view s) { return EndsTm(s) ? s.size() - kTm.size() + 1 : s.size(); }
// The letters, digits and underscores (the 3 a name needs at least).
inline size_t PlainChars(std::string_view s) { return EndsTm(s) ? s.size() - kTm.size() : s.size(); }
// Whether one more letter, digit or underscore fits (nothing goes after the sign).
inline bool CanType(std::string_view s) { return !EndsTm(s) && s.size() < kMaxBytes; }
// Whether the sign fits at the end now.
inline bool CanTypeTm(std::string_view s) { return !s.empty() && !EndsTm(s) && s.size() + kTm.size() <= kMaxBytes; }
// The last character taken away (the sign whole).
inline void PopBack(std::string& s) {
  if (EndsTm(s)) s.resize(s.size() - kTm.size());
  else if (!s.empty()) s.pop_back();
}
// A name as the game allows it: letters, digits and underscores (spaces too when `spaces`, for names saved before the
// name page), and one sign kept at its end wherever it was, at most 15 bytes. Any other byte is left out.
inline std::string Clean(std::string_view in, bool spaces = false) {
  std::string out;
  bool tm = false;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in.compare(i, kTm.size(), kTm) == 0) {
      tm = true, i += kTm.size() - 1;
      continue;
    }
    if (Plain(in[i]) || (spaces && in[i] == ' ')) out += in[i];
  }
  if (spaces) {
    out.erase(0, out.find_first_not_of(' ') == std::string::npos ? out.size() : out.find_first_not_of(' '));
  }
  const size_t room = kMaxBytes - (tm && !out.empty() ? kTm.size() : 0);
  if (out.size() > room) out.resize(room);
  while (spaces && !out.empty() && out.back() == ' ') out.pop_back();
  if (tm && !out.empty()) out += kTm;
  return out;
}

}  // namespace bf1943::player_name

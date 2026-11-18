// Developer aid (BF_DEV_TOOLS builds): BF_DEV_VEHICLESCAN=1 logs the text reachable from the vehicle the local
// soldier just got into (the object at player+304, seat_info.h), one and two pointers away: to find where the game
// keeps a vehicle's name on a player's game, for Reborn's vehicle achievements (reborn_achievements.cpp).

#if defined(BF_DEV_TOOLS)

#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>

#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "dev_guest_memory.h"

namespace bf1943 {
namespace {
bool Plausible(uint32_t a) { return a >= 0x00100000u && a < 0xFFF00000u && !(a & 3); }
uint32_t Load(const uint8_t* base, uint32_t a) { return dev::SafeLoad32(base, a); }
// Printable text of 3 to 64 characters ending in a zero at `a`.
std::string Text(const uint8_t* base, uint32_t a) {
  if (a < 0x00100000u || !dev::GuestReadable(base, a, 65)) return {};
  std::string s;
  for (uint32_t i = 0; i < 65; ++i) {
    const char c = char(base[a + i]);
    if (!c) return s.size() >= 3 ? s : std::string();
    if (c < 32 || c > 126) return {};
    s += c;
  }
  return {};
}
}  // namespace

void DevVehicleScan(uint32_t vehicle) {
  static const bool on = [] {
    const char* v = std::getenv("BF_DEV_VEHICLESCAN");
    return v && *v == '1';
  }();
  auto* k = rex::system::kernel_state();
  if (!on || !k || !k->memory() || !Plausible(vehicle)) return;
  const uint8_t* base = k->memory()->virtual_membase();
  REXLOG_INFO("BF vehicle scan: vehicle {:08X}, vtable {:08X}", vehicle, Load(base, vehicle));
  // Its data (+0x14: the name at +0xC), word by word: to compare vehicles.
  if (const uint32_t data = Load(base, vehicle + 0x14); Plausible(data))
    for (uint32_t off = 0; off < 0x240; off += 32)
      REXLOG_INFO("BF vehicle scan: data +{:03X}: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", off,
                  Load(base, data + off), Load(base, data + off + 4), Load(base, data + off + 8), Load(base, data + off + 12),
                  Load(base, data + off + 16), Load(base, data + off + 20), Load(base, data + off + 24),
                  Load(base, data + off + 28));
  int lines = 0;
  std::set<uint32_t> seen;
  auto note = [&](const std::string& where, uint32_t w) {
    if (lines >= 400) return;
    if (std::string t = Text(base, w); !t.empty()) {
      REXLOG_INFO("BF vehicle scan: {} -> \"{}\"", where, t);
      ++lines;
      return;
    }
    // An eastl string: begin and end words.
    const uint32_t b = Load(base, w), e = Load(base, w + 4);
    if (Plausible(w) && b >= 0x00100000u && e > b && e - b < 64)
      if (std::string t = Text(base, b); !t.empty() && t.size() == e - b) {
        REXLOG_INFO("BF vehicle scan: {} -> string \"{}\"", where, t);
        ++lines;
      }
  };
  for (uint32_t off = 0; off < 0x800; off += 4) {
    const uint32_t w = Load(base, vehicle + off);
    note(fmt::format("+{:X}", off), vehicle + off);  // a string in the object itself
    if (!Plausible(w) || !seen.insert(w).second) continue;
    note(fmt::format("+{:X}*", off), w);
    for (uint32_t off2 = 0; off2 < 0x200; off2 += 4) {
      const uint32_t w2 = Load(base, w + off2);
      if (!Plausible(w2)) continue;
      note(fmt::format("+{:X}* +{:X}*", off, off2), w2);
      // and one more level: an object's data, whose first words name it
      for (uint32_t off3 = 0; off3 < 0x40; off3 += 4) {
        const uint32_t w3 = Load(base, w2 + off3);
        if (Plausible(w3)) note(fmt::format("+{:X}* +{:X}* +{:X}*", off, off2, off3), w3);
      }
    }
  }
  REXLOG_INFO("BF vehicle scan: {} lines", lines);
}

}  // namespace bf1943

#endif  // BF_DEV_TOOLS

// Renders the achievement chime (bf/src/theme_chime.h) from a menu.wav the game extracted, with the game's own code:
// to listen to a recipe before it ships, and to check that two builds or systems make the same bytes.
// Build: clang++ -std=c++23 -O2 -I bf/src tools/theme_chime_render.cpp bf/src/theme_chime.cpp -o theme_chime_render
// Use:   theme_chime_render <user\cache\extracted> <out.wav> [field=value ...]
//        The fields change the recipe for this render only (to hear one layer alone, or try a level):
//        bass shimmer swell wet bells echo echo_cut bloom_room bloom_damp bloom_wet tail peak room damp speed
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "theme_chime.h"

using namespace bf1943;

static std::vector<int16_t> ReadPcm(const std::string& file) {
  std::ifstream f(file, std::ios::binary);
  std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  for (size_t o = 12; o + 8 <= b.size();) {
    uint32_t len;
    std::memcpy(&len, &b[o + 4], 4);
    if (!std::memcmp(&b[o], "data", 4)) {
      std::vector<int16_t> pcm(std::min<size_t>(len, b.size() - o - 8) / 2);
      std::memcpy(pcm.data(), &b[o + 8], pcm.size() * 2);
      return pcm;
    }
    o += 8 + len + (len & 1);
  }
  return {};
}

static void Write(const std::string& file, const std::vector<int16_t>& pcm) {
  auto le = [](std::string& s, uint32_t v, int n) { for (int i = 0; i < n; ++i) s += char(v >> (8 * i)); };
  const uint32_t bytes = uint32_t(pcm.size() * 2), rate = theme_chime::kRate;
  std::string h = "RIFF";
  le(h, 36 + bytes, 4), h += "WAVEfmt ", le(h, 16, 4), le(h, 1, 2), le(h, 2, 2), le(h, rate, 4), le(h, rate * 4, 4);
  le(h, 4, 2), le(h, 16, 2), h += "data", le(h, bytes, 4);
  std::ofstream o(file, std::ios::binary);
  o.write(h.data(), std::streamsize(h.size())).write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(bytes));
}

int main(int argc, char** argv) {
  if (argc < 3) return std::fprintf(stderr, "use: theme_chime_render <cache\\extracted> <out.wav> [field=value ...]\n"), 2;
  theme_chime::Recipe r = theme_chime::kRecipe;
  const struct {
    const char* name;
    int32_t* value;
  } fields[] = {{"bass", &r.bass},           {"shimmer", &r.shimmer},       {"swell", &r.swell},
                {"wet", &r.wet},             {"bells", &r.bells},           {"echo", &r.echo},
                {"echo_cut", &r.echo_cut},   {"bloom_room", &r.bloom_room}, {"bloom_damp", &r.bloom_damp},
                {"bloom_wet", &r.bloom_wet}, {"peak", &r.peak},             {"room", &r.room},
                {"damp", &r.damp},           {"speed", &r.speed},           {"highpass", &r.highpass}};
  for (int i = 3; i < argc; ++i) {
    const char* eq = std::strchr(argv[i], '=');
    bool found = false;
    if (eq && !std::strncmp(argv[i], "tail", size_t(eq - argv[i])) && eq - argv[i] == 4)
      r.tail = theme_chime::Ms(uint32_t(std::atoi(eq + 1))), found = true;  // in ms
    for (const auto& f : fields)
      if (eq && std::strlen(f.name) == size_t(eq - argv[i]) && !std::strncmp(argv[i], f.name, size_t(eq - argv[i])))
        *f.value = std::atoi(eq + 1), found = true;
    if (!found) return std::fprintf(stderr, "no field %s\n", argv[i]), 2;
  }
  const std::vector<int16_t> src = ReadPcm(std::string(argv[1]) + "/" + theme_chime::kSource);
  const std::vector<int16_t> pcm = theme_chime::Make(r, src.data(), src.size() / 2);
  if (pcm.empty()) return std::fprintf(stderr, "%s is missing or short\n", theme_chime::kSource), 1;
  Write(argv[2], pcm);
  std::printf("%.2f s  %s\n", double(pcm.size() / 2) / theme_chime::kRate, r.what);
}

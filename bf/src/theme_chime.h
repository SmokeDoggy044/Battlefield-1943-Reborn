// The achievement chime made from the Battlefield theme in the player's own game files (TekRantGaming, 18 Nov 2026:
// "the iconic Battlefield tone play as like a kind of esque achievement kind of sound ... when the user provides the
// game files, we can still accurately recreate this on everyone's systems").
//
// Only this recipe ships, never any of the sound: the first start extracts the game's menu music into the user's
// cache (shader_prep.cpp ExtractGameArt), and the chime is cut from it and dressed here. Everything is integer
// arithmetic (no floating point, no libm), so the same game files give the same bytes on every PC, compiler and
// system. Standard C++ only: the game (achievement_toast.cpp) and tools/theme_chime_render.cpp both build it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bf1943::theme_chime {

constexpr uint32_t kRate = 48000;  // the source and the result: 16 bit stereo at 48 kHz
constexpr uint32_t Ms(uint32_t ms) { return ms * (kRate / 1000); }

// Under user\cache\extracted: the menu theme the chime is cut from, and the made chime kept beside the menus' other
// sounds. The number in the chime's name goes up whenever the recipe changes, so an older one is made again.
inline constexpr char kSource[] = "music/menu.wav";
inline constexpr char kMade[] = "sounds/achievement-1.wav";

// How the chime is made. Levels are in 1/4096 and filter and reverb coefficients in 1/32768 unless said.
struct Recipe {
  // The cut from the menu theme: its first statement of the hook, "Da, da, da-daaa".
  uint32_t start, end;         // in samples of the source
  uint32_t fade_in, fade_out;  // linear, in samples
  int32_t speed;               // in 1/65536: tape style, pitch and tempo together (73562: a whole tone up)
  int32_t bass;                // its low end (below about 110 Hz) lifted by this much of itself
  int32_t highpass;            // then a one pole high pass: only the rumble goes
  int32_t shimmer;             // an octave up copy of its thinned self mixed in
  // The start: a reversed reverb of the first hit climbing into it.
  int32_t swell;
  uint32_t swell_length;       // in samples
  // The hall everything plays in.
  int32_t room, damp, wet;
  // The bells: a run up the swell, a chord on the first hit, and the hook again after the theme's bar.
  int32_t bells;               // a bell's level, of the cut's loudest sample
  // Under the bells' hook, the theme's own bar once more: softer, darker, further away.
  int32_t echo;                // its level
  int32_t echo_cut;            // its low pass
  // The ending: the last note of both rings on in a longer, darker hall.
  int32_t bloom_room, bloom_damp, bloom_wet;
  uint32_t tail;               // samples after the cut, the last 1.3 s of them drawn down to nothing
  int32_t peak;                // the result's loudest sample, out of 32767
  const char* what;            // for the log
};

// The recipe (TekRantGaming's pick of 18 Nov 2026, finished with the free hand he gave).
extern const Recipe kRecipe;

// The chime from `src` (the extracted menu theme's interleaved stereo samples, `frames` of them) at full volume,
// interleaved stereo; empty when the source is shorter than the cut.
std::vector<int16_t> Make(const Recipe& recipe, const int16_t* src, size_t frames);

}  // namespace bf1943::theme_chime

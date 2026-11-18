// The achievement chime from the Battlefield theme (theme_chime.h). Integer arithmetic only: C++23 makes >> of a
// negative number an arithmetic shift, / truncate toward zero and unsigned to signed conversion wrap, the same on
// every compiler.

#include "theme_chime.h"

#include <algorithm>

namespace bf1943::theme_chime {
namespace {

// Samples inside are 16 bit ones times 256, in 64 bits: room for every sum and product here, and fine enough that
// the rounding in the halls' feedback loops leaves no offset behind (plain 16 bit steps, rounded down, left one of a
// hundredth of full scale under the whole chime).
using Buffer = std::vector<int64_t>;  // interleaved stereo
constexpr int64_t kFine = 256;

constexpr int64_t Mul12(int64_t a, int64_t b) { return (a * b + 2048) >> 12; }   // a times b/4096, rounded
constexpr int64_t Mul15(int64_t a, int64_t b) { return (a * b + 16384) >> 15; }  // a times b/32768, rounded

// The sine of a phase (2^32 a turn), in 1/2^30: the series x - x^3/3! + ... + x^9/9! over a quarter turn (its
// constants are (pi/2)^k / k! in 1/2^30), the other quarters folded onto it. Good to 4 in a million.
int64_t Sin(uint32_t phase) {
  constexpr int64_t kQuarter = int64_t(1) << 30;
  int64_t x = int32_t(phase);  // half a turn each way
  if (x > kQuarter) x = 2 * kQuarter - x;
  else if (x < -kQuarter) x = -2 * kQuarter - x;
  const int64_t x2 = (x * x) >> 30;
  int64_t y = 172272;
  y = 5026995 - ((y * x2) >> 30);
  y = 85569306 - ((y * x2) >> 30);
  y = 693598668 - ((y * x2) >> 30);
  y = 1686629713 - ((y * x2) >> 30);
  return (y * x) >> 30;
}

// One pole filters in place, `a` in 1/32768. High: y = a (y' + x - x'). Low: y = y' + a (x - y').
void HighPass(Buffer& x, int64_t a) {
  for (size_t ch = 0; ch < 2; ++ch) {
    int64_t px = 0, py = 0;
    for (size_t i = ch; i < x.size(); i += 2) {
      const int64_t v = x[i];
      py = Mul15(a, py + v - px), px = v;
      x[i] = py;
    }
  }
}
void LowPass(Buffer& x, int64_t a) {
  for (size_t ch = 0; ch < 2; ++ch) {
    int64_t y = 0;
    for (size_t i = ch; i < x.size(); i += 2) y += Mul15(a, x[i] - y), x[i] = y;
  }
}
constexpr int64_t kThin = 31530;   // a high pass at about 300 Hz: what feeds the shimmer and the halls
constexpr int64_t kBassCut = 468;  // a low pass at about 110 Hz: the low end the recipe lifts

// Freeverb's layout (Jezar at Dreampoint, public domain), its delays scaled from 44.1 to 48 kHz, in fixed point:
// per channel eight damped comb filters side by side, then four all pass filters in a row. `stretch` (in 1/16)
// lengthens the combs: a larger hall, whose longer ring stays smooth.
struct Reverb {
  static constexpr uint32_t kCombs[8] = {1214, 1293, 1390, 1476, 1548, 1623, 1695, 1760};
  static constexpr uint32_t kPasses[4] = {605, 480, 371, 245};
  static constexpr uint32_t kSpread = 25;  // the right channel's delays are this much longer
  struct Line {
    std::vector<int64_t> buf;
    size_t at = 0;
    int64_t filter = 0;
  };
  Line combs[2][8], passes[2][4];
  int64_t room, damp;
  Reverb(int64_t room, int64_t damp, uint32_t stretch) : room(room), damp(damp) {
    for (uint32_t ch = 0; ch < 2; ++ch) {
      for (int i = 0; i < 8; ++i) combs[ch][i].buf.assign(kCombs[i] * stretch / 16 + ch * kSpread, 0);
      for (int i = 0; i < 4; ++i) passes[ch][i].buf.assign(kPasses[i] + ch * kSpread, 0);
    }
  }
  // One frame: in is the mono input, out the wet left and right.
  void Step(int64_t in, int64_t out[2]) {
    for (int ch = 0; ch < 2; ++ch) {
      int64_t sum = 0;
      for (Line& c : combs[ch]) {
        const int64_t v = c.buf[c.at];
        c.filter = (v * (32768 - damp) + c.filter * damp + 16384) >> 15;
        c.buf[c.at] = in + Mul15(c.filter, room);
        if (++c.at == c.buf.size()) c.at = 0;
        sum += v;
      }
      for (Line& p : passes[ch]) {
        const int64_t v = p.buf[p.at];
        p.buf[p.at] = sum + v / 2;
        sum = v - sum;
        if (++p.at == p.buf.size()) p.at = 0;
      }
      out[ch] = sum;
    }
  }
};

// The wet sound of `send` over `frames` (silence past its end), at `wet` in 1/4096. `width` (in 1/4096) is how far
// apart its two sides stay: at 4096 each keeps its own, below that they share (a long ring of held notes can
// otherwise drift out of phase between the ears, and thin out on one speaker).
Buffer Wet(const Buffer& send, size_t frames, int64_t room, int64_t damp, int64_t wet, uint32_t stretch, int64_t width) {
  Reverb r(room, damp, stretch);
  Buffer out(frames * 2, 0);
  for (size_t i = 0; i < frames; ++i) {
    const int64_t in = i * 2 + 1 < send.size() ? Mul15(send[i * 2] + send[i * 2 + 1], 492) : 0;  // x 0.015 a side
    int64_t o[2];
    r.Step(in, o);
    const int64_t own = (4096 + width) / 2, other = (4096 - width) / 2;
    out[i * 2] = Mul12(Mul12(o[0], own) + Mul12(o[1], other), wet);
    out[i * 2 + 1] = Mul12(Mul12(o[1], own) + Mul12(o[0], other), wet);
  }
  return out;
}

// ---- the bells

// A bell's partials: where each sits above the note (in 1/4096), how loud (in 1/4096 of the note's level) and how
// long it rings (in 1/4096 of the note's ring). The note and its octave carry the tone; the two between and above
// them are a struck metal bar's own overtones (2.76 and 5.4 times the note), gone in a moment: the strike.
struct Partial {
  int64_t ratio, level, ring;
};
constexpr Partial kBell[] = {{4096, 4096, 4096}, {8192, 1024, 2253}, {12288, 286, 1434}, {11289, 819, 410}, {22135, 450, 164}};

// The notes, in mHz at concert pitch (the menu theme's own partials sit within 3 cents of it). The recipe plays the
// theme a whole tone up, F minor to G minor.
constexpr int64_t kG4 = 391995, kG5 = 783991, kBb5 = 932328, kD6 = 1174659, kF6 = 1396913, kG6 = 1567982, kD7 = 2349318,
                  kG7 = 3135963;

// One bell note into `out` from frame `at`: `mhz` its pitch, `level` its peak (as the buffer's samples), `pan` 0 left
// to 4096 right, `ring` the ms its tone takes to fall to about a third. Its main voice carries 86% of it; two faint
// ones 2.5 cents under and over, left and right of the pan, make a held note shimmer slowly (shallow enough that a
// mono speaker never hears it pulse). Each partial rises in 2 ms and dies away exponentially.
void Bell(Buffer& out, size_t at, int64_t mhz, int64_t level, int64_t pan, uint32_t ring) {
  const size_t frames = out.size() / 2;
  const int64_t inc = (mhz << 32) / (int64_t(kRate) * 1000);  // phase a sample
  const int64_t rise = Ms(2);
  static constexpr struct {
    int64_t cents10, share, lean;  // tenths of a cent off, its share of the level in 1/4096, its place off the pan
  } kVoices[] = {{0, 3522, 0}, {-25, 287, -1100}, {25, 287, 1100}};
  for (const auto& voice : kVoices) {
    const int64_t tune = inc + ((inc * voice.cents10 * 3877) >> 26);
    const int64_t side = std::clamp<int64_t>(pan + voice.lean, 0, 4096);
    for (const Partial& p : kBell) {
      const int64_t step = (tune * p.ratio) >> 12;
      if (step >= int64_t(1) << 31) continue;  // above what the rate can carry
      const int64_t length = std::max<int64_t>(int64_t(Ms(ring)) * p.ring >> 12, 16);
      const int64_t decay = (int64_t(1) << 24) - (int64_t(1) << 24) / length;  // a sample's fall, in 1/2^24
      int64_t env = Mul12(Mul12(level, p.level), voice.share) << 12;  // 12 bits finer than the samples
      uint32_t phase = 0;
      for (size_t i = 0; at + i < frames; ++i) {
        int64_t a = env >> 12;
        if (int64_t(i) < rise) {
          a = a * int64_t(i) / rise;
        } else {
          env = (env * decay) >> 24;
          if (a < 8) break;  // a thirtieth of a 16 bit step: gone
        }
        const int64_t v = (Sin(phase) * a) >> 30;
        out[(at + i) * 2] += Mul12(v, 4096 - side), out[(at + i) * 2 + 1] += Mul12(v, side);
        phase += uint32_t(step);
      }
    }
  }
}

// The hook in the menu theme, "Da, da, da-daaa" (TekRantGaming, 18 Nov: "its at the very start ends about 2.5s in"):
// its four hits and the next bar's first, in ms from the theme's start (where the sound above 150 Hz rises
// steepest). It hammers one note, F, three times and lands on the F an octave up over an open chord (F, B flat, C);
// at the recipe's speed that is G, and G, C, D.
constexpr uint32_t kHits[] = {272, 777, 1292, 1642};
constexpr uint32_t kNextBar = 2293;

}  // namespace

std::vector<int16_t> Make(const Recipe& c, const int16_t* src, size_t frames) {
  if (c.end <= c.start || frames < c.end || c.speed <= 0) return {};
  // The cut, at its speed.
  size_t n = c.end - c.start;
  Buffer x(n * 2);
  for (size_t i = 0; i < n * 2; ++i) x[i] = int64_t(src[size_t(c.start) * 2 + i]) * kFine;
  if (c.speed != 65536) {  // out[i] = x[k] + (x[k+1] - x[k]) * frac, k.frac = i * speed (16.16)
    const size_t m = size_t((int64_t(n - 1) * 65536) / c.speed);
    Buffer y(m * 2);
    for (size_t i = 0; i < m; ++i) {
      const int64_t p = int64_t(i) * c.speed, k = p >> 16, fr = p & 0xFFFF;
      for (size_t ch = 0; ch < 2; ++ch) {
        const int64_t a = x[size_t(k) * 2 + ch], b = x[size_t(k + 1) * 2 + ch];
        y[i * 2 + ch] = a + (((b - a) * fr) >> 16);
      }
    }
    x = std::move(y), n = m;
  }
  for (size_t i = 0; i < n; ++i) {
    int64_t env = 4096;
    if (c.fade_in) env = std::min<int64_t>(env, int64_t(i) * 4096 / c.fade_in);
    if (c.fade_out) env = std::min<int64_t>(env, int64_t(n - 1 - i) * 4096 / c.fade_out);
    x[i * 2] = Mul12(x[i * 2], env), x[i * 2 + 1] = Mul12(x[i * 2 + 1], env);
  }
  // Its low end lifted, the rumble under it taken off.
  if (c.bass) {
    Buffer low = x;
    LowPass(low, kBassCut);
    for (size_t i = 0; i < n * 2; ++i) x[i] += Mul12(low[i], c.bass);
  }
  if (c.highpass) HighPass(x, c.highpass);
  // The shimmer: an octave up of the thinned cut (no low end to carry up), by two read heads sweeping a 2048 sample
  // window at double speed, crossfaded.
  Buffer thin = x;
  HighPass(thin, kThin);
  Buffer dry = x;
  if (c.shimmer) {
    constexpr int64_t kW = 2048;
    const Buffer from = thin;
    for (size_t i = 0; i < n; ++i) {
      int64_t sum[2] = {0, 0};
      for (int64_t tap = 0; tap < 2; ++tap) {
        const int64_t p = (int64_t(i) + tap * kW / 2) % kW, d = kW - p, g = std::min(p, kW - p);
        const int64_t at = int64_t(i) - d;
        if (at < 0) continue;
        sum[0] += from[size_t(at) * 2] * g, sum[1] += from[size_t(at) * 2 + 1] * g;
      }
      const int64_t l = Mul12(sum[0] >> 10, c.shimmer), r = Mul12(sum[1] >> 10, c.shimmer);
      dry[i * 2] += l, dry[i * 2 + 1] += r, thin[i * 2] += l, thin[i * 2 + 1] += r;
    }
  }

  // Laid out: the swell, the cut, its tail. `out` is the mix; `hall` and `bloom` are what the two halls hear (the
  // thinned sound: the bass stays dry).
  const size_t lead_in = c.swell ? c.swell_length : 0, total = lead_in + n + c.tail;
  auto at_theme = [&](uint32_t ms) {  // a time in the menu theme as a frame of the result
    return lead_in + size_t(std::max<int64_t>(int64_t(Ms(ms)) - int64_t(c.start), 0) * 65536 / c.speed);
  };
  const size_t bar = at_theme(kNextBar) - at_theme(kHits[0]);
  Buffer out(total * 2, 0), hall(total * 2, 0), bloom(total * 2, 0);
  for (size_t i = 0; i < n * 2; ++i) out[lead_in * 2 + i] = dry[i], hall[lead_in * 2 + i] = thin[i];

  // The theme's bar once more a bar later, under the bells' hook: softer and darker, the orchestra's own echo. Its
  // last note also goes to the long hall (doubled: the fade out is the orchestra's), eased in over 50 ms so the hit's
  // crack stays out of it, and falls away
  // from 120 ms after its hit, so the orchestra melts into that hall instead of stopping.
  if (c.echo) {
    Buffer far = dry, far_thin = thin;
    LowPass(far, c.echo_cut), LowPass(far_thin, c.echo_cut);
    const size_t last = at_theme(kHits[3]) - lead_in;
    for (size_t i = 0; i < n && lead_in + bar + i < total; ++i) {
      const size_t o = (lead_in + bar + i) * 2;
      const int64_t ease = i + Ms(15) < last ? 0 : std::min<int64_t>(4096, int64_t(i + Ms(15) - last) * 4096 / Ms(50));
      const size_t melt = std::min(last + Ms(120), n - 1);
      const int64_t fall = i < melt ? 4096 : int64_t(n - 1 - i) * 4096 / int64_t(n - melt);
      for (size_t ch = 0; ch < 2; ++ch) {
        const int64_t send = Mul12(far_thin[i * 2 + ch], c.echo);
        out[o + ch] += Mul12(Mul12(far[i * 2 + ch], c.echo), fall), hall[o + ch] += Mul12(send, fall);
        bloom[o + ch] += 2 * Mul12(send, ease);
      }
    }
  }

  // The bells.
  if (c.bells) {
    int64_t loudest = 1;
    for (int64_t v : dry) loudest = std::max(loudest, v < 0 ? -v : v);
    const int64_t level = Mul12(loudest, c.bells);
    Buffer bells(total * 2, 0), ending(total * 2, 0);
    // A run climbing the swell into the first hit, and on the hit a G minor chord, rolled.
    static constexpr int64_t kRun[] = {kG5, kD6, kF6, kG6};
    if (lead_in >= Ms(400))
      for (size_t k = 0; k < 4; ++k)
        Bell(bells, lead_in - Ms(330) + k * Ms(75), kRun[k], level * int64_t(30 + k * 12) / 100, k & 1 ? 2800 : 1300, 110);
    const size_t first = at_theme(kHits[0]) + Ms(8);
    Bell(bells, first, kG5, level, 1600, 230);
    Bell(bells, first + Ms(14), kBb5, level * 70 / 100, 2600, 250);
    Bell(bells, first + Ms(28), kD6, level * 60 / 100, 2048, 240);
    // Nothing over the theme's own bar (TekRantGaming: "it plays over the theme"). Then the hook on the bells, on the
    // echo's hits ("i want the sparkles to end with Da da dadaaa ... and have it fade softly out"): G over the G
    // below three times, then the G above, held, over its lower octaves and the fifth: the theme's open chord.
    for (size_t k = 0; k < 3; ++k) {
      const size_t at = at_theme(kHits[k]) + bar + Ms(6);
      Bell(bells, at, kG5, level * 90 / 100, k & 1 ? 2600 : 1500, 260);
      Bell(bells, at, kG4, level * 55 / 100, 2048, 260);
    }
    const size_t at = at_theme(kHits[3]) + bar + Ms(6);
    Bell(ending, at, kG6, level * 62 / 100, 2048, 1000);
    Bell(ending, at + Ms(4), kD6, level * 25 / 100, 1400, 950);
    Bell(ending, at + Ms(8), kG5, level * 44 / 100, 2700, 1100);
    Bell(ending, at + Ms(12), kG4, level * 19 / 100, 2048, 1100);
    Bell(ending, at + Ms(30), kD7, level * 7 / 100, 1200, 350);  // and a small glint rising off it: the diamond
    Bell(ending, at + Ms(60), kG7, level * 9 / 100, 2900, 350);
    for (size_t i = 0; i < total * 2; ++i) {
      const int64_t v = bells[i] + ending[i];
      out[i] += v, hall[i] += v, bloom[i] += ending[i] / 2;
    }
  }

  // The halls: everything in the one, the ending's last note (the bells' at half) also in the longer, darker one.
  if (c.wet) {
    const Buffer w = Wet(hall, total, c.room, c.damp, c.wet, 16, 4096);
    for (size_t i = 0; i < total * 2; ++i) out[i] += w[i];
  }
  if (c.bloom_wet) {
    const Buffer w = Wet(bloom, total, c.bloom_room, c.bloom_damp, c.bloom_wet, 31, 2458);
    for (size_t i = 0; i < total * 2; ++i) out[i] += w[i];
  }
  // The swell: the hall's ring of the first hit (its first 60 ms), reversed and faded in, so it climbs to where the
  // cut begins. Read from 70 ms into the ring: before that it is still building (its first echoes come after 25 ms),
  // and reversed that would fall away just before the hit.
  if (c.swell && lead_in) {
    const size_t hit = std::min<size_t>(Ms(60), n), skip = Ms(70);
    Buffer first(hit * 2);
    for (size_t i = 0; i < hit; ++i) {
      const int64_t env = int64_t(hit - i) * 4096 / int64_t(hit);
      first[i * 2] = Mul12(dry[i * 2], env), first[i * 2 + 1] = Mul12(dry[i * 2 + 1], env);
    }
    const Buffer w = Wet(first, skip + lead_in, c.room, c.damp, 4096, 16, 4096);
    for (size_t k = 0; k < lead_in; ++k) {
      const size_t from = skip + lead_in - 1 - k;  // the ring read backwards
      const int64_t env = std::min<int64_t>(4096, int64_t(k) * 4096 * 2 / int64_t(lead_in));
      out[k * 2] += Mul12(Mul12(w[from * 2], env), c.swell), out[k * 2 + 1] += Mul12(Mul12(w[from * 2 + 1], env), c.swell);
    }
  }
  // The end: the bells and the long hall die away by themselves; the tail's last 1.3 s (or all of a shorter one) is
  // drawn down to nothing in a straight line on top, which together is a steady fall of about 20 dB a second.
  const size_t fade = std::min<size_t>(c.tail, Ms(1300));
  for (size_t k = 0; k < fade; ++k) {
    const size_t i = total - fade + k;
    const int64_t env = int64_t(fade - 1 - k) * 65536 / int64_t(fade);
    out[i * 2] = (out[i * 2] * env) >> 16, out[i * 2 + 1] = (out[i * 2 + 1] * env) >> 16;
  }
  int64_t loudest = 1;
  for (int64_t v : out) loudest = std::max(loudest, v < 0 ? -v : v);
  const int64_t peak = std::clamp(c.peak, 0, 32767);
  std::vector<int16_t> pcm(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    const int64_t v = out[i] * peak * 2;
    pcm[i] = int16_t(std::clamp<int64_t>((v + (v < 0 ? -loudest : loudest)) / (2 * loudest), -32768, 32767));
  }
  return pcm;
}

// Times are at 48 kHz in the menu theme (music\menu.wav, BMUS_Menu_001: F minor, about 120 beats a minute; the cut is
// its first bar). In order: the cut 0.25 to 2.30 s, eased in over 30 ms (no jump) and out over 300, a whole tone up;
// its low end lifted by 40%, a high pass at 60 Hz, the shimmer at 29%; a swell of 0.55 s; the hall; the bells at 32%
// of the cut's loudest; the echo at 45% under a low pass at 3.5 kHz; the long hall; 3.3 s of tail (5.7 s in all, as
// long as the popup shows); a peak under half of full scale (TekRantGaming: "rather loud it might jump scare people").
const Recipe kRecipe = {Ms(250), Ms(2300), Ms(30), Ms(300), 73562, 1640,  32513, 1200,  24000,    Ms(550),
                        28180,   9000,     14000,  1300,    1850,  12044, 28500, 13000, 20000,    Ms(3300),
                        12000,   "the menu theme's hook as a diamond unlock"};

}  // namespace bf1943::theme_chime

// NativeXRenderer - the processor cores this process may run on (internal).

#pragma once

#include <cstdint>
#include <thread>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace native_x_renderer {

inline void Pause() {
#if defined(_M_X64) || defined(__x86_64__)
  _mm_pause();
#else
  std::this_thread::yield();
#endif
}

// Physical cores among the processors this process may run on (its affinity): a dual core with Hyper-Threading is 2,
// not 4. Read again at most once a second (the runtime and tests change the affinity while the game runs). At least 1.
uint32_t AvailableCores();

// Whether the renderer's waits may spin before they sleep: only with at least native_x_renderer_spin_cores cores. On
// fewer, the thread a spin waits for is usually waiting for the very core the spin holds.
bool SpinWaitsAllowed();

}  // namespace native_x_renderer

#pragma once

#include <chrono>

namespace sioxx
{

// Returns the delay for a one-based reconnect attempt. jitter_sample is in
// [0, 1]: 0 selects minimum jitter, 0.5 the exponential delay, and 1 maximum.
std::chrono::milliseconds reconnect_delay_for_attempt(
  std::chrono::milliseconds initial_delay, std::chrono::milliseconds max_delay,
  double jitter_factor, int attempt, double jitter_sample);

}  // namespace sioxx

#include "sioxx/reconnection.hpp"

#include <algorithm>
#include <cmath>

namespace sioxx
{

std::chrono::milliseconds reconnect_delay_for_attempt(
  std::chrono::milliseconds initial_delay, std::chrono::milliseconds max_delay,
  double jitter_factor, int attempt, double jitter_sample)
{
  const auto initial = std::max<int64_t>(0, initial_delay.count());
  const auto maximum = std::max<int64_t>(0, max_delay.count());
  const int exponent = std::max(0, attempt - 1);
  const long double multiplier = std::ldexp(1.0L, std::min(exponent, 62));
  const long double exponential = std::min<long double>(
    static_cast<long double>(maximum), initial * multiplier);
  const double factor = std::clamp(jitter_factor, 0.0, 1.0);
  const double sample = std::clamp(jitter_sample, 0.0, 1.0);
  const long double delayed = std::min<long double>(
    static_cast<long double>(maximum),
    exponential * (1.0L - factor + 2.0L * factor * sample));
  return std::chrono::milliseconds(static_cast<int64_t>(std::llround(delayed)));
}

}  // namespace sioxx

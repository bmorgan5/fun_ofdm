/*! \file metrics.h
 * \brief The clock and the build switch that the receiver's metrics share.
 *
 * Building with `--config=no_metrics` defines FUN_OFDM_METRICS as 0, which
 * turns every timestamp, histogram record and trace event into nothing, to
 * measure what they cost. The counters stay: they're a few additions per
 * frame.
 */

#ifndef METRICS_METRICS_H
#define METRICS_METRICS_H

#include <chrono>
#include <cstdint>

#ifndef FUN_OFDM_METRICS
#define FUN_OFDM_METRICS 1
#endif

namespace fun::metrics {
//! Whether the build records timestamps, histograms and trace events
constexpr bool ENABLED = FUN_OFDM_METRICS;

/*!
 * \brief A time in nanoseconds on std::chrono::steady_clock, which is Linux's
 * CLOCK_MONOTONIC, as Perfetto's traces record it.
 */
using time_point = std::int64_t;

/*!
 * \brief The time now, or 0 if the build doesn't record metrics.
 */
inline time_point now() {
  if constexpr (!ENABLED) return 0;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace fun::metrics

#endif  // METRICS_METRICS_H

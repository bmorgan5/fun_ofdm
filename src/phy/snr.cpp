/*! \file snr.cpp
 *  \brief C++ file for the SNR estimate and its histogram.
 */

#include "snr.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "preamble.h"

namespace fun {
double lts_snr_db(const std::complex<double>* first,
                  const std::complex<double>* second) {
  double sum = 0;         // Σ |Y1 + Y2|²
  double difference = 0;  // Σ |Y1 - Y2|²
  std::size_t used = 0;
  for (std::size_t k = 0; k < 64; k++) {
    if (LTS_FREQ_DOMAIN[k] == 0.0) continue;
    sum += std::norm(first[k] + second[k]);
    difference += std::norm(first[k] - second[k]);
    used++;
  }
  // N = difference / (2 used), and S = sum / (4 used) - N / 2
  //   = (sum - difference) / (4 used), so the SNR, used S / (64 N), is
  //   used (sum - difference) / (2 · 64 · difference)
  double signal = static_cast<double>(used) * (sum - difference);
  double noise = 2 * 64 * difference;
  if (signal <= 0) return -std::numeric_limits<double>::infinity();
  if (noise == 0) return std::numeric_limits<double>::infinity();
  return 10 * std::log10(signal / noise);
}

void snr_histogram::record(double db) {
  if (std::isnan(db)) return;
  double bin = std::floor((db - MIN_DB) / BIN_DB);
  bin = std::clamp(bin, 0.0, static_cast<double>(BINS - 1));
  m_bins[static_cast<std::size_t>(bin)]++;
  m_count++;
}

double snr_histogram::percentile(double percentile) const {
  if (m_count == 0) return std::numeric_limits<double>::quiet_NaN();
  // The nearest rank, counting from 1: the first value with at least the
  // percentage of the values at or below it. The product can be a rounding
  // error above a whole number, like 7.000...1 for the 7th percentile of
  // 100 values, which isn't the 8th.
  double rank = std::max(
      1.0, std::ceil(percentile / 100 * static_cast<double>(m_count) - 1e-9));
  std::uint64_t seen = 0;
  for (std::size_t bin = 0; bin < BINS; bin++) {
    seen += m_bins[bin];
    if (static_cast<double>(seen) >= rank)
      return MIN_DB + (static_cast<double>(bin) + 0.5) * BIN_DB;
  }
  return MAX_DB - BIN_DB / 2;
}
}  // namespace fun

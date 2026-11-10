/*! \file histogram.cpp
 * \brief C++ file for the receiver's histograms of times.
 */

#include "histogram.h"

#include <algorithm>
#include <new>

#include <hdr/hdr_histogram.h>

#include "metrics.h"

namespace fun::metrics {
/*!
 * \brief A new, empty hdr_histogram with the class's range and precision.
 * \throws std::bad_alloc if it can't be allocated.
 */
static hdr_histogram* new_histogram() {
  hdr_histogram* h = nullptr;
  if (hdr_init(1, histogram::MAX_VALUE, 3, &h) != 0) throw std::bad_alloc();
  return h;
}

histogram::histogram() : m_histogram(new_histogram()) {}

histogram::~histogram() { hdr_close(m_histogram); }

histogram::histogram(const histogram& other) : m_histogram(new_histogram()) {
  add(other);
}

histogram& histogram::operator=(const histogram& other) {
  if (this != &other) {
    hdr_reset(m_histogram);
    add(other);
  }
  return *this;
}

void histogram::record(std::int64_t ns) {
  if constexpr (!ENABLED) return;
  hdr_record_value(m_histogram, std::clamp<std::int64_t>(ns, 0, MAX_VALUE));
}

void histogram::add(const histogram& other) {
  hdr_add(m_histogram, other.m_histogram);
}

std::int64_t histogram::count() const { return m_histogram->total_count; }

std::int64_t histogram::percentile(double percentile) const {
  return hdr_value_at_percentile(m_histogram, percentile);
}

std::int64_t histogram::max() const { return hdr_max(m_histogram); }

std::vector<histogram::bucket> histogram::buckets() const {
  std::vector<bucket> buckets;
  hdr_iter iter;
  hdr_iter_recorded_init(&iter, m_histogram);
  while (hdr_iter_next(&iter)) {
    buckets.push_back(
        {.value = iter.highest_equivalent_value, .count = iter.count});
  }
  return buckets;
}
}  // namespace fun::metrics

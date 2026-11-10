/*! \file histogram.h
 * \brief Header file for the receiver's histograms of times.
 */

#ifndef METRICS_HISTOGRAM_H
#define METRICS_HISTOGRAM_H

#include <cstdint>
#include <vector>

struct hdr_histogram;

namespace fun::metrics {
/*!
 * \brief A histogram of times in nanoseconds, from 1 ns to an hour, to 3
 * significant digits, with HdrHistogram_c.
 *
 * Recording a value takes a few nanoseconds, and neither locks nor
 * allocates, so a thread can record into its own histogram on every batch.
 * A histogram isn't thread-safe: each thread records into its own, and they're
 * read or added together once the threads have stopped, or between batches.
 *
 * In a build without metrics (metrics.h), record() does nothing.
 */
class histogram {
 public:
  //! The largest value: an hour, in nanoseconds
  static constexpr std::int64_t MAX_VALUE = 3600'000'000'000;

  //! One of the histogram's buckets
  struct bucket {
    std::int64_t value;  //!< The largest value the bucket holds
    std::int64_t count;  //!< How many values it holds
  };

  histogram();
  ~histogram();
  histogram(const histogram& other);
  histogram& operator=(const histogram& other);

  /*!
   * \brief Records a time.
   * \param ns The time, in nanoseconds. Below 0 counts as 0, and above
   * #MAX_VALUE as #MAX_VALUE.
   */
  void record(std::int64_t ns);

  /*!
   * \brief Adds another histogram's values to this one's.
   */
  void add(const histogram& other);

  //! The number of values recorded
  std::int64_t count() const;

  /*!
   * \brief The value below which a percentage of the values fall.
   * \param percentile From 0 to 100, e.g. 99.9
   * \return The value, to 3 significant digits, or 0 if there are none.
   */
  std::int64_t percentile(double percentile) const;

  //! The largest value recorded, to 3 significant digits, or 0
  std::int64_t max() const;

  //! The buckets that hold any values, in order
  std::vector<bucket> buckets() const;

 private:
  hdr_histogram* m_histogram;  //!< HdrHistogram_c's histogram
};
}  // namespace fun::metrics

#endif  // METRICS_HISTOGRAM_H

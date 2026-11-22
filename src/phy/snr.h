/*! \file snr.h
 *  \brief Estimates a frame's SNR from its long training symbols, and keeps a
 *  histogram of the estimates.
 *
 *  Each frame starts with two identical long training symbols (LTS, see
 *  preamble.h). On each of the 52 subcarriers they use, the receiver gets
 *
 *  ~~~
 *  Y1[k] = H[k] X[k] + N1[k]      Y2[k] = H[k] X[k] + N2[k]
 *  ~~~
 *
 *  so their difference is only noise, with twice the noise's power, and their
 *  sum is twice the signal plus noise with twice its power:
 *
 *  ~~~
 *  noise per subcarrier   N = Σ |Y1 - Y2|² / (2 · 52)
 *  signal per subcarrier  S = Σ |Y1 + Y2|² / (4 · 52) - N / 2
 *  ~~~
 *
 *  The SNR is then in the channel's terms (channel.h): the signal's power
 *  over the noise's, in the time domain. The signal is on 52 subcarriers and
 *  the noise on all 64, so
 *
 *  ~~~
 *  SNR = 52 S / (64 N)
 *  ~~~
 *
 *  The two symbols are 3.2 µs apart, so anything that changes between them
 *  counts as noise: a frequency offset that timing_sync doesn't correct
 *  (see its \\bug), or a channel that changes. At high SNR, those are what
 *  limit the estimate.
 */

#ifndef SNR_H
#define SNR_H

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace fun {
/*!
 * \brief Estimates a frame's SNR from its two long training symbols.
 * \param first The first LTS, after the FFT: 64 subcarriers
 * \param second The second LTS, likewise
 * \return The SNR in dB, as channel_params::snr_db defines it. It's infinite
 * if the symbols are identical, as without noise, and minus infinity if their
 * difference is as large as their sum.
 */
double lts_snr_db(const std::complex<double>* first,
                  const std::complex<double>* second);

/*!
 * \brief A histogram of SNRs in dB, in 0.1 dB bins from #MIN_DB to #MAX_DB.
 *
 * It's a fixed array, so recording a value neither locks nor allocates.
 * Values outside the range count in the first or last bin: a frame without
 * noise, whose estimate is infinite, comes out as 59.95 dB.
 */
class snr_histogram {
 public:
  static constexpr double MIN_DB = -20;     //!< The lowest bin's start
  static constexpr double MAX_DB = 60;      //!< The highest bin's end
  static constexpr double BIN_DB = 0.1;     //!< Each bin's width
  static constexpr std::size_t BINS = 800;  //!< (MAX_DB - MIN_DB) / BIN_DB

  /*!
   * \brief Records an SNR. NaN isn't recorded.
   */
  void record(double db);

  //! The number of SNRs recorded
  std::uint64_t count() const { return m_count; }

  /*!
   * \brief The SNR below which a percentage of the values fall.
   * \param percentile From 0 to 100, like 50 for the median
   * \return The middle of the bin that holds it, or NaN if there are no
   * values.
   */
  double percentile(double percentile) const;

 private:
  std::array<std::uint64_t, BINS> m_bins{};  //!< The count in each bin
  std::uint64_t m_count = 0;                 //!< See count()
};
}  // namespace fun

#endif  // SNR_H

/*! \file symbol_mapper.h
 *  \brief Places data points and pilots on the 64 subcarriers, and back.
 *
 *  Each OFDM symbol has 64 subcarriers, numbered -32 to 31, stored at index
 *  subcarrier + 32 (802.11a-1999 section 17.3.5.9):
 *
 *  ~~~
 *  -32 ... -27   empty: guard band, keeps the signal inside its channel
 *  -26 ...  -1   data, with pilots at -21 and -7
 *    0           empty: DC, where radios have a spike
 *    1 ...  26   data, with pilots at 7 and 21
 *   27 ...  31   empty: guard band
 *  ~~~
 *
 *  That leaves 48 data subcarriers, filled in order from -26 up.
 *
 *  The 4 pilots are known values, so the receiver can measure how the phase
 *  of each symbol has drifted (see phase_tracker.h). They're BPSK points,
 *  (1, 1, 1, -1) at (-21, -7, 7, 21), all multiplied by the polarity p_n of
 *  symbol n. p is a fixed 127-long ±1 sequence, starting at the SIGNAL
 *  symbol, which keeps the pilots from adding up to a tone.
 *
 *  demap() keeps only the 48 data subcarriers.
 */

#ifndef SYMBOL_MAPPER_H
#define SYMBOL_MAPPER_H

#include <complex>
#include <cstdint>
#include <vector>

namespace fun {
/*!
 * \brief The symbol_mapper class
 *
 *  The symbol mapper class takes the stream of modulated data and maps it
 *  into symbols by mapping the data, pilots, and nulls onto their respective
 *  subcarriers. Conversely it also extracts the data from received symbols.
 */
class symbol_mapper {
 public:
  /*!
   * \brief Constructor for symbol mapper class.
   */
  symbol_mapper();

  /*!
   * \brief Maps the data, pilots, and nulls to their respective subcarriers
   * \param data_samples Vector of modulated data to be mapped into symbols
   * \return Vector of symbols with data, pilots, and nulls
   */
  std::vector<std::complex<double>> map(
      std::vector<std::complex<double>> data_samples);

  /*!
   * \brief Extracts the data from the symbols throwing out the nulls and pilots
   * \param samples Vector of symbols to extract data from
   * \return Vector of data samples
   */
  std::vector<std::complex<double>> demap(
      std::vector<std::complex<double>> samples);

  /*!
   * \brief Gets the current active map of data, pilots, and nulls.
   * \return The current active map.
   */
  std::vector<std::uint8_t> get_active_map();

 private:
  static const std::vector<std::uint8_t>
      m_active_map;  //!< The current map of data, pilots, and nulls.

  static const double POLARITY[127];  //!< The Pilot Polarity Sequence

  static const std::complex<double> PILOTS[4];  //!< The 4 Pilot symbols

  int m_data_subcarrier_count;  //!< Number of data subcarriers.
};
}  // namespace fun

#endif  // SYMBOL_MAPPER_H

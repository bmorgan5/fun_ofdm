/*! \file fft.h
 *  \brief 64-point FFTs, wrapping FFTW, in the subcarrier order of the PHY.
 *
 *  The inverse FFT turns a symbol's 64 subcarrier values into 64 time
 *  samples: the sum of 64 tones, each with that subcarrier's amplitude and
 *  phase. The forward FFT turns them back. That's all OFDM needs to send 64
 *  points at once.
 *
 *  Subcarriers are in order from -32 to 31, so subcarrier k is at index
 *  k + 32. FFTW puts 0 first, so both functions reorder. inverse() runs on a
 *  whole vector of symbols, and scales by 1/64 so forward(inverse(x)) is x.
 *  forward() runs on one symbol in place.
 *
 *  \bug fft has no destructor, so it never frees its four FFTW buffers or its
 *  two FFTW plans. LeakSanitizer ignores this leak in the tests
 *  (src/phy/lsan_suppressions.txt) until it's fixed. Copying an fft copies
 *  the raw pointers, so the fix must also forbid or define copying.
 */

#ifndef FFT_H
#define FFT_H

#include <complex>
#include <cstddef>
#include <vector>

#include <fftw3.h>

namespace fun {
/*!
 * \brief The fft class
 *
 * This class contains is a wrapper for the fftw3 library and contains the
 * functions and necessary parameters for performing the IFFTs and FFTs in the
 * transmit and receive chains respectively.
 */
class fft {
 public:
  /*!
   * \brief Constructor for fft object
   * \param fft_length length of FFT - i.e. 64 point FFT
   */
  fft(int fft_length);

  /*!
   * \brief In place 64 point forward FFT.
   * \param data Array of 64 complex samples in time domain to be
   *  converted to frequency domain.
   */
  void forward(std::complex<double> data[64]);

  /*!
   * \brief In place inverse FFT of input data.
   * \param data Vector of complex doubles in frequency domain to be
   *  converted to time domain. The length of the data vector must
   *  be an integer multiple of #m_fft_length.
   */
  void inverse(std::vector<std::complex<double>>& data);

 private:
  /*!
   * \brief Mapping to/from FFT order.
   */
  static const std::size_t fft_map[64];

  /*!
   * \brief Length of FFT. In 802.11a it is always a 64 Point FFT since
   *  There are 64 subcarriers.
   */
  int m_fft_length;

  /*!
   * \brief Forward input buffer for use by fftw3 library.
   */
  fftw_complex* m_fftw_in_forward;

  /*!
   * \brief Forward output buffer for use by fftw3 library.
   */
  fftw_complex* m_fftw_out_forward;

  /*!
   * \brief Inverse input buffer for use by fftw3 library.
   */
  fftw_complex* m_fftw_in_inverse;

  /*!
   * \brief Inverse output buffer for use by fftw3 library.
   */
  fftw_complex* m_fftw_out_inverse;

  /*!
   * \brief Forward FFT plan for use by fftw3 library.
   */
  fftw_plan m_fftw_plan_forward;

  /*!
   * \brief Inverse FFT plan for use by fftw3 library.
   */
  fftw_plan m_fftw_plan_inverse;
};
}  // namespace fun

#endif  // FFT_H

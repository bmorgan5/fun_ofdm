/*! \file frame_builder.h
 *  \brief Builds a frame's samples from a payload: the whole transmitter.
 *
 *  ~~~
 *  payload
 *    → ppdu::encode()      SIGNAL and data, 48 points per symbol
 *    → symbol_mapper       add pilots and empty subcarriers: 64 per symbol
 *    → fft::inverse()      64 time samples per symbol
 *    → cyclic prefix       copy each symbol's last 16 samples to its front
 *    → preamble            320 samples in front
 *  = 320 + 80 × (1 + data symbols) samples, for the USRP
 *  ~~~
 *
 *  The cyclic prefix gives echoes of each symbol time to die out before the
 *  receiver's 64-sample window for the next one starts. Inside the window, an
 *  echo shorter than the prefix is a circular shift of the symbol, which the
 *  FFT turns into a gain and phase on each subcarrier, which the receiver
 *  corrects (see channel_est.h).
 *
 *  See README.md for the frame layout and the pipeline.
 */

#ifndef FRAME_BUILDER_H
#define FRAME_BUILDER_H

#include <complex>
#include <cstdint>
#include <vector>

#include "fft.h"
#include "rates.h"

namespace fun {
/*!
 * \brief The frame_builder class.
 *
 *  Inputs vector of bytes representing the payload (MPDU) and the
 * transmission PHY transmission rate.
 *
 *  Outputs vector of raw complex samples representing the digital base-band
 * time domain signal to be passed to the usrp class for up-conversion and
 * transmission over the air.
 *
 *  This class takes input data (MPDUs) and builds PHY layer frames by first
 * prepending a PHY header and appending an IEEE CRC-32 checksum to the data. It
 * then encodes the data by scrambling, convolutional coding the data at the
 * coding rate specified (this may or may not require puncturing), and
 * interleaving the data. It then modulates the data as specified. The modulated
 * data is then mapped into symbols with the data being mapped to the respective
 * data subcarriers with pilots and nulls being mapped to their respective
 * subcarriers. Each symbol is then run through an IFFT to convert it to time
 * domain an a cyclic prefix is attached to each symbol. Finally, the symbols
 * are concatenated together and a preamble is prepended to complete the frame.
 * The frame is then returned so that it can be passed to the USRP for
 * transmission.
 */
class frame_builder {
 public:
  /*!
   * \brief Constructor for frame_builder class
   */
  frame_builder();

  /*!
   * \brief Main function for building a PHY frame
   * \param payload (MPDU) the data that needs to be transmitted over the air.
   * \param rate the PHY transmission rate at which to transmit the respective
   * data at.
   * \return A vector of complex doubles representing the digital base-band time
   * domain signal to be passed to the usrp class for up-conversion and
   * transmission over the air.
   */
  std::vector<std::complex<double>> build_frame(
      std::vector<std::uint8_t> payload, Rate rate);

 private:
  fft m_ifft;  //!< The fft instance used to perform the inverse FFT on the OFDM
               //!< symbols
};
}  // namespace fun

#endif  // FRAME_BUILDER_H

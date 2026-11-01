/*! \file transmitter.cpp
 *  \brief C++ file for the transmitter class.
 *
 *  The transmitter class is the public interface for the fun_ofdm transmit
 * chain. This is the easiest way to start transmitting 802.11a OFDM frames out
 * of the box.
 */

#include "transmitter.h"

#include <cstdint>

namespace fun {

transmitter::transmitter(usrp_params params)
    : m_usrp(params), m_frame_builder() {}

/*!
 *  Transmits a single frame, blocking until the frame is sent.
 */
void transmitter::send_frame(std::vector<std::uint8_t> payload, Rate phy_rate) {
  std::vector<std::complex<double>> samples =
      m_frame_builder.build_frame(payload, phy_rate);
  m_usrp.send_burst_sync(samples);
}

}  // namespace fun

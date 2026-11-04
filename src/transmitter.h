/*! \file transmitter.h
 * \brief Sends frames over the air with a USRP.
 *
 * This is the simplest way to transmit: it builds each frame with the PHY's
 * frame_builder and sends its samples with a usrp, as one burst:
 *
 * ~~~
 * payload → frame_builder → samples → usrp::send_burst_sync() → air
 * ~~~
 */

#ifndef TRANSMITTER_H
#define TRANSMITTER_H

#include <cstdint>
#include <vector>

#include "src/phy/frame_builder.h"
#include "src/phy/rates.h"
#include "src/usrp/usrp.h"

namespace fun {

/*!
 * \brief Sends frames over the air with one USRP, one frame at a time.
 *
 * Usage: create a transmitter with the USRP's parameters (center frequency,
 * sample rate, transmit gain and amplitude), then call send_frame() with each
 * payload and the PHY rate to send it at.
 */
class transmitter {
 public:
  /*!
   * \brief Opens the USRP.
   * \param params The USRP's parameters. Defaults to the defaults in
   * #usrp_params. The receive gain isn't used.
   * \throws Whatever usrp::usrp() throws, if the device can't be opened or a
   * gain is out of range.
   */
  transmitter(usrp_params params = usrp_params());

  /*!
   * \brief Sends one frame, and blocks until it has gone out.
   * \param payload The frame's payload (the PSDU), up to 4095 bytes. The PHY
   * adds its own CRC-32 to it.
   * \param phy_rate The PHY rate to send the payload at. The SIGNAL field is
   * always sent at 1/2 BPSK.
   *
   * It blocks for as long as usrp::send_burst_sync() does: until the device
   * reports the end of the burst.
   */
  void send_frame(std::vector<std::uint8_t> payload,
                  Rate phy_rate = RATE_1_2_BPSK);

 private:
  usrp m_usrp;  //!< Sends each frame's samples

  frame_builder m_frame_builder;  //!< Builds each frame's samples
};

}  // namespace fun

#endif  // TRANSMITTER_H

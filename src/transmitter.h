/*! \file transmitter.h
 *  \brief Header file for transmitter class.
 *
 *  The transmitter class is the public interface for the fun_ofdm transmit
 * chain. This is the easiest way to start transmitting 802.11a OFDM frames out
 * of the box.
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
 * \brief The transmitter class is the public interface for the fun_ofdm
 * transmit chain. This is the easiest way to start transmitting 802.11a OFDM
 * frames out of the box.
 *
 *  Usage: Create a transmitter object with the desired USRP parameters (center
 * frequency, sample rate, transmitter gain, and amplitude). Then to send a
 * packet simply call the transmitter::send_frame() function passing it the
 * desired packet to be transmitted and the desired Physical Layer rate (PHY
 * Rate) to transmit it.
 */
class transmitter {
 public:
  /*!
   * \brief Constructor for the transmitter
   * \param params [Optional] The usrp parameters you want to use for this
   * transmitter. Defaults to the defaults in #usrp_params. The rx gain is
   * irrelevant for the transmitter.
   */
  transmitter(usrp_params params = usrp_params());

  /*!
   * \brief Send a single PHY frame at the given PHY Rate
   * \param payload The data to be transmitted (i.e. the MPDU)
   * \param phy_rate [Optional] The PHY data rate to transmit at - defaults to
   * 1/2 BPSK
   *
   *  This function uses the usrp::send_burst_sync() function which means that
   * this function blocks until the packet is done transmitting.
   */
  void send_frame(std::vector<std::uint8_t> payload,
                  Rate phy_rate = RATE_1_2_BPSK);

 private:
  usrp m_usrp;  //!< The usrp object used to send the generated frames over the
                //!< air

  frame_builder m_frame_builder;  //!< The frame builder object used to generate
                                  //!< the frames
};

}  // namespace fun

#endif  // TRANSMITTER_H

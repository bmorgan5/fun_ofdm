/*! \file transmitter.cpp
 *  \brief C++ file for the transmitter class.
 *
 *  The transmitter class is the public interface for the fun_ofdm transmit chain.
 *  This is the easiest way to start transmitting 802.11a OFDM frames out of the box.
 */

#include "transmitter.h"

namespace fun {

    /*!
     *  This constructor shows exactly what parameters need to be set for the transmitter
     */
    transmitter::transmitter(double freq, double samp_rate, double tx_gain, double tx_amp, std::string device_addr) :
        m_usrp(usrp_params(freq, samp_rate, tx_gain, 20, tx_amp, device_addr)),
        m_frame_builder()
    {
    }

    /*!
     * This construct is for those who feel more comfortable using the usrp_params struct
     */
    transmitter::transmitter(usrp_params params) :
        m_usrp(params),
        m_frame_builder()
    {
    }

    /*!
     *  Transmits a single frame, blocking until the frame is sent.
     */
    void transmitter::send_frame(std::vector<unsigned char> payload, Rate phy_rate)
    {
        std::vector<std::complex<double> > samples = m_frame_builder.build_frame(payload, phy_rate);
        m_usrp.send_burst_sync(samples);
    }

}

/*! \file receiver.h
 *  \brief C++ file for the receiver class.
 *
 *  The receiver class is the public interface for the fun_ofdm receiver.
 *  This is the easiest way to start receiving 802.11a OFDM frames out of the box.
 */

#include "receiver.h"

namespace fun
{

    /*!
     * This constructor shows exactly what parameters need to be set for the receiver.
     */
    receiver::receiver(void (*callback)(std::vector<std::vector<unsigned char> > packets), double freq, double samp_rate, double rx_gain, std::string device_addr) :
        receiver(callback, usrp_params(freq, samp_rate, 20, rx_gain, 1.0, device_addr))
    {
    }

    /*!
     * This constructor is for those who feel more comfortable using the usrp_params struct.
     */
    receiver::receiver(void (*callback)(std::vector<std::vector<unsigned char> > packets), usrp_params params) :
        m_callback(callback),
        m_usrp(params),
        m_rec_chain(),
        m_samples(NUM_RX_SAMPLES)
    {
    }

    /*!
     *  This function loops until a stop is requested, pulling samples from the USRP and passing them through the
     *  receiver chain. It then passes any successfully decoded packets to the callback function for the user
     *  to process further.
     */
    void receiver::run(std::stop_token stop)
    {
        while(!stop.stop_requested())
        {
            m_usrp.get_samples(NUM_RX_SAMPLES, m_samples);

            std::vector<std::vector<unsigned char> > packets =
                    m_rec_chain.process_samples(m_samples);

            m_callback(packets);
        }
    }
}

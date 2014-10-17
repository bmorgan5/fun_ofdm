/*! \file usrp.cpp
 *  \brief C++ file for the usrp class.
 *
 *  The usrp class is a wrapper class for the UHD API. It provides a simple interface for transmitting
 *  and receiving samples to/from the USRP.
 *
 *  The usrp_params class is a container for holding the necessary parameters for the usrp
 *  such as center frequency, sample rate, tx/rx gain, etc..
 */

#include "usrp.h"

namespace fun
{
    /*!
     * -Initializations
     *  + #m_params -> Previously initialized usrp_params object containing the desired parameters
     *    for the USRP.
     */
    usrp::usrp(usrp_params params) :
        m_params(params)
    {
        // Instantiate the multi_usrp
        m_usrp = uhd::usrp::multi_usrp::make(uhd::device_addr_t(m_params.device_addr));
        m_device = m_usrp->get_device();

        // Set the center frequency
        m_usrp->set_tx_freq(uhd::tune_request_t(m_params.freq));
        m_usrp->set_rx_freq(uhd::tune_request_t(m_params.freq));

        // Set the sample rate
        m_usrp->set_tx_rate(m_params.rate);
        m_usrp->set_rx_rate(m_params.rate);

        // Set the gains
        m_usrp->set_tx_gain(m_params.tx_gain);
        m_usrp->set_rx_gain(m_params.rx_gain);

        // Set the RX antenna
        //m_usrp->set_rx_antenna("RX2");

        // Get the TX and RX stream handles
        m_tx_streamer = m_usrp->get_tx_stream(uhd::stream_args_t("fc64"));
        m_rx_streamer = m_usrp->get_rx_stream(uhd::stream_args_t("fc64"));

        // Start the RX stream
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.stream_now = true;
        m_usrp->issue_stream_cmd(stream_cmd);

        sem_init(&m_tx_sem, 0, 0);
        sem_post(&m_tx_sem);
    }

    /*!
     * Sends a burst of samples to the USRP which represent the digital base-band signal
     * to be up-converted and transmitted by the USRP.
     *
     * This function does not block after it is called. Due to the multi-threading nature of the
     * UHD API, this function may return before the USRP has finished transmitting all of the
     * samples. This is generally ok as subsequent calls to this method will just buffer more
     * samples in the USRP. However, if it is not called fast enough an underrun may occer.
     * See <a href="http://files.ettus.com/manual/page_general.html#general_ounotes"> link to ettus' website</a>
     * for more details.
     */
    void usrp::send_burst(std::vector<std::complex<double> > samples)
    {
        sem_wait(&m_tx_sem);

        uhd::tx_metadata_t tx_metadata;
        tx_metadata.start_of_burst = true;
        tx_metadata.end_of_burst = true;
        tx_metadata.has_time_spec = false;
        m_tx_streamer->send(&samples[0], samples.size(), tx_metadata);

        sem_post(&m_tx_sem);
    }


    /*!
     * Sends a burst of samples to the USRP which represent the digital base-band signal
     * to be up-converted and transmitted by the USRP.
     *
     * This function uses a semaphore to block until the USRP has responded with an acknowledgement
     * that all the samples have been transmitted over the air. This prevents the user from sending
     * too much data at once so that the user has some sense of when the transmission is finished.
     * If the user does not call this fast enough an underrun may occur.
     * See <a href="http://files.ettus.com/manual/page_general.html#general_ounotes"> link to ettus' website</a>
     * for more details.
     */
    void usrp::send_burst_sync(std::vector<std::complex<double> > samples)
    {
        // Scale the samples by m_amp
        if(m_params.tx_amp != 1.0)
            for(int x = 0; x < samples.size(); x++)
                samples[x] *= m_params.tx_amp;

        // Send the samples
        uhd::tx_metadata_t tx_metadata;
        tx_metadata.start_of_burst = true;
        tx_metadata.end_of_burst = true;
        tx_metadata.has_time_spec = false;
        m_tx_streamer->send(&samples[0], samples.size(), tx_metadata);

        // Wait for the end of burst ACK followed by an underflow
        bool got_ack = false;
        bool got_underflow = false;
        uhd::async_metadata_t async_metadata;
        while(!got_ack && !got_underflow && m_device->recv_async_msg(async_metadata, 1))
        {
            got_ack = (async_metadata.event_code == uhd::async_metadata_t::EVENT_CODE_BURST_ACK);
            got_underflow = (got_ack && async_metadata.event_code == uhd::async_metadata_t::EVENT_CODE_UNDERFLOW);
        }
    }

    /*!
     * Gets num_samples from the USRP and places them in the buffer parameter. If this function
     * is not called "fast enough" the USRP will get upset because the computer is not consuming
     * samples fast enough to keep up with the USRPs receive sample rate.  This will cause the USRP
     * to indicate an overflow and thus not guarantee the integrity of the retrieved data.
     * See <a href="http://files.ettus.com/manual/page_general.html#general_ounotes"> link to ettus' website</a>
     * for more details.
     *
     */
    void usrp::get_samples(int num_samples, std::vector<std::complex<double> > & buffer)
    {
        // Get some samples
        uhd::rx_metadata_t rx_meta;
        m_rx_streamer->recv(&buffer[0], num_samples, rx_meta);
    }

}


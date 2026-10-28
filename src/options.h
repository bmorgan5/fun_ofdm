/*! \file options.h
 *  \brief Header file for the fun-ofdm command line options.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>
#include <CLI/CLI.hpp>
#include "rates.h"
#include "usrp.h"

namespace fun
{
    /*!
     * \brief Options shared by the modes that use a USRP.
     */
    struct radio_options
    {
        double freq_ghz = 5.72;     //!< Center frequency in GHz, unless #channel is set
        int channel = 0;            //!< 802.11 channel number, or 0 to use #freq_ghz
        double bandwidth_mhz = 5;   //!< Channel bandwidth (and sample rate) in MHz
        double gain = 30;           //!< TX or RX gain in dB, depending on the mode
        std::string device;         //!< UHD device address, or "" to find one automatically
        bool no_realtime = false;   //!< Skip requesting real time thread priority
    };

    /*!
     * \brief Options for the tx mode.
     */
    struct tx_options
    {
        radio_options radio;            //!< USRP options
        double tx_amp = 0.5;            //!< Scale factor for transmit samples
        Rate phy_rate = RATE_1_2_BPSK;  //!< PHY rate for all transmitted frames
    };

    /*!
     * \brief Options for the rx mode.
     */
    struct rx_options
    {
        radio_options radio; //!< USRP options
    };

    /*!
     * \brief Options for the sim mode.
     */
    struct sim_options
    {
        Rate phy_rate = RATE_3_4_QAM16; //!< PHY rate for all simulated frames
    };

    /*!
     * \brief Adds the tx mode's options to its subcommand.
     */
    void add_options(CLI::App * app, tx_options & opts);

    /*!
     * \brief Adds the rx mode's options to its subcommand.
     */
    void add_options(CLI::App * app, rx_options & opts);

    /*!
     * \brief Adds the sim mode's options to its subcommand.
     */
    void add_options(CLI::App * app, sim_options & opts);

    /*!
     * \brief Converts an 802.11 channel number to its center frequency.
     * \param channel Channel 1-14 (2.4 GHz band) or 32-177 (5 GHz band)
     * \return The center frequency in Hz
     */
    double channel_to_freq(int channel);

    /*!
     * \brief Builds the USRP parameters for the tx mode.
     */
    usrp_params to_usrp_params(const tx_options & opts);

    /*!
     * \brief Builds the USRP parameters for the rx mode.
     */
    usrp_params to_usrp_params(const rx_options & opts);
}

#endif // OPTIONS_H

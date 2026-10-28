/*! \file options.cpp
 *  \brief C++ file for the fun-ofdm command line options.
 */

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
#include "options.h"

namespace fun
{
    /*!
     * Names for the --phy-rate option, in the same order as the #Rate enum.
     */
    static const std::vector<std::pair<std::string, Rate> > RATE_NAMES = {
        {"bpsk-1/2",  RATE_1_2_BPSK},
        {"bpsk-2/3",  RATE_2_3_BPSK},
        {"bpsk-3/4",  RATE_3_4_BPSK},
        {"qpsk-1/2",  RATE_1_2_QPSK},
        {"qpsk-2/3",  RATE_2_3_QPSK},
        {"qpsk-3/4",  RATE_3_4_QPSK},
        {"qam16-1/2", RATE_1_2_QAM16},
        {"qam16-2/3", RATE_2_3_QAM16},
        {"qam16-3/4", RATE_3_4_QAM16},
        {"qam64-2/3", RATE_2_3_QAM64},
        {"qam64-3/4", RATE_3_4_QAM64},
    };

    /*!
     * \brief Makes a validator that checks that an option's value is a number
     * \param name Name shown in the help text, e.g. POSITIVE
     * \param valid Returns whether a number is valid
     * \param requirement Describes valid numbers in the error message, e.g. "greater than 0"
     */
    static CLI::Validator number_validator(const std::string & name, bool (*valid)(double), const std::string & requirement)
    {
        return CLI::Validator(
            [valid, requirement](std::string & value) -> std::string
            {
                char * end;
                double number = std::strtod(value.c_str(), &end);
                if(end == value.c_str() || *end != '\0' || !valid(number))
                {
                    return "Value " + value + " is not a number " + requirement;
                }
                return "";
            },
            name);
    }

    static const CLI::Validator POSITIVE = number_validator("POSITIVE", [](double x) { return x > 0; }, "greater than 0");
    static const CLI::Validator NON_NEGATIVE = number_validator("NONNEGATIVE", [](double x) { return x >= 0; }, "of at least 0");

    /*!
     * \brief Adds the --phy-rate option.
     */
    static void add_phy_rate_option(CLI::App * app, Rate & phy_rate)
    {
        std::vector<std::string> names;
        std::string default_name;
        for(const auto & name : RATE_NAMES)
        {
            names.push_back(name.first);
            if(name.second == phy_rate) default_name = name.first;
        }

        app->add_option_function<std::string>("--phy-rate",
                [&phy_rate](const std::string & value)
                {
                    for(const auto & name : RATE_NAMES)
                    {
                        if(name.first == value) phy_rate = name.second;
                    }
                },
                "Modulation and coding rate")
            ->check(CLI::IsMember(names))
            ->default_str(default_name);
    }

    /*!
     * \brief Adds the options shared by the modes that use a USRP.
     */
    static void add_radio_options(CLI::App * app, radio_options & opts)
    {
        CLI::Option * freq = app->add_option("--freq", opts.freq_ghz, "Center frequency in GHz")
            ->check(POSITIVE)
            ->capture_default_str();
        CLI::Option * channel = app->add_option("--channel", opts.channel, "802.11 channel number, converted to a center frequency")
            ->check(CLI::Range(1, 14) | CLI::Range(32, 177));
        freq->excludes(channel);

        app->add_option("--bandwidth", opts.bandwidth_mhz, "Channel bandwidth in MHz (also the sample rate)")
            ->check(POSITIVE)
            ->capture_default_str();
        app->add_option("--gain", opts.gain, "Amplifier gain in dB (the valid range depends on the device)")
            ->check(NON_NEGATIVE)
            ->capture_default_str();
        app->add_option("--device", opts.device, "UHD device address, e.g. addr=192.168.10.2 (default: find one)");
        app->add_flag("--no-realtime", opts.no_realtime, "Don't request real time thread priority");
    }

    void add_options(CLI::App * app, tx_options & opts)
    {
        add_radio_options(app, opts.radio);
        app->add_option("--tx-amp", opts.tx_amp, "Scale factor for transmit samples")
            ->check(CLI::Range(0.0, 1.0))
            ->capture_default_str();
        add_phy_rate_option(app, opts.phy_rate);
    }

    void add_options(CLI::App * app, rx_options & opts)
    {
        add_radio_options(app, opts.radio);
    }

    void add_options(CLI::App * app, sim_options & opts)
    {
        add_phy_rate_option(app, opts.phy_rate);
    }

    /*!
     * Channels 1-13 are 5 MHz apart starting at 2412 MHz and channel 14 is at 2484 MHz.
     * In the 5 GHz band, channel N is at 5000 + 5N MHz.
     */
    double channel_to_freq(int channel)
    {
        if(channel == 14) return 2484e6;
        if(channel < 14) return (2407 + 5 * channel) * 1e6;
        return (5000 + 5 * channel) * 1e6;
    }

    /*!
     * \brief Builds the USRP parameters shared by both modes and prints the frequency
     * and bandwidth being used. The caller sets the gain for the direction it uses; the
     * other direction keeps the #usrp_params default.
     */
    static usrp_params to_usrp_params(const radio_options & opts)
    {
        usrp_params params;
        params.freq = opts.channel ? channel_to_freq(opts.channel) : opts.freq_ghz * 1e9;
        params.rate = opts.bandwidth_mhz * 1e6;
        params.device_addr = opts.device;

        if(opts.bandwidth_mhz != 5 && opts.bandwidth_mhz != 10 && opts.bandwidth_mhz != 20)
        {
            std::cerr << "Warning: 802.11a uses a 5, 10 or 20 MHz bandwidth, not " << opts.bandwidth_mhz << " MHz" << std::endl;
        }

        std::cout << "Frequency: " << params.freq / 1e9 << " GHz, bandwidth: " << opts.bandwidth_mhz << " MHz" << std::endl;
        return params;
    }

    usrp_params to_usrp_params(const tx_options & opts)
    {
        usrp_params params = to_usrp_params(opts.radio);
        params.tx_gain = opts.radio.gain;
        params.tx_amp = opts.tx_amp;
        return params;
    }

    usrp_params to_usrp_params(const rx_options & opts)
    {
        usrp_params params = to_usrp_params(opts.radio);
        params.rx_gain = opts.radio.gain;
        return params;
    }
}

/*! \file options.h
 *  \brief Header file for the fun-ofdm command line options.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <cstdint>
#include <optional>
#include <string>

#include <CLI/CLI.hpp>

#include "channel.h"
#include "src/phy/rates.h"
#include "src/usrp/usrp_params.h"

namespace fun {
/*!
 * \brief Options shared by the modes that use a USRP.
 */
struct radio_options {
  double freq_ghz = 5.72;  //!< Center frequency in GHz, unless #channel is set
  int channel = 0;         //!< 802.11 channel number, or 0 to use #freq_ghz
  double bandwidth_mhz = 5;  //!< Channel bandwidth (and sample rate) in MHz
  double gain = 30;          //!< TX or RX gain in dB, depending on the mode
  std::string device;  //!< UHD device address, or "" to find one automatically
  bool no_realtime = false;  //!< Skip requesting real time thread priority
};

/*!
 * \brief Options for how much data to send. At most one of them is set.
 */
struct data_options {
  //! Number of full packets of random data, or 0 if not set
  std::uint64_t packets = 0;
  //! Number of bytes of random data, or 0 if not set
  std::uint64_t bytes = 0;

  /*!
   * \brief The number of bytes to send.
   * \return The bytes set by #packets or #bytes, or std::nullopt if neither is
   * set
   */
  std::optional<std::uint64_t> limit() const;
};

/*!
 * \brief Options for tracing the receiver (src/metrics/tracing.h).
 */
struct trace_options {
  //! --trace: the file to record a trace into, or std::nullopt for none
  std::optional<std::string> file;
  //! --trace-system: let the system's traced service record the receiver
  bool system = false;
};

/*!
 * \brief Options for the tx mode.
 */
struct tx_options {
  radio_options radio;
  double tx_amp = 0.5;
  Rate phy_rate = RATE_1_2_BPSK;
  data_options data;
  std::optional<std::uint64_t> seed;
  std::optional<std::string> json_file;
};

/*!
 * \brief Options for the rx mode.
 */
struct rx_options {
  radio_options radio;
  std::optional<std::string> json_file;
  trace_options trace;
};

/*!
 * \brief Options for the sim mode.
 */
struct sim_options {
  Rate phy_rate = RATE_3_4_QAM16;
  data_options data;
  double bandwidth_mhz = 5;  //!< Channel bandwidth (and sample rate) in MHz
  channel_params channel;    //!< The channel's impairments

  std::optional<std::uint64_t> seed;
  std::optional<std::string> json_file;
  trace_options trace;
};

/*!
 * \brief The number of packets sim sends when neither --packets nor --bytes is
 * given.
 */
const std::uint64_t SIM_DEFAULT_PACKETS = 100;

/*!
 * \brief Adds the tx mode's options to its subcommand.
 */
void add_options(CLI::App* app, tx_options& opts);

/*!
 * \brief Adds the rx mode's options to its subcommand.
 */
void add_options(CLI::App* app, rx_options& opts);

/*!
 * \brief Adds the sim mode's options to its subcommand.
 */
void add_options(CLI::App* app, sim_options& opts);

/*!
 * \brief The --phy-rate name of a rate, e.g. "qam16-3/4".
 */
std::string rate_name(Rate phy_rate);

/*!
 * \brief Converts an 802.11 channel number to its center frequency.
 * \param channel Channel 1-14 (2.4 GHz band) or 32-177 (5 GHz band)
 * \return The center frequency in Hz
 */
double channel_to_freq(int channel);

/*!
 * \brief Builds the USRP parameters for the tx mode.
 */
usrp_params to_usrp_params(const tx_options& opts);

/*!
 * \brief Builds the USRP parameters for the rx mode.
 */
usrp_params to_usrp_params(const rx_options& opts);
}  // namespace fun

#endif  // OPTIONS_H

/*! \file summary.h
 * \brief Header file for the run summary that --json writes.
 *
 * Each mode fills in its result, and to_json() writes the summary: the
 * version, the mode, its options, and the result.
 */

#ifndef SUMMARY_H
#define SUMMARY_H

#include <cstdint>
#include <optional>
#include <string>

#include "options.h"
#include "stats.h"

namespace fun {
/*!
 * \brief What a tx run did.
 */
struct tx_result {
  std::uint64_t seed = 0;  //!< The seed of the random data
  transfer_totals sent;    //!< The packets sent
};

/*!
 * \brief What an rx run did. rx runs until it's stopped.
 */
struct rx_result {
  transfer_totals received;      //!< The packets received, without duplicates
  std::uint64_t lost = 0;        //!< Packets found to be lost
  std::uint64_t duplicates = 0;  //!< Duplicate packets received
};

/*!
 * \brief How sim's received data compared with what it sent.
 */
struct sim_check {
  std::uint64_t lost = 0;              //!< Packets sent but not received
  std::uint64_t duplicates = 0;        //!< Duplicate packets received
  std::uint64_t mismatched_bytes = 0;  //!< Bytes received wrong

  /*!
   * \brief Whether every packet was received intact.
   */
  bool passed() const { return lost == 0 && mismatched_bytes == 0; }
};

/*!
 * \brief What a sim run did.
 */
struct sim_result {
  std::uint64_t seed = 0;    //!< The seed of everything random
  transfer_totals received;  //!< The packets received, without duplicates
  //! The check of the received data, or std::nullopt if the run was stopped
  //! before it
  std::optional<sim_check> check;
};

/*!
 * \brief Writes a tx run's summary as JSON.
 * \param version fun-ofdm's version
 * \param opts The run's options
 * \param result What the run did
 * \return The summary as JSON, with a member per line and no newline at the
 * end.
 *
 * The summary is an object with these members, in this order:
 * - `version`, and `mode`: "tx", "rx" or "sim".
 * - `options`: a member per option, named after it without the leading
 *   dashes, in the option's units, with the value the run used. An option is
 *   null only if the run had no value for it: `freq` when `channel` is set,
 *   `channel` when it isn't, `device` when UHD finds one, both `packets`
 *   and `bytes` when tx sends until it's stopped, and `snr` when sim adds no
 *   noise. --seed and --json aren't
 *   included: the seed that was used is a member of its own, and where the
 *   summary went isn't part of the run.
 * - `seed` (tx and sim).
 * - `sent` (tx) or `received` (rx and sim): `packets`, `bytes`, `seconds` and
 *   `bits_per_second`, which is null if no time passed.
 * - `lost` and `duplicates` (rx, and sim unless it was stopped).
 * - `mismatched_bytes` and `passed` (sim, unless it was stopped). A stopped sim
 *   run doesn't check the data it received.
 */
std::string to_json(const std::string& version, const tx_options& opts,
                    const tx_result& result);

/*!
 * \brief Writes an rx run's summary as JSON, like
 * to_json(const std::string&, const tx_options&, const tx_result&).
 */
std::string to_json(const std::string& version, const rx_options& opts,
                    const rx_result& result);

/*!
 * \brief Writes a sim run's summary as JSON, like
 * to_json(const std::string&, const tx_options&, const tx_result&).
 */
std::string to_json(const std::string& version, const sim_options& opts,
                    const sim_result& result);
}  // namespace fun

#endif  // SUMMARY_H

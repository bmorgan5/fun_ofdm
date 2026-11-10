/*! \file summary.cpp
 * \brief C++ file for the run summary that --json writes.
 */

#include "summary.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>

#include <boost/json.hpp>
// Boost.JSON's header-only mode: this compiles the library into this file, so
// nothing links against libboost_json. No other file may include it.
#include <boost/json/src.hpp>

namespace fun {
namespace json = boost::json;

/*!
 * \brief Returns a value, or null if it's 0, which the options use for "not
 * set".
 */
template <typename T>
static json::value unless_zero(T value) {
  if (value == 0) return nullptr;
  return value;
}

/*!
 * \brief The options shared by the modes that use a USRP. Only one of freq
 * and channel is set.
 */
static json::object radio_json(const radio_options& opts) {
  return {
      {"freq", opts.channel ? json::value() : json::value(opts.freq_ghz)},
      {"channel", unless_zero(opts.channel)},
      {"bandwidth", opts.bandwidth_mhz},
      {"gain", opts.gain},
      {"device",
       opts.device.empty() ? json::value() : json::value(opts.device)},
      {"no_realtime", opts.no_realtime},
  };
}

/*!
 * \brief Packet and byte totals, with the throughput.
 */
static json::object totals_json(const transfer_totals& totals) {
  json::value bits_per_second;
  if (totals.elapsed > std::chrono::nanoseconds::zero())
    bits_per_second = totals.bytes * 8 / totals.seconds();
  return {
      {"packets", totals.packets},
      {"bytes", totals.bytes},
      {"seconds", totals.seconds()},
      {"bits_per_second", bits_per_second},
  };
}

/*!
 * \brief Starts a summary with the members every mode has.
 */
static json::object start(const std::string& version, const std::string& mode,
                          json::object options) {
  return {
      {"version", version},
      {"mode", mode},
      {"options", std::move(options)},
  };
}

/*!
 * \brief Appends a double to JSON text.
 *
 * Boost.JSON writes doubles in scientific notation, like 3E1 for 30. This
 * writes the shortest form that reads back as the same double instead, with
 * ".0" added to whole numbers, so that they read back as doubles too. JSON has
 * no NaN or infinity, so they're written as null.
 */
static void write_double(std::string& out, double value) {
  if (!std::isfinite(value)) {
    out += "null";
    return;
  }

  std::array<char, 32> buffer;
  char* end =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value).ptr;
  std::string number(buffer.data(), end);
  if (number.find_first_of(".e") == std::string::npos) number += ".0";
  out += number;
}

/*!
 * \brief Appends a value to JSON text, with each member of an object on a line
 * of its own, indented by 2 spaces per level.
 * \param out Where to append the value
 * \param value The value
 * \param depth How deeply nested the value is
 *
 * Boost.JSON only writes JSON on one line. The summary has no arrays, so they
 * stay on one line.
 */
static void write_pretty(std::string& out, const json::value& value,
                         int depth) {
  if (value.is_double()) {
    write_double(out, value.get_double());
    return;
  }
  if (!value.is_object() || value.get_object().empty()) {
    out += json::serialize(value);
    return;
  }

  std::string indent(2 * (depth + 1), ' ');
  out += "{\n";
  bool first = true;
  for (const auto& member : value.get_object()) {
    if (!first) out += ",\n";
    first = false;
    out += indent + json::serialize(member.key()) + ": ";
    write_pretty(out, member.value(), depth + 1);
  }
  out += "\n" + std::string(2 * depth, ' ') + "}";
}

/*!
 * \brief Writes a summary as pretty-printed JSON.
 */
static std::string write_pretty(const json::object& summary) {
  std::string out;
  write_pretty(out, summary, 0);
  return out;
}

std::string to_json(const std::string& version, const tx_options& opts,
                    const tx_result& result) {
  json::object options = radio_json(opts.radio);
  options["tx_amp"] = opts.tx_amp;
  options["phy_rate"] = rate_name(opts.phy_rate);
  // Without either, tx sends until it's stopped
  options["packets"] = unless_zero(opts.data.packets);
  options["bytes"] = unless_zero(opts.data.bytes);

  json::object summary = start(version, "tx", std::move(options));
  summary["seed"] = result.seed;
  summary["sent"] = totals_json(result.sent);
  return write_pretty(summary);
}

std::string to_json(const std::string& version, const rx_options& opts,
                    const rx_result& result) {
  json::object summary = start(version, "rx", radio_json(opts.radio));
  summary["received"] = totals_json(result.received);
  summary["lost"] = result.lost;
  summary["duplicates"] = result.duplicates;
  return write_pretty(summary);
}

std::string to_json(const std::string& version, const sim_options& opts,
                    const sim_result& result) {
  // Without either, sim sends SIM_DEFAULT_PACKETS packets
  bool data_given = opts.data.packets || opts.data.bytes;
  json::object options = {
      {"phy_rate", rate_name(opts.phy_rate)},
      {"packets", data_given ? unless_zero(opts.data.packets)
                             : json::value(SIM_DEFAULT_PACKETS)},
      {"bytes", unless_zero(opts.data.bytes)},
      {"bandwidth", opts.bandwidth_mhz},
      {"snr",
       opts.channel.snr_db ? json::value(*opts.channel.snr_db) : json::value()},
      {"cfo", opts.channel.cfo_hz},
      {"timing_offset", opts.channel.timing_offset},
      {"delay_spread", opts.channel.delay_spread_ns},
  };

  json::object summary = start(version, "sim", std::move(options));
  summary["seed"] = result.seed;
  summary["received"] = totals_json(result.received);
  if (result.check) {
    summary["lost"] = result.check->lost;
    summary["duplicates"] = result.check->duplicates;
    summary["mismatched_bytes"] = result.check->mismatched_bytes;
    summary["passed"] = result.check->passed();
  }
  return write_pretty(summary);
}
}  // namespace fun

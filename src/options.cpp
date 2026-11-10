/*! \file options.cpp
 *  \brief C++ file for the fun-ofdm command line options.
 */

#include "options.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <system_error>
#include <utility>
#include <vector>

#include "framing.h"

namespace fun {
/*!
 * Names for the --phy-rate option, in the same order as the #Rate enum.
 */
static const std::vector<std::pair<std::string, Rate>> RATE_NAMES = {
    {"bpsk-1/2", RATE_1_2_BPSK},   {"bpsk-2/3", RATE_2_3_BPSK},
    {"bpsk-3/4", RATE_3_4_BPSK},   {"qpsk-1/2", RATE_1_2_QPSK},
    {"qpsk-2/3", RATE_2_3_QPSK},   {"qpsk-3/4", RATE_3_4_QPSK},
    {"qam16-1/2", RATE_1_2_QAM16}, {"qam16-2/3", RATE_2_3_QAM16},
    {"qam16-3/4", RATE_3_4_QAM16}, {"qam64-2/3", RATE_2_3_QAM64},
    {"qam64-3/4", RATE_3_4_QAM64},
};

/*!
 * \brief Makes a validator that checks that an option's value is a number
 * \param name Name shown in the help text, e.g. POSITIVE
 * \param valid Returns whether a number is valid
 * \param requirement Describes valid numbers in the error message, e.g.
 * "greater than 0"
 */
static CLI::Validator number_validator(const std::string& name,
                                       bool (*valid)(double),
                                       const std::string& requirement) {
  return CLI::Validator(
      [valid, requirement](std::string& value) -> std::string {
        char* end;
        double number = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || !valid(number)) {
          return "Value " + value + " is not a number " + requirement;
        }
        return "";
      },
      name);
}

static const CLI::Validator POSITIVE = number_validator(
    "POSITIVE", [](double x) { return x > 0; }, "greater than 0");
static const CLI::Validator NON_NEGATIVE = number_validator(
    "NONNEGATIVE", [](double x) { return x >= 0; }, "of at least 0");

/*!
 * \brief Parses a whole string as a finite double.
 * \return The number, or std::nullopt if the string isn't one.
 */
static std::optional<double> parse_double(const std::string& text) {
  double number;
  const char* end = text.data() + text.size();
  auto [last, error] = std::from_chars(text.data(), end, number);
  if (error != std::errc() || last != end || !std::isfinite(number))
    return std::nullopt;
  return number;
}

/*!
 * \brief Converts a frequency with an optional unit, like 1.5kHz, to Hz.
 *
 * The unit is Hz, kHz, MHz or GHz, in any case, and can follow a space.
 * Without one, the number is in Hz, so a frequency can still be given to the
 * last digit.
 *
 * The unit moves the decimal point before the number is parsed, so
 * 2.4123456MHz is exactly the double nearest 2412345.6, as if it were typed
 * out: multiplying after parsing would round twice. CLI11's AsNumberWithUnit,
 * which --bytes uses, would also write the result back with only 6 digits.
 */
static const CLI::Validator HERTZ(
    [](std::string& value) -> std::string {
      const std::vector<std::pair<std::string, int>> UNITS = {
          {"ghz", 9}, {"mhz", 6}, {"khz", 3}, {"hz", 0}};
      std::string lower = value;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });

      std::string number = value;
      int exponent = 0;
      for (const auto& [unit, power] : UNITS) {
        if (lower.ends_with(unit)) {
          number.resize(number.size() - unit.size());
          if (number.ends_with(' ')) number.pop_back();
          exponent = power;
          break;
        }
      }

      std::optional<double> hz = parse_double(number);
      if (hz && exponent) {
        // A number with its own exponent, like 1e3kHz, is multiplied instead
        if (number.find_first_of("eE") == std::string::npos)
          hz = parse_double(number + "e" + std::to_string(exponent));
        else if (double scaled = *hz * std::pow(10, exponent);
                 std::isfinite(scaled))
          hz = scaled;
        else
          hz = std::nullopt;
      }
      if (!hz) {
        return "Value " + value +
               " is not a frequency, e.g. 1500, 1.5kHz or 0.1MHz";
      }

      // The shortest form that reads back as the same double
      std::array<char, 32> buffer;
      char* end =
          std::to_chars(buffer.data(), buffer.data() + buffer.size(), *hz).ptr;
      value.assign(buffer.data(), end);
      return "";
    },
    "HZ");

/*!
 * \brief Adds the --phy-rate option.
 */
static void add_phy_rate_option(CLI::App* app, Rate& phy_rate) {
  std::vector<std::string> names;
  std::string default_name;
  for (const auto& name : RATE_NAMES) {
    names.push_back(name.first);
    if (name.second == phy_rate) default_name = name.first;
  }

  app->add_option_function<std::string>(
         "--phy-rate",
         [&phy_rate](const std::string& value) {
           for (const auto& name : RATE_NAMES) {
             if (name.first == value) phy_rate = name.second;
           }
         },
         "Modulation and coding rate")
      ->check(CLI::IsMember(names))
      ->default_str(default_name);
}

std::string rate_name(Rate phy_rate) {
  for (const auto& name : RATE_NAMES) {
    if (name.second == phy_rate) return name.first;
  }
  return "";
}

/*!
 * \brief Adds the --seed option.
 * \param app The subcommand to add the option to
 * \param seed Where to store the seed
 * \param what What the seed seeds, for the help text
 */
static void add_seed_option(CLI::App* app, std::optional<std::uint64_t>& seed,
                            const std::string& what) {
  app->add_option("--seed", seed,
                  "Seed for " + what +
                      ", to repeat a run (default: a random seed, which is "
                      "printed)");
}

/*!
 * \brief Adds the --json option. Without a file name, it's "-", for stdout.
 *
 * CLI11 takes an empty value for no value, so --json "" would silently write
 * nothing. A callback, unlike a validator, still sees the empty string, so it
 * can reject it.
 */
static void add_json_option(CLI::App* app,
                            std::optional<std::string>& json_file) {
  app->add_option_function<std::string>(
         "--json",
         [&json_file](const std::string& value) {
           if (value.empty())
             throw CLI::ValidationError("--json", "The file name is empty");
           json_file = value;
         },
         "Also write the run summary as JSON, to FILE, or to stdout without "
         "one (the rest of the output then goes to stderr)")
      ->expected(0, 1)
      ->default_str("-")
      ->type_name("[FILE]");
}

/*!
 * \brief Adds the options shared by the modes that use a USRP.
 */
static void add_radio_options(CLI::App* app, radio_options& opts) {
  CLI::Option* freq =
      app->add_option("--freq", opts.freq_ghz, "Center frequency in GHz")
          ->check(POSITIVE)
          ->capture_default_str();
  CLI::Option* channel =
      app->add_option("--channel", opts.channel,
                      "802.11 channel number, converted to a center frequency")
          ->check(CLI::Range(1, 14) | CLI::Range(32, 177));
  freq->excludes(channel);

  app->add_option("--bandwidth", opts.bandwidth_mhz,
                  "Channel bandwidth in MHz (also the sample rate)")
      ->check(POSITIVE)
      ->capture_default_str();
  app->add_option(
         "--gain", opts.gain,
         "Amplifier gain in dB (the valid range depends on the device)")
      ->check(NON_NEGATIVE)
      ->capture_default_str();
  app->add_option(
      "--device", opts.device,
      "UHD device address, e.g. addr=192.168.10.2 (default: find one)");
  app->add_flag("--no-realtime", opts.no_realtime,
                "Don't request real time thread priority");
}

/*!
 * \brief Adds the options for how much data to send.
 * \param app The subcommand to add the options to
 * \param opts Where to store the options
 * \param unset What is sent if neither option is given, for the help text
 */
static void add_data_options(CLI::App* app, data_options& opts,
                             const std::string& unset) {
  CLI::Option* packets =
      app->add_option("--packets", opts.packets,
                      "Send this many full packets of random data (default: " +
                          unset + ")")
          ->check(POSITIVE);
  CLI::Option* bytes =
      app->add_option("--bytes", opts.bytes,
                      "Send this much random data, e.g. 10MB or 64KiB")
          ->transform(CLI::AsSizeValue(true))
          ->check(POSITIVE);
  packets->excludes(bytes);
}

std::optional<std::uint64_t> data_options::limit() const {
  if (packets) return packets * FRAME_DATA_SIZE;
  if (bytes) return bytes;
  return std::nullopt;
}

void add_options(CLI::App* app, tx_options& opts) {
  add_radio_options(app, opts.radio);
  app->add_option("--tx-amp", opts.tx_amp, "Scale factor for transmit samples")
      ->check(CLI::Range(0.0, 1.0))
      ->capture_default_str();
  add_phy_rate_option(app, opts.phy_rate);
  add_data_options(app, opts.data, "send until Ctrl+C");
  add_seed_option(app, opts.seed, "the random data");
  add_json_option(app, opts.json_file);
}

void add_options(CLI::App* app, rx_options& opts) {
  add_radio_options(app, opts.radio);
  add_json_option(app, opts.json_file);
}

/*!
 * \brief Adds sim's options for the channel's impairments, and the bandwidth,
 * which converts them to samples.
 */
static void add_channel_options(CLI::App* app, double& bandwidth_mhz,
                                channel_params& params) {
  app->add_option("--bandwidth", bandwidth_mhz,
                  "Channel bandwidth in MHz (also the sample rate)")
      ->check(POSITIVE)
      ->capture_default_str();
  app->add_option("--snr", params.snr_db,
                  "Add white Gaussian noise at this SNR in dB (default: no "
                  "noise)");
  app->add_option("--cfo", params.cfo_hz,
                  "Carrier frequency offset, the receiver's minus the "
                  "transmitter's, e.g. 1500, 1.5kHz or 0.1MHz (Hz without a "
                  "unit)")
      ->transform(HERTZ)
      ->capture_default_str();
  app->add_option("--timing-offset", params.timing_offset,
                  "Delay the signal by this many samples, which can be a "
                  "fraction")
      ->check(NON_NEGATIVE)
      ->capture_default_str();
  app->add_option("--delay-spread", params.delay_spread_ns,
                  "RMS delay spread in ns of a random multipath channel per "
                  "frame (default: no multipath)")
      ->check(NON_NEGATIVE);
}

void add_options(CLI::App* app, sim_options& opts) {
  add_phy_rate_option(app, opts.phy_rate);
  add_data_options(app, opts.data, std::to_string(SIM_DEFAULT_PACKETS));
  add_channel_options(app, opts.bandwidth_mhz, opts.channel);
  add_seed_option(app, opts.seed, "everything random in the simulation");
  add_json_option(app, opts.json_file);
}

/*!
 * Channels 1-13 are 5 MHz apart starting at 2412 MHz and channel 14 is at 2484
 * MHz. In the 5 GHz band, channel N is at 5000 + 5N MHz.
 */
double channel_to_freq(int channel) {
  if (channel == 14) return 2484e6;
  if (channel < 14) return (2407 + 5 * channel) * 1e6;
  return (5000 + 5 * channel) * 1e6;
}

/*!
 * \brief Builds the USRP parameters shared by both modes and prints the
 * frequency and bandwidth being used. The caller sets the gain for the
 * direction it uses; the other direction keeps the #usrp_params default.
 */
static usrp_params to_usrp_params(const radio_options& opts) {
  usrp_params params;
  params.freq =
      opts.channel ? channel_to_freq(opts.channel) : opts.freq_ghz * 1e9;
  params.rate = opts.bandwidth_mhz * 1e6;
  params.device_addr = opts.device;

  if (opts.bandwidth_mhz != 5 && opts.bandwidth_mhz != 10 &&
      opts.bandwidth_mhz != 20) {
    std::cerr << "Warning: 802.11a uses a 5, 10 or 20 MHz bandwidth, not "
              << opts.bandwidth_mhz << " MHz" << std::endl;
  }

  std::cout << "Frequency: " << params.freq / 1e9
            << " GHz, bandwidth: " << opts.bandwidth_mhz << " MHz" << std::endl;
  return params;
}

usrp_params to_usrp_params(const tx_options& opts) {
  usrp_params params = to_usrp_params(opts.radio);
  params.tx_gain = opts.radio.gain;
  params.tx_amp = opts.tx_amp;
  return params;
}

usrp_params to_usrp_params(const rx_options& opts) {
  usrp_params params = to_usrp_params(opts.radio);
  params.rx_gain = opts.radio.gain;
  return params;
}
}  // namespace fun

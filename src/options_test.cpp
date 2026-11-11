#include "options.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>

#include "framing.h"

namespace fun {
namespace {
// Center frequencies in MHz of channels from the 802.11 channel tables, at
// both ends of each band and in between.
const std::vector<std::pair<int, double>> KNOWN_CHANNELS = {
    {1, 2412},  {6, 2437},  {11, 2462},  {13, 2472},  {14, 2484},  {32, 5160},
    {36, 5180}, {64, 5320}, {100, 5500}, {149, 5745}, {165, 5825}, {177, 5885},
};

// Every --phy-rate name and the rate it selects.
const std::vector<std::pair<std::string, Rate>> RATE_NAMES = {
    {"bpsk-1/2", RATE_1_2_BPSK},   {"bpsk-2/3", RATE_2_3_BPSK},
    {"bpsk-3/4", RATE_3_4_BPSK},   {"qpsk-1/2", RATE_1_2_QPSK},
    {"qpsk-2/3", RATE_2_3_QPSK},   {"qpsk-3/4", RATE_3_4_QPSK},
    {"qam16-1/2", RATE_1_2_QAM16}, {"qam16-2/3", RATE_2_3_QAM16},
    {"qam16-3/4", RATE_3_4_QAM16}, {"qam64-2/3", RATE_2_3_QAM64},
    {"qam64-3/4", RATE_3_4_QAM64},
};

TEST(ChannelToFreqTest, MatchesTheChannelTables) {
  for (const auto& [channel, mhz] : KNOWN_CHANNELS) {
    EXPECT_DOUBLE_EQ(channel_to_freq(channel), mhz * 1e6)
        << "channel " << channel;
  }
}

// With the known channels at the ends of each band, this pins down every
// channel in between.
TEST(ChannelToFreqTest, EveryChannelIs5MHzAboveTheLastInItsBand) {
  for (int channel = 2; channel <= 13; channel++) {
    EXPECT_DOUBLE_EQ(channel_to_freq(channel) - channel_to_freq(channel - 1),
                     5e6)
        << "channel " << channel;
  }
  for (int channel = 33; channel <= 177; channel++) {
    EXPECT_DOUBLE_EQ(channel_to_freq(channel) - channel_to_freq(channel - 1),
                     5e6)
        << "channel " << channel;
  }
}

TEST(OptionsTest, ParsesEveryRateName) {
  for (const auto& [name, rate] : RATE_NAMES) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    app.parse("--phy-rate " + name, false);
    EXPECT_EQ(opts.phy_rate, rate) << name;
  }
}

TEST(OptionsTest, RejectsAnUnknownRateName) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  EXPECT_THROW(app.parse("--phy-rate qam256-5/6", false), CLI::ValidationError);
}

TEST(OptionsTest, KeepsTheDefaultRateWithoutAnOption) {
  CLI::App app;
  tx_options opts;
  add_options(&app, opts);
  app.parse("", false);
  EXPECT_EQ(opts.phy_rate, RATE_1_2_BPSK);
}

TEST(OptionsTest, ConvertsTheChannelToAFrequency) {
  CLI::App app;
  rx_options opts;
  add_options(&app, opts);
  app.parse("--channel 36", false);
  EXPECT_DOUBLE_EQ(to_usrp_params(opts).freq, 5180e6);
}

TEST(OptionsTest, RejectsBothAFrequencyAndAChannel) {
  CLI::App app;
  rx_options opts;
  add_options(&app, opts);
  EXPECT_THROW(app.parse("--freq 5.18 --channel 36", false),
               CLI::ExcludesError);
}

TEST(OptionsTest, SetsOnlyTheGainForTheModesDirection) {
  usrp_params defaults;

  CLI::App tx_app;
  tx_options tx_opts;
  add_options(&tx_app, tx_opts);
  tx_app.parse("--gain 12", false);
  usrp_params tx_params = to_usrp_params(tx_opts);
  EXPECT_DOUBLE_EQ(tx_params.tx_gain, 12);
  EXPECT_DOUBLE_EQ(tx_params.rx_gain, defaults.rx_gain);

  CLI::App rx_app;
  rx_options rx_opts;
  add_options(&rx_app, rx_opts);
  rx_app.parse("--gain 12", false);
  usrp_params rx_params = to_usrp_params(rx_opts);
  EXPECT_DOUBLE_EQ(rx_params.rx_gain, 12);
  EXPECT_DOUBLE_EQ(rx_params.tx_gain, defaults.tx_gain);
}

TEST(DataOptionsTest, HasNoLimitWithoutAnOption) {
  CLI::App app;
  tx_options opts;
  add_options(&app, opts);
  app.parse("", false);
  EXPECT_EQ(opts.data.limit(), std::nullopt);
}

TEST(DataOptionsTest, SendsFullPacketsForPackets) {
  CLI::App app;
  tx_options opts;
  add_options(&app, opts);
  app.parse("--packets 3", false);
  EXPECT_EQ(opts.data.limit(), 3 * FRAME_DATA_SIZE);
}

TEST(DataOptionsTest, ParsesSizesForBytes) {
  const std::vector<std::pair<std::string, std::uint64_t>> SIZES = {
      {"1", 1},           {"1500", 1500}, {"64KiB", 64 * 1024},
      {"10MB", 10000000}, {"1kb", 1000},  {"2MiB", 2 * 1024 * 1024},
      {"1GB", 1000000000}};
  for (const auto& [size, bytes] : SIZES) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    app.parse("--bytes " + size, false);
    EXPECT_EQ(opts.data.limit(), bytes) << size;
  }
}

TEST(DataOptionsTest, SendsFullPayloadsOfTheGivenSize) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  app.parse("--packets 3 --payload-size 1000", false);
  EXPECT_EQ(opts.data.payload_size, 1000u);
  EXPECT_EQ(opts.data.limit(), 3 * (1000 - FRAME_HEADER_SIZE));
}

TEST(DataOptionsTest, HasTheLargestPayloadsWithoutAnOption) {
  CLI::App app;
  tx_options opts;
  add_options(&app, opts);
  app.parse("", false);
  EXPECT_EQ(opts.data.payload_size, MAX_PAYLOAD_SIZE);
}

TEST(DataOptionsTest, AcceptsPayloadsFromOneDataByteTo1500Bytes) {
  for (std::size_t size : {FRAME_HEADER_SIZE + 1, MAX_PAYLOAD_SIZE}) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    app.parse("--payload-size " + std::to_string(size), false);
    EXPECT_EQ(opts.data.payload_size, size);
  }
  for (std::size_t size : {FRAME_HEADER_SIZE, MAX_PAYLOAD_SIZE + 1}) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    EXPECT_THROW(app.parse("--payload-size " + std::to_string(size), false),
                 CLI::ValidationError)
        << size;
  }
}

TEST(DataOptionsTest, RejectsZero) {
  for (const std::string& option : {"--packets 0", "--bytes 0"}) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    EXPECT_THROW(app.parse(option, false), CLI::ValidationError) << option;
  }
}

TEST(DataOptionsTest, RejectsBothPacketsAndBytes) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  EXPECT_THROW(app.parse("--packets 3 --bytes 1MB", false), CLI::ExcludesError);
}

TEST(DataOptionsTest, AreOnlyForTheModesThatSend) {
  CLI::App app;
  rx_options opts;
  add_options(&app, opts);
  EXPECT_THROW(app.parse("--packets 3", false), CLI::ExtrasError);
}
TEST(SeedOptionTest, IsUnsetWithoutTheOption) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  app.parse("", false);
  EXPECT_EQ(opts.seed, std::nullopt);
}

TEST(SeedOptionTest, ParsesEvery64BitSeed) {
  for (std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{1},
                             std::numeric_limits<std::uint64_t>::max()}) {
    CLI::App app;
    tx_options opts;
    add_options(&app, opts);
    app.parse("--seed " + std::to_string(seed), false);
    EXPECT_EQ(opts.seed, seed);
  }
}

TEST(SeedOptionTest, RejectsNegativeAndTooLargeSeeds) {
  for (const std::string& seed : {"-1", "18446744073709551616", "abc"}) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    EXPECT_THROW(app.parse("--seed " + seed, false), CLI::ParseError) << seed;
  }
}

TEST(SeedOptionTest, IsOnlyForTheModesWithRandomness) {
  CLI::App app;
  rx_options opts;
  add_options(&app, opts);
  EXPECT_THROW(app.parse("--seed 1", false), CLI::ExtrasError);
}

TEST(ChannelOptionsTest, AreAPerfectChannelWithoutOptions) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  app.parse("", false);
  EXPECT_DOUBLE_EQ(opts.bandwidth_mhz, 5);
  EXPECT_EQ(opts.channel.snr_db, std::nullopt);
  EXPECT_EQ(opts.channel.cfo_hz, 0);
  EXPECT_EQ(opts.channel.timing_offset, 0);
  EXPECT_EQ(opts.channel.delay_spread_ns, 0);
}

TEST(ChannelOptionsTest, ParsesEachImpairment) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  app.parse(
      "--bandwidth 20 --snr -3.5 --cfo -1500 --timing-offset 0.25 "
      "--delay-spread 50",
      false);
  EXPECT_DOUBLE_EQ(opts.bandwidth_mhz, 20);
  EXPECT_EQ(opts.channel.snr_db, -3.5);
  EXPECT_DOUBLE_EQ(opts.channel.cfo_hz, -1500);
  EXPECT_DOUBLE_EQ(opts.channel.timing_offset, 0.25);
  EXPECT_DOUBLE_EQ(opts.channel.delay_spread_ns, 50);
}

TEST(ChannelOptionsTest, ParsesTheCfoWithAUnit) {
  const std::vector<std::pair<std::string, double>> CFOS = {
      {"1500", 1500},        {"1500Hz", 1500},
      {"1.5kHz", 1500},      {"1.5khz", 1500},
      {"\"1.5 kHz\"", 1500}, {"0.1MHz", 100000},
      {"-2.5kHz", -2500},    {"1GHz", 1e9},
      {"1e3kHz", 1e6},       {"2.4123456MHz", 2412345.6},
  };
  for (const auto& [cfo, hz] : CFOS) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    app.parse("--cfo " + cfo, false);
    // Exactly the double nearest the number in Hz, as if it were typed out
    EXPECT_EQ(opts.channel.cfo_hz, hz) << cfo;
  }
}

TEST(ChannelOptionsTest, KeepsEveryDigitOfALargeCfo) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  app.parse("--cfo 123456789.123", false);
  EXPECT_EQ(opts.channel.cfo_hz, 123456789.123);
}

TEST(ChannelOptionsTest, RejectsACfoThatIsntAFrequency) {
  for (const std::string& cfo :
       {"kHz", "1.5kbps", "\"1.5 k\"", "abc", "inf", "1e400", "1e300GHz"}) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    EXPECT_THROW(app.parse("--cfo " + cfo, false), CLI::ValidationError) << cfo;
  }
}

TEST(ChannelOptionsTest, RejectsNegativeDelaysAndBandwidths) {
  for (const std::string& option :
       {"--timing-offset -1", "--delay-spread -1", "--bandwidth 0"}) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    EXPECT_THROW(app.parse(option, false), CLI::ValidationError) << option;
  }
}

TEST(ChannelOptionsTest, AreOnlyForSim) {
  CLI::App app;
  tx_options opts;
  add_options(&app, opts);
  EXPECT_THROW(app.parse("--snr 10", false), CLI::ExtrasError);
}

TEST(RateNameTest, IsTheOptionName) {
  for (const auto& [name, rate] : RATE_NAMES) EXPECT_EQ(rate_name(rate), name);
}

TEST(JsonOptionTest, IsUnsetWithoutTheOption) {
  CLI::App app;
  rx_options opts;
  add_options(&app, opts);
  app.parse("", false);
  EXPECT_EQ(opts.json_file, std::nullopt);
}

TEST(JsonOptionTest, IsStdoutWithoutAFileName) {
  for (const std::string& args : {"--json", "--json -"}) {
    CLI::App app;
    sim_options opts;
    add_options(&app, opts);
    app.parse(args, false);
    EXPECT_EQ(opts.json_file, "-") << args;
  }
}

TEST(JsonOptionTest, DoesntTakeTheNextOptionAsItsFileName) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  app.parse("--json --packets 3", false);
  EXPECT_EQ(opts.json_file, "-");
  EXPECT_EQ(opts.data.packets, 3u);
}

TEST(JsonOptionTest, RejectsAnEmptyFileName) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  // CLI11 takes the arguments in reverse order: this is `--json ""`.
  std::vector<std::string> args = {"", "--json"};
  EXPECT_THROW(app.parse(args), CLI::ValidationError);
}

TEST(JsonOptionTest, SetsTheFileInEveryMode) {
  CLI::App tx_app;
  tx_options tx_opts;
  add_options(&tx_app, tx_opts);
  tx_app.parse("--json tx.json", false);
  EXPECT_EQ(tx_opts.json_file, "tx.json");

  CLI::App rx_app;
  rx_options rx_opts;
  add_options(&rx_app, rx_opts);
  rx_app.parse("--json rx.json", false);
  EXPECT_EQ(rx_opts.json_file, "rx.json");

  CLI::App sim_app;
  sim_options sim_opts;
  add_options(&sim_app, sim_opts);
  sim_app.parse("--json sim.json", false);
  EXPECT_EQ(sim_opts.json_file, "sim.json");
}

TEST(TraceOptionsTest, AreOffWithoutTheOptions) {
  CLI::App app;
  sim_options opts;
  add_options(&app, opts);
  app.parse("", false);
  EXPECT_EQ(opts.trace.file, std::nullopt);
  EXPECT_FALSE(opts.trace.system);
}

TEST(TraceOptionsTest, AreForTheModesThatReceive) {
  CLI::App rx_app;
  rx_options rx_opts;
  add_options(&rx_app, rx_opts);
  rx_app.parse("--trace rx.pftrace --trace-system", false);
  EXPECT_EQ(rx_opts.trace.file, "rx.pftrace");
  EXPECT_TRUE(rx_opts.trace.system);

  CLI::App sim_app;
  sim_options sim_opts;
  add_options(&sim_app, sim_opts);
  sim_app.parse("--trace sim.pftrace", false);
  EXPECT_EQ(sim_opts.trace.file, "sim.pftrace");

  CLI::App tx_app;
  tx_options tx_opts;
  add_options(&tx_app, tx_opts);
  EXPECT_THROW(tx_app.parse("--trace tx.pftrace", false), CLI::ExtrasError);
}

}  // namespace
}  // namespace fun

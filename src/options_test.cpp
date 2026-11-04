#include "options.h"

#include <gtest/gtest.h>

#include <cstdint>
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
}  // namespace
}  // namespace fun

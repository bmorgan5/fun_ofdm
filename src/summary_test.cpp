#include "summary.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <boost/json.hpp>

namespace fun {
namespace {
using namespace std::chrono_literals;

namespace json = boost::json;

// Parses the options as fun-ofdm would.
template <typename Options>
Options parse(const std::string& args) {
  CLI::App app;
  Options opts;
  add_options(&app, opts);
  app.parse(args, false);
  return opts;
}

// The summary, parsed back. Comparing parsed values checks each member's type
// too: 30.0 and 30 differ.
json::value summary(const std::string& text) { return json::parse(text); }

// A summary without its metrics, which have tests of their own.
json::value without_metrics(json::value value) {
  value.as_object().erase("metrics");
  return value;
}

// The keys of an object, in order.
std::vector<std::string> keys(const json::value& value) {
  std::vector<std::string> names;
  for (const auto& member : value.as_object()) names.emplace_back(member.key());
  return names;
}

const transfer_totals TOTALS = {.packets = 2, .bytes = 1000, .elapsed = 500ms};
const char* TOTALS_JSON =
    R"({"packets": 2, "bytes": 1000, "seconds": 0.5,
        "bits_per_second": 16000.0})";

// The exact text, unlike the other tests, which compare parsed values, up to
// the metrics.
TEST(SummaryTest, WritesAMemberPerLineIndentedBy2) {
  rx_result result = {
      .received = {.packets = 100, .bytes = 149600, .elapsed = 47ms}};
  std::string text =
      to_json("1.2.3", parse<rx_options>("--channel 36"), result);
  EXPECT_TRUE(text.starts_with(R"({
  "version": "1.2.3",
  "mode": "rx",
  "options": {
    "freq": null,
    "channel": 36,
    "bandwidth": 5.0,
    "gain": 30.0,
    "device": null,
    "no_realtime": false
  },
  "received": {
    "packets": 100,
    "bytes": 149600,
    "seconds": 0.047,
    "bits_per_second": 25463829.78723404
  },
  "lost": 0,
  "duplicates": 0,
  "metrics": {
    "enabled": )"))
      << text;
}

// Boost.JSON on its own would write 3E1 and 4.7E-2.
TEST(SummaryTest, WritesDoublesInTheirShortestForm) {
  tx_options opts;
  opts.tx_amp = 1e-7;
  opts.radio.gain = 1e21;
  std::string text = to_json("1.2.3", opts, tx_result{});
  EXPECT_TRUE(text.contains(R"("freq": 5.72,)")) << text;
  EXPECT_TRUE(text.contains(R"("gain": 1e+21,)")) << text;
  EXPECT_TRUE(text.contains(R"("tx_amp": 1e-07,)")) << text;
  EXPECT_TRUE(text.contains(R"("seconds": 0.0,)")) << text;
}

TEST(SummaryTest, WritesNonFiniteDoublesAsNull) {
  for (double gain : {std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity()}) {
    rx_options opts;
    opts.radio.gain = gain;
    json::value parsed = summary(to_json("1.2.3", opts, rx_result{}));
    EXPECT_TRUE(
        parsed.as_object().at("options").as_object().at("gain").is_null())
        << gain;
  }
}

TEST(SummaryTest, WritesTheTxDefaults) {
  tx_result result = {.seed = 42, .sent = TOTALS};
  EXPECT_EQ(
      summary(to_json("1.2.3", parse<tx_options>(""), result)), summary(R"({
              "version": "1.2.3",
              "mode": "tx",
              "options": {
                "freq": 5.72,
                "channel": null,
                "bandwidth": 5.0,
                "gain": 30.0,
                "device": null,
                "no_realtime": false,
                "tx_amp": 0.5,
                "phy_rate": "bpsk-1/2",
                "packets": null,
                "bytes": null
              },
              "seed": 42,
              "sent": )" + std::string(TOTALS_JSON) + "}"));
}

TEST(SummaryTest, WritesTheGivenTxOptions) {
  tx_options opts = parse<tx_options>(
      "--channel 36 --bandwidth 20 --gain 25 --device addr=192.168.10.2 "
      "--no-realtime --tx-amp 0.25 --phy-rate qam64-3/4 --bytes 1MB --seed 7 "
      "--json out.json");
  EXPECT_EQ(
      summary(to_json("1.2.3", opts, tx_result{})).as_object().at("options"),
      summary(R"({
              "freq": null,
              "channel": 36,
              "bandwidth": 20.0,
              "gain": 25.0,
              "device": "addr=192.168.10.2",
              "no_realtime": true,
              "tx_amp": 0.25,
              "phy_rate": "qam64-3/4",
              "packets": null,
              "bytes": 1000000
            })"));
}

TEST(SummaryTest, WritesAnRxRun) {
  rx_result result = {.received = TOTALS, .lost = 3, .duplicates = 1};
  EXPECT_EQ(without_metrics(summary(to_json(
                "1.2.3", parse<rx_options>("--freq 2.412 --gain 0"), result))),
            summary(R"({
              "version": "1.2.3",
              "mode": "rx",
              "options": {
                "freq": 2.412,
                "channel": null,
                "bandwidth": 5.0,
                "gain": 0.0,
                "device": null,
                "no_realtime": false
              },
              "received": )" +
                    std::string(TOTALS_JSON) +
                    R"(,
              "lost": 3,
              "duplicates": 1
            })"));
}

TEST(SummaryTest, WritesASimRunThatFailed) {
  sim_result result = {
      .seed = std::numeric_limits<std::uint64_t>::max(),
      .received = TOTALS,
      .check = sim_check{.lost = 1, .duplicates = 2, .mismatched_bytes = 3}};
  EXPECT_EQ(without_metrics(summary(
                to_json("1.2.3", parse<sim_options>("--packets 3"), result))),
            summary(R"({
        "version": "1.2.3",
        "mode": "sim",
        "options": {"phy_rate": "qam16-3/4", "packets": 3, "bytes": null,
                    "bandwidth": 5.0, "snr": null, "cfo": 0.0,
                    "timing_offset": 0.0, "delay_spread": 0.0},
        "seed": 18446744073709551615,
        "received": )" +
                    std::string(TOTALS_JSON) +
                    R"(,
        "lost": 1,
        "duplicates": 2,
        "mismatched_bytes": 3,
        "passed": false
      })"));
}

TEST(SummaryTest, WritesTheSimDefaultPackets) {
  json::value parsed =
      summary(to_json("1.2.3", parse<sim_options>(""), sim_result{}));
  EXPECT_EQ(parsed.as_object().at("options"),
            summary(R"({"phy_rate": "qam16-3/4", "packets": 100,
                        "bytes": null, "bandwidth": 5.0, "snr": null,
                        "cfo": 0.0, "timing_offset": 0.0,
                        "delay_spread": 0.0})"));
}

TEST(SummaryTest, WritesTheSimBytesInsteadOfTheDefault) {
  json::value parsed = summary(
      to_json("1.2.3", parse<sim_options>("--bytes 1500"), sim_result{}));
  EXPECT_EQ(parsed.as_object().at("options"),
            summary(R"({"phy_rate": "qam16-3/4", "packets": null,
                        "bytes": 1500, "bandwidth": 5.0, "snr": null,
                        "cfo": 0.0, "timing_offset": 0.0,
                        "delay_spread": 0.0})"));
}

TEST(SummaryTest, WritesTheGivenSimChannelOptions) {
  sim_options opts = parse<sim_options>(
      "--bandwidth 20 --snr -3 --cfo -1500 --timing-offset 2.5 "
      "--delay-spread 50");
  json::value parsed = summary(to_json("1.2.3", opts, sim_result{}));
  EXPECT_EQ(parsed.as_object().at("options"),
            summary(R"({"phy_rate": "qam16-3/4", "packets": 100,
                        "bytes": null, "bandwidth": 20.0, "snr": -3.0,
                        "cfo": -1500.0, "timing_offset": 2.5,
                        "delay_spread": 50.0})"));
}

TEST(SummaryTest, PassesASimRunWithNothingLostOrMismatched) {
  sim_result result = {.check = sim_check{.duplicates = 2}};
  json::value parsed = summary(to_json("1.2.3", sim_options{}, result));
  EXPECT_EQ(parsed.as_object().at("passed"), true);
}

TEST(SummaryTest, LeavesOutTheCheckOfAStoppedSimRun) {
  sim_result result = {.seed = 1, .received = TOTALS};
  json::value parsed = summary(to_json("1.2.3", sim_options{}, result));
  EXPECT_EQ(keys(parsed),
            (std::vector<std::string>{"version", "mode", "options", "seed",
                                      "received", "metrics"}));
}

TEST(SummaryTest, WritesTheMembersInOrder) {
  sim_result result = {.check = sim_check{}};
  EXPECT_EQ(keys(summary(to_json("1.2.3", sim_options{}, result))),
            (std::vector<std::string>{
                "version", "mode", "options", "seed", "received", "lost",
                "duplicates", "mismatched_bytes", "passed", "metrics"}));
  EXPECT_EQ(keys(summary(to_json("1.2.3", tx_options{}, tx_result{}))
                     .as_object()
                     .at("sent")),
            (std::vector<std::string>{"packets", "bytes", "seconds",
                                      "bits_per_second"}));
}

TEST(SummaryTest, WritesANullThroughputIfNoTimePassed) {
  json::value parsed = summary(to_json("1.2.3", tx_options{}, tx_result{}));
  EXPECT_EQ(parsed.as_object().at("sent"),
            summary(R"({"packets": 0, "bytes": 0, "seconds": 0.0,
                        "bits_per_second": null})"));
}

TEST(SummaryTest, EscapesStrings) {
  rx_options opts;
  opts.radio.device = "a\"b\\c\n";
  json::value parsed = summary(to_json("1.2.3", opts, rx_result{}));
  EXPECT_EQ(parsed.as_object().at("options").as_object().at("device"),
            "a\"b\\c\n");
}
// A histogram of 100, 200 and 300 ns. Values below 2048 have buckets of their
// own, at 3 significant digits, so they come back exactly.
metrics::histogram hundreds() {
  metrics::histogram h;
  for (std::int64_t ns : {100, 200, 300}) h.record(ns);
  return h;
}

const char* HUNDREDS_JSON =
    R"({"count": 3, "p50": 200, "p99": 300, "p99_9": 300, "max": 300,
        "buckets": [[100, 1], [200, 1], [300, 1]]})";

const char* EMPTY_JSON =
    R"({"count": 0, "p50": 0, "p99": 0, "p99_9": 0, "max": 0, "buckets": []})";

// A count per rate, all 0 but one.
std::string per_rate(const std::string& rate, int count) {
  std::string out = "{";
  for (int r = 0; r < static_cast<int>(RATE_COUNT); r++) {
    std::string name = rate_name(static_cast<Rate>(r));
    out += (r ? ", \"" : "\"") + name +
           "\": " + std::to_string(name == rate ? count : 0);
  }
  return out + "}";
}

TEST(SummaryTest, WritesTheReceiverChainsMetrics) {
  if (!metrics::ENABLED) GTEST_SKIP() << "Built without metrics";
  sim_result result;
  result.metrics.latency = hundreds();
  result.metrics.blocks.push_back({.name = "frame_detector"});
  result.metrics.blocks[0].processing = hundreds();
  result.metrics.frames_detected = 7;
  result.metrics.sync_lost = 1;
  result.metrics.signal_failed = 2;
  result.metrics.crc_ok[RATE_3_4_QAM16] = 3;
  result.metrics.crc_failed[RATE_3_4_QAM16] = 1;

  EXPECT_EQ(summary(to_json("1.2.3", sim_options{}, result))
                .as_object()
                .at("metrics"),
            summary(std::string(R"({
              "enabled": true,
              "frames": {"detected": 7, "sync_lost": 1, "signal_failed": 2,
                         "crc_ok": )") +
                    per_rate("qam16-3/4", 3) + R"(, "crc_failed": )" +
                    per_rate("qam16-3/4", 1) + R"(},
              "arrival_interval": )" +
                    EMPTY_JSON + R"(,
              "latency": )" +
                    HUNDREDS_JSON +
                    R"(,
              "blocks": {"frame_detector": {"wait": )" +
                    EMPTY_JSON + R"(, "processing": )" + HUNDREDS_JSON +
                    R"(, "start_interval": )" + EMPTY_JSON + R"(, "lag": )" +
                    EMPTY_JSON + "}}}"));
}

TEST(SummaryTest, AddsRxsOwnMetrics) {
  if (!metrics::ENABLED) GTEST_SKIP() << "Built without metrics";
  rx_result result = {.overflows = 4, .recv_delay = hundreds()};
  json::value parsed = summary(to_json("1.2.3", rx_options{}, result));
  const json::object& m = parsed.as_object().at("metrics").as_object();
  EXPECT_EQ(keys(m), (std::vector<std::string>{"enabled", "frames", "overflows",
                                               "arrival_interval", "recv_delay",
                                               "latency", "blocks"}));
  EXPECT_EQ(m.at("overflows"), 4);
  EXPECT_EQ(m.at("recv_delay"), summary(HUNDREDS_JSON));
}

TEST(SummaryTest, SaysWhetherTheBuildHasMetrics) {
  json::value parsed = summary(to_json("1.2.3", sim_options{}, sim_result{}));
  EXPECT_EQ(parsed.as_object().at("metrics").as_object().at("enabled"),
            metrics::ENABLED);
}

}  // namespace
}  // namespace fun

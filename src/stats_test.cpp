#include "stats.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace fun {
namespace {
using namespace std::chrono_literals;

TEST(TransferStatsTest, StartsAtZero) {
  transfer_stats stats;
  EXPECT_EQ(stats.packets(), 0u);
}

TEST(TransferStatsTest, CountsPackets) {
  transfer_stats stats;
  stats.add_packet(1500);
  stats.add_packet(0);
  stats.add_packet(100);
  EXPECT_EQ(stats.packets(), 3u);
}

TEST(TransferStatsTest, TotalsTheCounts) {
  transfer_stats stats;
  stats.add_packet(1500);
  stats.add_packet(100);

  transfer_totals totals = stats.totals();
  EXPECT_EQ(totals.packets, 2u);
  EXPECT_EQ(totals.bytes, 1600u);
  EXPECT_GE(totals.elapsed, std::chrono::nanoseconds::zero());
}

std::string printed(const transfer_totals& totals, const std::string& verb) {
  testing::internal::CaptureStdout();
  totals.print(verb);
  return testing::internal::GetCapturedStdout();
}

TEST(TransferTotalsTest, PrintsTheCountsTimeAndThroughput) {
  EXPECT_EQ(printed({.packets = 1000, .bytes = 1500000, .elapsed = 12345600us},
                    "Sent"),
            "Sent 1000 packets (1500000 bytes) in 12.346 s, 972.0 kbit/s\n");
  EXPECT_EQ(
      printed({.packets = 100, .bytes = 149600, .elapsed = 47ms}, "Received"),
      "Received 100 packets (149600 bytes) in 0.047 s, 25.5 Mbit/s\n");
}

TEST(TransferTotalsTest, PrintsNoThroughputIfNoTimePassed) {
  EXPECT_EQ(printed({}, "Received"),
            "Received 0 packets (0 bytes) in 0.000 s\n");
}

}  // namespace
}  // namespace fun

#include "stats.h"

#include <gtest/gtest.h>

#include <string>

namespace fun {
namespace {
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

// The elapsed time and throughput depend on the clock, so this only checks the
// counts before them.
TEST(TransferStatsTest, PrintsThePacketAndByteCounts) {
  transfer_stats stats;
  stats.add_packet(1500);
  stats.add_packet(100);

  testing::internal::CaptureStdout();
  stats.print_summary("Sent");
  std::string summary = testing::internal::GetCapturedStdout();

  EXPECT_TRUE(summary.starts_with("Sent 2 packets (1600 bytes) in "))
      << summary;
  EXPECT_TRUE(summary.ends_with("\n")) << summary;
}
}  // namespace
}  // namespace fun

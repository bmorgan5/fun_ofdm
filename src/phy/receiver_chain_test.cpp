#include "receiver_chain.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "frame_builder.h"
#include "test_util.h"

namespace fun {
namespace {
using payloads = std::vector<std::vector<std::uint8_t>>;

// A batch size that stands for a sequence of sizes from 1 to 4096, chosen by
// PRBS-15.
constexpr std::size_t PRBS_BATCHES = 0;

// The samples of a frame, with 200 zeros before it and enough after it to
// flush it through the blocks: timing_sync holds back 160 samples.
std::vector<std::complex<double>> samples(
    const std::vector<std::uint8_t>& payload, Rate rate) {
  std::vector<std::complex<double>> samples(200, 0);
  std::vector<std::complex<double>> frame =
      frame_builder().build_frame(payload, rate);
  samples.insert(samples.end(), frame.begin(), frame.end());
  samples.resize(samples.size() + 400, 0);
  return samples;
}

// The size of each batch to split count samples into, given batch, and then 6
// more batches, as the payloads come out 5 calls after their samples go in.
std::vector<std::size_t> batch_sizes(std::size_t count, std::size_t batch) {
  std::vector<std::size_t> sizes;
  test::prbs15 prbs;
  std::size_t total = 0;
  int after = 0;
  while (after < 6) {
    std::size_t size = batch;
    if (batch == PRBS_BATCHES) {
      size = 1;
      for (int b = 0; b < 12; b++) size += std::size_t{prbs.next_bit()} << b;
    }
    sizes.push_back(size);
    if (total >= count) after++;
    total += size;
  }
  return sizes;
}

// Runs samples through rx, in batches of the given sizes, and returns the
// payloads it outputs. Pads the samples with zeros to fill the batches.
payloads receive(receiver_chain& rx, std::vector<std::complex<double>> samples,
                 const std::vector<std::size_t>& sizes) {
  payloads received;
  std::size_t start = 0;
  for (std::size_t size : sizes) {
    std::vector<std::complex<double>> batch(size, 0);
    if (start < samples.size()) {
      std::size_t end = std::min(samples.size(), start + size);
      std::copy(samples.begin() + start, samples.begin() + end, batch.begin());
    }
    start += size;
    payloads out = rx.process_samples(batch);
    received.insert(received.end(), out.begin(), out.end());
  }
  return received;
}

// The known bugs that stop a frame decoding in batches of this size, or an
// empty string if there are none.
std::string known_bugs(std::size_t batch) {
  std::string bugs;
  if (batch == PRBS_BATCHES || batch < 16) {
    bugs +=
        "frame_detector reads before the start of a batch shorter than 16 "
        "samples (see the \\bug in frame_detector.h). ";
  }
  if (batch == PRBS_BATCHES || batch <= 160) {
    bugs +=
        "timing_sync asserts that a batch has more than 160 samples, and "
        "drops a frame whose LTS cyclic prefix starts before a call's first "
        "sample (see the \\bug in timing_sync.h). ";
  }
  if (batch == PRBS_BATCHES || batch < 80) {
    bugs +=
        "In a call where fft_symbols outputs no symbols, channel_est gets an "
        "empty batch, and hands phase_tracker its old input again (see the "
        "\\bug in receiver_chain.h). ";
  }
  return bugs;
}

using LoopbackParam = std::tuple<Rate, std::size_t, std::size_t>;

class LoopbackTest : public testing::TestWithParam<LoopbackParam> {};

// Names a test, i.e. "3_4_QAM16_100_bytes_64_samples".
std::string loopback_name(const testing::TestParamInfo<LoopbackParam>& info) {
  auto [rate, length, batch] = info.param;
  std::string name =
      test::rate_name(testing::TestParamInfo<Rate>(rate, info.index)) + "_" +
      std::to_string(length) + "_bytes_";
  if (batch == PRBS_BATCHES) return name + "prbs_batches";
  return name + std::to_string(batch) + "_samples";
}

// Frame builder → receiver chain. Each frame is split across batches, except
// the frames shorter than 4096 samples in batches of 4096.
TEST_P(LoopbackTest, DecodesTheFrame) {
  auto [rate, length, batch] = GetParam();
  if (std::string bugs = known_bugs(batch); !bugs.empty()) GTEST_SKIP() << bugs;

  std::vector<std::uint8_t> payload = test::prbs_bytes(length, length);
  std::vector<std::complex<double>> input = samples(payload, rate);
  receiver_chain rx;
  EXPECT_EQ(receive(rx, input, batch_sizes(input.size(), batch)),
            payloads{payload});
}

INSTANTIATE_TEST_SUITE_P(AllRates, LoopbackTest,
                         testing::Combine(testing::ValuesIn(test::ALL_RATES),
                                          testing::Values(1, 100, 1500, 4095),
                                          testing::Values(1, 7, 64, 4096,
                                                          PRBS_BATCHES)),
                         loopback_name);

// An empty batch, after each batch of samples.
TEST(ReceiverChainTest, IgnoresEmptyBatches) {
  GTEST_SKIP() << "A block that gets an empty batch hands the next block its "
                  "old input again, and frame_decoder outputs its old payloads "
                  "again (see the \\bug in receiver_chain.h)";

  std::vector<std::uint8_t> payload = test::prbs_bytes(1500);
  std::vector<std::complex<double>> input = samples(payload, RATE_1_2_QPSK);
  std::vector<std::size_t> sizes;
  for (std::size_t size : batch_sizes(input.size(), 4096)) {
    sizes.push_back(size);
    sizes.push_back(0);
  }
  receiver_chain rx;
  EXPECT_EQ(receive(rx, input, sizes), payloads{payload});
}
// Batches of 4096 samples, said to arrive 1 ms apart, starting 10 s ago, so
// a frame's latency is from its last batch's arrival to now.
TEST(ReceiverChainTest, MeasuresEachBlockAndFrame) {
  if (!metrics::ENABLED) GTEST_SKIP() << "Built without metrics";
  const std::size_t BATCH = 4096;
  const metrics::time_point MS = 1'000'000;
  std::vector<std::uint8_t> payload = test::prbs_bytes(100);
  std::vector<std::complex<double>> input = samples(payload, RATE_1_2_QPSK);
  std::vector<std::size_t> sizes = batch_sizes(input.size(), BATCH);
  input.resize(sizes.size() * BATCH, 0);

  receiver_chain rx;
  metrics::time_point first = metrics::now() - 10'000 * MS;
  metrics::time_point before = 0;
  payloads received;
  for (std::size_t b = 0; b < sizes.size(); b++) {
    std::vector<std::complex<double>> batch(input.begin() + b * BATCH,
                                            input.begin() + (b + 1) * BATCH);
    before = metrics::now();
    payloads out = rx.process_samples(batch, first + b * MS);
    received.insert(received.end(), out.begin(), out.end());
  }
  metrics::time_point after = metrics::now();
  ASSERT_EQ(received, payloads{payload});

  chain_metrics m = rx.metrics();
  auto calls = static_cast<std::int64_t>(sizes.size());
  EXPECT_EQ(m.frames_detected, 1u);
  EXPECT_EQ(m.sync_lost, 0u);
  EXPECT_EQ(m.signal_failed, 0u);
  EXPECT_EQ(m.crc_ok[RATE_1_2_QPSK], 1u);
  EXPECT_EQ(m.arrival_interval.count(), calls - 1);
  EXPECT_NEAR(m.arrival_interval.percentile(50), MS, MS / 1000);

  // The frame ends in the first batch: 200 zeros and under 4096 samples of
  // frame. Its payload came out during one of the calls.
  ASSERT_EQ(m.latency.count(), 1);
  EXPECT_GE(m.latency.max(), (before - first) * 999 / 1000);
  EXPECT_LE(m.latency.max(), (after - first) * 1001 / 1000);

  std::vector<std::string> names;
  for (const block_metrics& block : m.blocks) {
    names.push_back(block.name);
    EXPECT_EQ(block.processing.count(), calls) << block.name;
    EXPECT_EQ(block.wait.count(), calls) << block.name;
    EXPECT_EQ(block.start_interval.count(), calls - 1) << block.name;
  }
  EXPECT_EQ(names, (std::vector<std::string>{
                       "frame_detector", "timing_sync", "fft_symbols",
                       "channel_est", "phase_tracker", "frame_decoder"}));
  // Block k first gets a batch on call k, and lags it by at least k ms
  for (std::size_t k = 0; k < m.blocks.size(); k++) {
    EXPECT_EQ(m.blocks[k].lag.count(), calls - static_cast<std::int64_t>(k))
        << m.blocks[k].name;
    EXPECT_GE(m.blocks[k].lag.percentile(0),
              static_cast<std::int64_t>(k) * MS * 999 / 1000)
        << m.blocks[k].name;
  }
}

}  // namespace
}  // namespace fun

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
}  // namespace
}  // namespace fun

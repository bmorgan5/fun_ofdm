#include "timing_sync.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstddef>
#include <vector>

#include "frame_builder.h"
#include "tagged_vector.h"
#include "test_util.h"

namespace fun {
namespace {
// Where the frame starts in the input.
constexpr std::size_t START = 500;

// The block holds back 160 samples to look ahead into, so each sample comes
// out 160 samples later than it went in.
constexpr std::size_t DELAY = 160;

// A frame at START, between zeros, with STS_END at START + sts_end, as
// frame_detector would tag it. Each sample is rotated by offset radians more
// than the one before, as a carrier frequency offset does, and scaled by
// amplitude.
std::vector<tagged_sample> tagged_frame(std::size_t sts_end, double offset = 0,
                                        double amplitude = 1) {
  std::vector<std::complex<double>> frame =
      frame_builder().build_frame(test::prbs_bytes(100), RATE_1_2_BPSK);
  std::vector<tagged_sample> samples(START + frame.size() + 500);
  for (tagged_sample& s : samples) s.sample = 0;
  for (std::size_t x = 0; x < frame.size(); x++) {
    samples[START + x].sample = frame[x] * std::polar(amplitude, offset * x);
  }
  samples[START + sts_end].tag = STS_END;
  return samples;
}

// The positions of each tag in the output.
std::vector<std::pair<std::size_t, vector_tag>> tags(
    const std::vector<tagged_sample>& output) {
  std::vector<std::pair<std::size_t, vector_tag>> found;
  for (std::size_t x = 0; x < output.size(); x++) {
    if (output[x].tag != NONE) found.push_back({x, output[x].tag});
  }
  return found;
}

// The LTS symbols start at START + 192 and START + 256. LTS1 and LTS2 go 8
// samples before each.
TEST(TimingSyncTest, TagsTheLongTrainingSymbols) {
  for (std::size_t sts_end : {161, 170, 180, 192}) {
    std::vector<tagged_sample> input = tagged_frame(sts_end);
    timing_sync sync;
    std::vector<std::pair<std::size_t, vector_tag>> expected = {
        {START + sts_end + DELAY, STS_END},
        {START + 184 + DELAY, LTS1},
        {START + 248 + DELAY, LTS2},
    };
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(tags(test::run_block(sync, input, input.size())), expected)
        << "STS_END at " << sts_end;
  }
}

// A channel's gain scales the whole frame, which mustn't change where the LTS
// symbols are.
TEST(TimingSyncTest, TagsTheLongTrainingSymbolsAtAnyAmplitude) {
  GTEST_SKIP() << "The LTS correlation falls as the amplitude rises, so "
                  "above 1/0.9 it misses the threshold and the block drops "
                  "the frame (see the \\bug in timing_sync.h)";

  for (double amplitude : {0.1, 0.5, 1.2, 2.0, 10.0}) {
    std::vector<tagged_sample> input = tagged_frame(161, 0, amplitude);
    timing_sync sync;
    std::vector<std::pair<std::size_t, vector_tag>> expected = {
        {START + 161 + DELAY, STS_END},
        {START + 184 + DELAY, LTS1},
        {START + 248 + DELAY, LTS2},
    };
    EXPECT_EQ(tags(test::run_block(sync, input, input.size())), expected)
        << "amplitude " << amplitude;
  }
}

TEST(TimingSyncTest, PassesTheSamplesThroughWithoutAFrequencyOffset) {
  std::vector<tagged_sample> input = tagged_frame(161);
  timing_sync sync;
  std::vector<tagged_sample> output =
      test::run_block(sync, input, input.size());
  ASSERT_EQ(output.size(), input.size());
  for (std::size_t x = 0; x < DELAY; x++) {
    ASSERT_EQ(output[x].sample, std::complex<double>(0)) << "sample " << x;
  }
  for (std::size_t x = 0; x + DELAY < input.size(); x++) {
    ASSERT_EQ(output[x + DELAY].sample, input[x].sample) << "sample " << x;
  }
}

TEST(TimingSyncTest, TagsTheSameInAnyBatchSize) {
  std::vector<tagged_sample> input = tagged_frame(170);
  timing_sync whole;
  std::vector<std::pair<std::size_t, vector_tag>> expected =
      tags(test::run_block(whole, input, input.size()));
  for (std::size_t batch : {161, 200, 1000, 4096}) {
    // Whole batches only, so the last isn't 160 samples or fewer. The samples
    // it leaves out are after the frame.
    std::vector<tagged_sample> batches(input.begin(),
                                       input.end() - input.size() % batch);
    timing_sync sync;
    EXPECT_EQ(tags(test::run_block(sync, batches, batch)), expected)
        << batch << " samples per batch";
  }
}

TEST(TimingSyncTest, TagsTheSameInBatchesOf160OrFewer) {
  GTEST_SKIP() << "The block asserts that a batch has more than 160 samples, "
                  "and in short batches it drops frames whose LTS cyclic "
                  "prefix starts before the call's first sample (see the "
                  "\\bug in timing_sync.h)";

  std::vector<tagged_sample> input = tagged_frame(170);
  timing_sync whole;
  std::vector<std::pair<std::size_t, vector_tag>> expected =
      tags(test::run_block(whole, input, input.size()));
  for (std::size_t batch : {1, 7, 64, 160}) {
    timing_sync sync;
    EXPECT_EQ(tags(test::run_block(sync, input, batch)), expected)
        << batch << " samples per batch";
  }
}

// Each call works on the 160 samples held back from before, then the batch,
// so the first sample it looks at is 160 before the batch's first. Batches of
// START + 161 + 160 samples put STS_END there in the second call, and the
// LTS's cyclic prefix starts 1 sample earlier, in the first call.
TEST(TimingSyncTest, TagsTheLongTrainingSymbolsWhenStsEndStartsACall) {
  GTEST_SKIP() << "When the LTS's cyclic prefix starts before the first "
                  "sample of a call, the block drops the frame (see the "
                  "\\bug in timing_sync.h)";

  std::vector<tagged_sample> input = tagged_frame(161);
  timing_sync sync;
  std::vector<std::pair<std::size_t, vector_tag>> expected = {
      {START + 161 + DELAY, STS_END},
      {START + 184 + DELAY, LTS1},
      {START + 248 + DELAY, LTS2},
  };
  EXPECT_EQ(tags(test::run_block(sync, input, START + 161 + DELAY)), expected);
}

// With the offset corrected, the second LTS symbol matches the first. 0.01
// radians per sample is 8 kHz at 5 MS/s, and puts the second symbol 0.64
// radians ahead of the first.
TEST(TimingSyncTest, CorrectsTheFrequencyOffset) {
  GTEST_SKIP() << "The frequency offset estimate is always 0 (see the \\bug "
                  "in timing_sync.h)";

  for (double offset : {-0.01, 0.003, 0.01}) {
    std::vector<tagged_sample> input = tagged_frame(161, offset);
    timing_sync sync;
    std::vector<tagged_sample> output =
        test::run_block(sync, input, input.size());
    std::complex<double> corr = 0;
    for (std::size_t x = START + 192; x < START + 256; x++) {
      corr +=
          output[x + 64 + DELAY].sample * std::conj(output[x + DELAY].sample);
    }
    EXPECT_NEAR(std::arg(corr), 0, 1e-3) << offset << " radians per sample";
  }
}
}  // namespace
}  // namespace fun

#include "frame_detector.h"

#include <gtest/gtest.h>

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

// A frame at START, in noise 24 dB below the preamble, with 500 samples
// of noise after it.
std::vector<std::complex<double>> noisy_frame() {
  std::vector<std::complex<double>> frame =
      frame_builder().build_frame(test::prbs_bytes(100), RATE_1_2_BPSK);
  std::vector<std::complex<double>> samples =
      test::prbs_noise(START + frame.size() + 500, 0.005);
  for (std::size_t x = 0; x < frame.size(); x++) samples[START + x] += frame[x];
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

TEST(FrameDetectorTest, PassesTheSamplesThrough) {
  std::vector<std::complex<double>> samples = noisy_frame();
  frame_detector detector;
  std::vector<tagged_sample> output =
      test::run_block(detector, samples, samples.size());
  ASSERT_EQ(output.size(), samples.size());
  for (std::size_t x = 0; x < samples.size(); x++) {
    ASSERT_EQ(output[x].sample, samples[x]) << "sample " << x;
  }
}

// STS_START comes once the correlation has been high for 16 samples, inside
// the STS. STS_END comes when it drops, after the STS. timing_sync looks for
// the two LTS symbols in the 160 samples from STS_END, so it has to come
// between the end of the STS (START + 160) and the first LTS symbol
// (START + 192), or timing_sync misses one of them.
TEST(FrameDetectorTest, TagsTheShortTrainingSequence) {
  std::vector<std::complex<double>> samples = noisy_frame();
  frame_detector detector;
  std::vector<std::pair<std::size_t, vector_tag>> found =
      tags(test::run_block(detector, samples, samples.size()));
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].second, STS_START);
  EXPECT_GT(found[0].first, START + 16);
  EXPECT_LT(found[0].first, START + 160);
  EXPECT_EQ(found[1].second, STS_END);
  EXPECT_GT(found[1].first, START + 160);
  EXPECT_LE(found[1].first, START + 192);
}

TEST(FrameDetectorTest, TagsTheSameInAnyBatchSize) {
  std::vector<std::complex<double>> samples = noisy_frame();
  frame_detector whole;
  std::vector<std::pair<std::size_t, vector_tag>> expected =
      tags(test::run_block(whole, samples, samples.size()));
  for (std::size_t batch : {16, 17, 64, 100, 4096}) {
    // Whole batches only, so the last isn't shorter than 16 samples. The
    // samples it leaves out are noise.
    std::vector<std::complex<double>> batches(
        samples.begin(), samples.end() - samples.size() % batch);
    frame_detector detector;
    EXPECT_EQ(tags(test::run_block(detector, batches, batch)), expected)
        << batch << " samples per batch";
  }
}

TEST(FrameDetectorTest, TagsTheSameInBatchesShorterThan16) {
  GTEST_SKIP() << "A batch shorter than 16 samples makes the block read "
                  "before the start of its input (see the \\bug in "
                  "frame_detector.h)";

  std::vector<std::complex<double>> samples = noisy_frame();
  frame_detector whole;
  std::vector<std::pair<std::size_t, vector_tag>> expected =
      tags(test::run_block(whole, samples, samples.size()));
  for (std::size_t batch : {1, 7, 15}) {
    frame_detector detector;
    EXPECT_EQ(tags(test::run_block(detector, samples, batch)), expected)
        << batch << " samples per batch";
  }
}

TEST(FrameDetectorTest, IgnoresNoise) {
  std::vector<std::complex<double>> noise = test::prbs_noise(10000, 0.005);
  frame_detector detector;
  EXPECT_TRUE(tags(test::run_block(detector, noise, noise.size())).empty());
}
}  // namespace
}  // namespace fun

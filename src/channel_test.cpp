#include "channel.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "src/phy/test_util.h"

namespace fun {
namespace {
using samples = std::vector<std::complex<double>>;

const double SAMPLE_RATE = 5e6;

// Test samples: QPSK points from PRBS-15, with a mean power of 2.
samples prbs_samples(std::size_t size) { return test::prbs_noise(size, 1); }

// Sends frames through the channel, then idles for the rest of what they
// sent, and returns it all.
samples send(channel& air, const std::vector<samples>& frames,
             std::size_t idle) {
  samples out;
  for (const samples& frame : frames) {
    samples received = air.transmit(frame);
    out.insert(out.end(), received.begin(), received.end());
  }
  samples rest = air.idle(idle);
  out.insert(out.end(), rest.begin(), rest.end());
  return out;
}

double mean_power(const samples& x) {
  double power = 0;
  for (const auto& sample : x) power += std::norm(sample);
  return power / static_cast<double>(x.size());
}

TEST(ChannelTest, IsPerfectByDefault) {
  channel air(channel_params{}, SAMPLE_RATE, 1);
  samples frame = prbs_samples(1000);
  EXPECT_EQ(air.transmit(frame), frame);
  EXPECT_EQ(air.idle(100), samples(100));
}

TEST(ChannelTest, ReturnsAsManySamplesAsItGets) {
  channel_params params = {.snr_db = 10,
                           .cfo_hz = 1000,
                           .timing_offset = 2.5,
                           .delay_spread_ns = 200};
  channel air(params, SAMPLE_RATE, 1);
  for (std::size_t size : {0, 1, 7, 1000}) {
    EXPECT_EQ(air.transmit(prbs_samples(size)).size(), size);
    EXPECT_EQ(air.idle(size).size(), size);
  }
}

TEST(ChannelTest, DelaysByWholeSamplesAcrossCalls) {
  const std::size_t DELAY = 5;
  channel air(channel_params{.timing_offset = DELAY}, SAMPLE_RATE, 1);
  samples frame = prbs_samples(1000);
  samples first(frame.begin(), frame.begin() + 3);
  samples second(frame.begin() + 3, frame.end());

  samples expected(DELAY);
  expected.insert(expected.end(), frame.begin(), frame.end());
  EXPECT_EQ(send(air, {first, second}, DELAY), expected);
}

// A slow tone is far inside the band, where the interpolation filter is
// accurate.
TEST(ChannelTest, DelaysByAFractionOfASample) {
  const double DELAY = 2.25;
  const double FREQUENCY = 0.01;  // Cycles per sample
  channel air(channel_params{.timing_offset = DELAY}, SAMPLE_RATE, 1);
  auto tone = [&](double t) {
    return std::polar(1.0, 2 * std::numbers::pi * FREQUENCY * t);
  };
  samples frame(1000);
  for (std::size_t n = 0; n < frame.size(); n++)
    frame[n] = tone(static_cast<double>(n));

  samples received = air.transmit(frame);
  double latency = DELAY + channel::INTERPOLATION_LATENCY;
  // Skip the start, where the filter sees the samples before the tone
  for (std::size_t n = 100; n < received.size(); n++) {
    std::complex<double> expected = tone(static_cast<double>(n) - latency);
    EXPECT_NEAR(std::abs(received[n] - expected), 0, 1e-3) << "sample " << n;
  }
}

TEST(ChannelTest, RotatesByTheFrequencyOffsetAcrossCalls) {
  const double CFO = 12500;  // 1/400 of the sample rate
  channel air(channel_params{.cfo_hz = CFO}, SAMPLE_RATE, 1);
  samples ones(300, 1);
  samples received = send(air, {ones, ones}, 0);
  ASSERT_EQ(received.size(), 600u);
  for (std::size_t n = 0; n < received.size(); n++) {
    double phase = 2 * std::numbers::pi * CFO / SAMPLE_RATE * n;
    EXPECT_NEAR(std::abs(received[n] - std::polar(1.0, phase)), 0, 1e-9)
        << "sample " << n;
  }
}

TEST(ChannelTest, AddsNoiseAtTheSnr) {
  const std::size_t SIZE = 100000;
  for (double snr : {0.0, 10.0, 25.0}) {
    channel air(channel_params{.snr_db = snr}, SAMPLE_RATE, 1);
    samples frame = prbs_samples(SIZE);
    samples received = air.transmit(frame);

    samples noise(SIZE);
    std::complex<double> mean = 0;
    for (std::size_t n = 0; n < SIZE; n++) {
      noise[n] = received[n] - frame[n];
      mean += noise[n] / static_cast<double>(SIZE);
    }
    double measured = 10 * std::log10(mean_power(frame) / mean_power(noise));
    EXPECT_NEAR(measured, snr, 0.05) << snr << " dB";
    EXPECT_NEAR(std::abs(mean), 0, 0.01 * std::sqrt(mean_power(noise)))
        << snr << " dB";
  }
}

TEST(ChannelTest, KeepsTheFirstFramesNoiseFloor) {
  channel air(channel_params{.snr_db = 10}, SAMPLE_RATE, 1);
  EXPECT_EQ(air.idle(100), samples(100));
  air.transmit(prbs_samples(1000));  // Mean power 2
  samples quiet(100000);
  EXPECT_NEAR(mean_power(air.transmit(quiet)), 0.2, 0.005);
  EXPECT_NEAR(mean_power(air.idle(100000)), 0.2, 0.005);
}

TEST(ChannelTest, GivesTheSameSamplesForTheSameSeed) {
  channel_params params = {.snr_db = 10, .delay_spread_ns = 200};
  channel first(params, SAMPLE_RATE, 42);
  channel second(params, SAMPLE_RATE, 42);
  channel third(params, SAMPLE_RATE, 43);
  samples frame = prbs_samples(1000);
  samples expected = first.transmit(frame);
  EXPECT_EQ(second.transmit(frame), expected);
  EXPECT_NE(third.transmit(frame), expected);
}

// Sends a lone impulse many times: what comes out is each frame's multipath,
// and on average each tap's power follows the exponential model.
TEST(ChannelTest, FollowsTheExponentialMultipathModel) {
  const double DELAY_SPREAD = 400;  // ns: 2 samples at 5 MHz
  const std::size_t TAPS = 21;      // 0 to 10 delay spreads
  const int FRAMES = 20000;
  channel air(channel_params{.delay_spread_ns = DELAY_SPREAD}, SAMPLE_RATE, 1);

  samples impulse(TAPS + 10);
  impulse[0] = 1;
  std::vector<double> powers(impulse.size());
  for (int frame = 0; frame < FRAMES; frame++) {
    samples received = air.transmit(impulse);
    for (std::size_t k = 0; k < received.size(); k++)
      powers[k] += std::norm(received[k]) / FRAMES;
  }

  double total = 0;
  for (std::size_t k = 0; k < TAPS; k++)
    total += std::exp(-0.5 * static_cast<double>(k));
  for (std::size_t k = 0; k < TAPS; k++) {
    double expected = std::exp(-0.5 * static_cast<double>(k)) / total;
    EXPECT_NEAR(powers[k], expected, 0.05 * expected) << "tap " << k;
  }
  for (std::size_t k = TAPS; k < powers.size(); k++)
    EXPECT_EQ(powers[k], 0) << "tap " << k;
}

TEST(ChannelTest, SendsTheEchoesAfterTheFrame) {
  channel air(channel_params{.delay_spread_ns = 400}, SAMPLE_RATE, 1);
  samples impulse(1);
  impulse[0] = 1;
  ASSERT_NE(air.transmit(impulse)[0], std::complex<double>(0));
  samples echoes = air.idle(25);
  EXPECT_NE(echoes[19], std::complex<double>(0));
  EXPECT_EQ(echoes[20], std::complex<double>(0));
}
}  // namespace
}  // namespace fun

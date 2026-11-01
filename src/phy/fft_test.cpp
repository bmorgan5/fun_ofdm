#include "fft.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "test_util.h"

namespace fun {
namespace {
// Returns count complex values from PRBS-15, with real and imaginary parts
// between -1 and 1.
std::vector<std::complex<double>> prbs_values(std::size_t count,
                                              std::size_t offset) {
  std::vector<std::uint8_t> bytes = test::prbs_bytes(2 * count, offset);
  std::vector<std::complex<double>> values(count);
  for (std::size_t x = 0; x < count; x++)
    values[x] = {bytes[2 * x] / 127.5 - 1, bytes[2 * x + 1] / 127.5 - 1};
  return values;
}

// The DFT, straight from its definition: O(N^2), and unscaled.
std::vector<std::complex<double>> naive_forward(
    const std::complex<double>* samples) {
  std::vector<std::complex<double>> subcarriers(64);
  for (int k = -32; k < 32; k++) {
    for (int n = 0; n < 64; n++) {
      subcarriers[k + 32] +=
          samples[n] * std::polar(1.0, -2 * std::numbers::pi * k * n / 64);
    }
  }
  return subcarriers;
}

// The inverse DFT, straight from its definition, scaled by 1/64.
std::vector<std::complex<double>> naive_inverse(
    const std::complex<double>* subcarriers) {
  std::vector<std::complex<double>> samples(64);
  for (int n = 0; n < 64; n++) {
    for (int k = -32; k < 32; k++) {
      samples[n] += subcarriers[k + 32] *
                    std::polar(1.0 / 64, 2 * std::numbers::pi * k * n / 64);
    }
  }
  return samples;
}

double energy(const std::complex<double>* data, int count) {
  double sum = 0;
  for (int x = 0; x < count; x++) sum += std::norm(data[x]);
  return sum;
}

// The FFT works on subcarriers in order from -32 to 31, so subcarrier k is at
// index k + 32.
TEST(FftTest, InverseOfASubcarrierIsATone) {
  fft f(64);
  for (int k : {-26, -1, 1, 7, 26}) {
    std::vector<std::complex<double>> data(64);
    data[k + 32] = 1;
    f.inverse(data);
    for (int n = 0; n < 64; n++) {
      std::complex<double> expected =
          std::polar(1.0 / 64, 2 * std::numbers::pi * k * n / 64);
      EXPECT_NEAR(std::abs(data[n] - expected), 0, 1e-12)
          << "subcarrier " << k << ", sample " << n;
    }
  }
}

TEST(FftTest, ForwardUndoesInverse) {
  fft f(64);
  int symbols = 3;
  std::vector<std::complex<double>> freq(64 * symbols);
  for (std::size_t x = 0; x < freq.size(); x++)
    freq[x] = {std::sin(x * 1.3), std::cos(x * 0.7)};
  std::vector<std::complex<double>> data = freq;
  f.inverse(data);
  for (int s = 0; s < symbols; s++) {
    f.forward(&data[64 * s]);
    for (int x = 0; x < 64; x++) {
      EXPECT_NEAR(std::abs(data[64 * s + x] - freq[64 * s + x]), 0, 1e-12)
          << "symbol " << s << ", subcarrier " << x - 32;
    }
  }
}

TEST(FftTest, ForwardMatchesTheDft) {
  fft f(64);
  for (int symbol = 0; symbol < 4; symbol++) {
    std::vector<std::complex<double>> samples = prbs_values(64, 1024 * symbol);
    std::vector<std::complex<double>> expected = naive_forward(samples.data());
    f.forward(samples.data());
    for (int x = 0; x < 64; x++) {
      EXPECT_NEAR(std::abs(samples[x] - expected[x]), 0, 1e-9)
          << "symbol " << symbol << ", subcarrier " << x - 32;
    }
  }
}

// inverse() runs on every symbol in the vector.
TEST(FftTest, InverseMatchesTheDft) {
  fft f(64);
  int symbols = 4;
  std::vector<std::complex<double>> data = prbs_values(64 * symbols, 0);
  std::vector<std::complex<double>> freq = data;
  f.inverse(data);
  for (int s = 0; s < symbols; s++) {
    std::vector<std::complex<double>> expected = naive_inverse(&freq[64 * s]);
    for (int x = 0; x < 64; x++) {
      EXPECT_NEAR(std::abs(data[64 * s + x] - expected[x]), 0, 1e-12)
          << "symbol " << s << ", sample " << x;
    }
  }
}

// Parseval's theorem: the transform keeps the energy, apart from its scaling.
// With forward() unscaled and inverse() scaled by 1/64, the time samples have
// 1/64 of the subcarriers' energy.
TEST(FftTest, KeepsTheEnergy) {
  fft f(64);
  std::vector<std::complex<double>> samples = prbs_values(64, 0);
  double time_energy = energy(samples.data(), 64);
  f.forward(samples.data());
  EXPECT_NEAR(energy(samples.data(), 64) / 64, time_energy, 1e-9);

  std::vector<std::complex<double>> freq = prbs_values(64, 1024);
  double freq_energy = energy(freq.data(), 64);
  f.inverse(freq);
  EXPECT_NEAR(energy(freq.data(), 64), freq_energy / 64, 1e-12);
}
}  // namespace
}  // namespace fun

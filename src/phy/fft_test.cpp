#include "fft.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace fun {
namespace {
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
  for (int x = 0; x < freq.size(); x++)
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
}  // namespace
}  // namespace fun

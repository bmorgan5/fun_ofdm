#include "symbol_mapper.h"

#include <gtest/gtest.h>

#include <complex>
#include <cstddef>
#include <vector>

namespace fun {
namespace {
// Index into a 64-subcarrier symbol for subcarrier -32 to 31.
int index(int subcarrier) { return subcarrier + 32; }

// 48 data points per symbol, all different so they can be traced.
std::vector<std::complex<double>> numbered_data(int symbols) {
  std::vector<std::complex<double>> data(48 * symbols);
  for (std::size_t x = 0; x < data.size(); x++) data[x] = {double(x + 1), 0.5};
  return data;
}

// 802.11a-1999 section 17.3.5.9: the pilot polarity sequence is the scrambler
// output with the all-ones initial state, with 0 sent as +1 and 1 as -1.
std::vector<double> standard_polarity() {
  std::vector<double> polarity;
  int state = 0x7F;
  for (int n = 0; n < 127; n++) {
    int bit = ((state >> 6) ^ (state >> 3)) & 1;
    state = ((state << 1) | bit) & 0x7F;
    polarity.push_back(bit ? -1 : 1);
  }
  return polarity;
}

TEST(SymbolMapperTest, DemapUndoesMap) {
  symbol_mapper mapper;
  std::vector<std::complex<double>> data = numbered_data(5);
  std::vector<std::complex<double>> mapped = mapper.map(data);
  ASSERT_EQ(mapped.size(), 64u * 5);
  EXPECT_EQ(mapper.demap(mapped), data);
}

// 802.11a-1999 section 17.3.5.9: subcarriers -26 to 26 are used, except DC.
// The pilots are at -21, -7, 7 and 21, and the data fills the other 48 in
// order.
TEST(SymbolMapperTest, UsesTheStandardSubcarriers) {
  std::vector<std::complex<double>> mapped =
      symbol_mapper().map(numbered_data(1));
  int data_index = 0;
  for (int sc = -32; sc < 32; sc++) {
    std::complex<double> value = mapped[index(sc)];
    if (sc < -26 || sc > 26 || sc == 0) {
      EXPECT_EQ(value, std::complex<double>(0, 0)) << "subcarrier " << sc;
    } else if (sc == -21 || sc == -7 || sc == 7 || sc == 21) {
      EXPECT_EQ(std::abs(value), 1) << "subcarrier " << sc;
    } else {
      EXPECT_EQ(value, std::complex<double>(++data_index, 0.5))
          << "subcarrier " << sc;
    }
  }
  EXPECT_EQ(data_index, 48);
}

// The pilots for symbol n are (1, 1, 1, -1) * p_n, with p_0 on the first symbol
// (the SIGNAL field), and the sequence repeats every 127 symbols.
TEST(SymbolMapperTest, UsesTheStandardPilotPolarity) {
  std::vector<double> polarity = standard_polarity();
  std::size_t symbols = 130;
  std::vector<std::complex<double>> mapped =
      symbol_mapper().map(numbered_data(symbols));
  for (std::size_t n = 0; n < symbols; n++) {
    double p = polarity[n % 127];
    const std::complex<double>* symbol = &mapped[64 * n];
    EXPECT_EQ(symbol[index(-21)], std::complex<double>(p, 0)) << "symbol " << n;
    EXPECT_EQ(symbol[index(-7)], std::complex<double>(p, 0)) << "symbol " << n;
    EXPECT_EQ(symbol[index(7)], std::complex<double>(p, 0)) << "symbol " << n;
    EXPECT_EQ(symbol[index(21)], std::complex<double>(-p, 0)) << "symbol " << n;
  }
}
}  // namespace
}  // namespace fun

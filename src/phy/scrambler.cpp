/*! \file scrambler.cpp
 *  \brief C++ file for the scrambler class.
 */

#include "scrambler.h"

#include <cstdint>

namespace fun {
std::vector<std::uint8_t> scrambler::scramble(std::vector<std::uint8_t> data) {
  // Bit 6 of state is x7, and bit 0 is x1.
  int state = SEED, feedback = 0;
  for (std::uint8_t& byte : data) {
    feedback = (!!(state & 64)) ^ (!!(state & 8));
    byte ^= feedback;
    state = ((state << 1) & 0x7E) | feedback;
  }
  return data;
}
}  // namespace fun

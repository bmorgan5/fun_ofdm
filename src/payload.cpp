/*! \file payload.cpp
 * \brief C++ file for the sources of the data that fun-ofdm sends.
 */

#include "payload.h"

#include <algorithm>

namespace fun {
std::uint64_t random_seed() {
  std::random_device device;
  return std::uint64_t{device()} << 32 | device();
}

random_source::random_source(std::optional<std::uint64_t> limit,
                             std::uint64_t seed)
    : m_remaining(limit), m_random(seed) {}

std::size_t random_source::read(std::uint8_t* buffer, std::size_t max_bytes) {
  std::size_t count = max_bytes;
  if (m_remaining) count = std::min<std::uint64_t>(count, *m_remaining);

  // Fill 8 bytes at a time from the 64-bit generator, least significant byte
  // first. Shifting, unlike copying the number's bytes, doesn't depend on the
  // machine's byte order.
  for (std::size_t i = 0; i < count; i += 8) {
    std::uint64_t value = m_random();
    for (std::size_t j = i; j < std::min(i + 8, count); j++) {
      buffer[j] = static_cast<std::uint8_t>(value);
      value >>= 8;
    }
  }

  if (m_remaining) *m_remaining -= count;
  return count;
}
}  // namespace fun

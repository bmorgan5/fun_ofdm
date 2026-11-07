/*! \file stats.cpp
 *  \brief C++ file for counting the packets that are sent or received.
 */

#include "stats.h"

#include <iomanip>
#include <iostream>

namespace fun {
transfer_stats::transfer_stats() : m_start(std::chrono::steady_clock::now()) {}

void transfer_stats::add_packet(std::size_t bytes) {
  m_packets++;
  m_bytes += bytes;
}

std::size_t transfer_stats::packets() const { return m_packets; }

transfer_totals transfer_stats::totals() const {
  std::chrono::duration<double> elapsed =
      std::chrono::steady_clock::now() - m_start;
  return {.packets = m_packets, .bytes = m_bytes, .seconds = elapsed.count()};
}

/*!
 * Prints a line like "Sent 1000 packets (1500000 bytes) in 12.345 s, 972.1
 * kbit/s".
 */
void transfer_totals::print(const std::string& verb) const {
  std::cout << verb << " " << packets << " packets (" << bytes << " bytes) in "
            << std::fixed << std::setprecision(3) << seconds << " s";

  if (seconds > 0) {
    double bits_per_second = bytes * 8 / seconds;
    std::cout << ", " << std::setprecision(1);
    if (bits_per_second >= 1e6)
      std::cout << bits_per_second / 1e6 << " Mbit/s";
    else
      std::cout << bits_per_second / 1e3 << " kbit/s";
  }

  std::cout << std::defaultfloat << std::endl;
}

}  // namespace fun

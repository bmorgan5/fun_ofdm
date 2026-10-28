/*! \file stats.cpp
 *  \brief C++ file for counting the packets that are sent or received.
 */

#include <iomanip>
#include <iostream>
#include "stats.h"

namespace fun
{
    transfer_stats::transfer_stats() :
        m_start(std::chrono::steady_clock::now())
    {
    }

    void transfer_stats::add_packet(std::size_t bytes)
    {
        m_packets++;
        m_bytes += bytes;
    }

    std::uint64_t transfer_stats::packets() const
    {
        return m_packets;
    }

    /*!
     * Prints a line like "Sent 1000 packets (1500000 bytes) in 12.345 s, 972.1 kbit/s".
     */
    void transfer_stats::print_summary(const std::string & verb) const
    {
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();

        std::cout << verb << " " << m_packets << " packets (" << m_bytes << " bytes) in "
                  << std::fixed << std::setprecision(3) << seconds << " s";

        if(seconds > 0)
        {
            double bits_per_second = m_bytes * 8 / seconds;
            std::cout << ", " << std::setprecision(1);
            if(bits_per_second >= 1e6) std::cout << bits_per_second / 1e6 << " Mbit/s";
            else std::cout << bits_per_second / 1e3 << " kbit/s";
        }

        std::cout << std::defaultfloat << std::endl;
    }
}

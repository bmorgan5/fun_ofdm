/*! \file stats.h
 *  \brief Header file for counting the packets that are sent or received.
 */

#ifndef STATS_H
#define STATS_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fun
{
    /*!
     * \brief Counts the packets and bytes sent or received since it was created and
     * prints a summary of them.
     */
    class transfer_stats
    {
    public:

        /*!
         * \brief Starts the clock for the elapsed time.
         */
        transfer_stats();

        /*!
         * \brief Counts one packet.
         * \param bytes The size of the packet's payload.
         */
        void add_packet(std::size_t bytes);

        /*!
         * \brief The number of packets counted so far.
         */
        std::uint64_t packets() const;

        /*!
         * \brief Prints the packet and byte counts, the elapsed time and the throughput.
         * \param verb What happened to the packets, e.g. "Sent" or "Received".
         */
        void print_summary(const std::string & verb) const;

    private:

        std::chrono::steady_clock::time_point m_start; //!< When counting started

        std::uint64_t m_packets = 0; //!< Packets counted

        std::uint64_t m_bytes = 0; //!< Payload bytes counted
    };
}

#endif // STATS_H

/*! \file main.cpp
 *  \brief Entry point for the fun-ofdm binary.
 *
 *  Parses the command line and runs the selected mode: transmitting frames over the air
 *  (tx), receiving them (rx), or building frames and running them through the receive
 *  chain in software (sim).
 */

#include <pthread.h>
#include <signal.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>
#include <CLI/CLI.hpp>
#include "frame_builder.h"
#include "framing.h"
#include "options.h"
#include "payload.h"
#include "realtime.h"
#include "receiver.h"
#include "receiver_chain.h"
#include "stats.h"
#include "transmitter.h"

using namespace fun;

/*!
 * \brief Prints how many packets were lost and duplicated.
 * \param received The number of packets received, not counting duplicates
 * \param lost The number of packets lost
 * \param duplicates The number of duplicate packets received
 */
static void print_losses(std::uint64_t received, std::uint64_t lost, std::uint64_t duplicates)
{
    std::uint64_t total = received + lost;
    double percent = total ? 100.0 * lost / total : 0;
    std::cout << "Lost " << lost << " packets (" << std::fixed << std::setprecision(1) << percent << "%), "
              << duplicates << " duplicates" << std::defaultfloat << std::endl;
}

/*!
 * \brief Transmits data over the air with a USRP.
 * \param opts The tx options
 * \param stop Stops transmitting after the current packet
 * \return Process exit code.
 *
 *  This function sends random data, split into frames with sequence numbers so that the
 *  receiver can tell which frames it lost. It sends the amount of data set by --packets or
 *  --bytes, or keeps sending until a stop is requested.
 */
static int run_tx(const tx_options & opts, std::stop_token stop)
{
    if(!opts.radio.no_realtime) set_realtime_priority();

    transmitter tx = transmitter(to_usrp_params(opts));

    random_source source(opts.data.limit());
    fragmenter frames(source);

    std::cout << "Sending at " << RateParams(opts.phy_rate).name << " (press Ctrl+C to stop)" << std::endl;
    transfer_stats stats;
    for(std::uint64_t sequence = 0; !stop.stop_requested(); sequence++)
    {
        std::vector<unsigned char> payload = frames.next_frame();
        if(payload.empty()) break;

        tx.send_frame(payload, opts.phy_rate);
        stats.add_packet(payload.size() - FRAME_HEADER_SIZE);
        std::cout << "Sent packet " << sequence << " (" << payload.size() - FRAME_HEADER_SIZE << " bytes)" << std::endl;
    }

    stats.print_summary("Sent");

    return 0;
}

/*!
 * \brief What the receiver's callback keeps track of.
 */
struct rx_progress
{
    transfer_stats stats;  //!< Counts the received packets
    reassembler frames;    //!< Checks the received packets' sequence numbers
};

/*!
 * The receiver's callback is a plain function pointer, so it finds what it keeps track of through
 * a global. It is created when the receiver starts running.
 */
static std::optional<rx_progress> rx_state;

/*!
 * \brief The callback function for processing the Received Packets
 * \param packets The successfully received packets in from the receiver_chain
 *
 *  This function counts the received packets, skipping duplicates, and prints each one's
 *  sequence number.
 */
static void process_packets_callback(std::vector<std::vector<unsigned char> > packets)
{
    for(const auto & payload : packets)
    {
        std::optional<received_frame> frame = rx_state->frames.add(payload);
        if(!frame) continue;

        rx_state->stats.add_packet(frame->data.size());
        std::cout << "Received packet " << frame->sequence << " (" << frame->data.size() << " bytes)" << std::endl;
    }
}

/*!
 * \brief Receives frames over the air with a USRP.
 * \param opts The rx options
 * \param stop Stops receiving
 * \return Process exit code.
 *
 *  This function creates a receiver object, passing it a pointer to the #process_packets_callback()
 *  function which is used to process the received packets. It then runs the receiver until a stop
 *  is requested.
 */
static int run_rx(const rx_options & opts, std::stop_token stop)
{
    if(!opts.radio.no_realtime) set_realtime_priority();

    receiver rx = receiver(&process_packets_callback, to_usrp_params(opts));

    std::cout << "Receiving (press Ctrl+C to stop)" << std::endl;
    rx_state.emplace();
    rx.run(stop);

    rx_state->stats.print_summary("Received");
    print_losses(rx_state->stats.packets(), rx_state->frames.lost(), rx_state->frames.duplicates());

    return 0;
}

/*!
 * \brief Counts the bytes in received data that don't match the data that was sent.
 * \param sent The data that was sent
 * \param received The data that was received
 * \return The number of mismatched bytes, counting any difference in length as mismatched.
 */
static std::uint64_t count_mismatches(const std::vector<unsigned char> & sent, const std::vector<unsigned char> & received)
{
    std::size_t common = std::min(sent.size(), received.size());
    std::uint64_t mismatches = std::max(sent.size(), received.size()) - common;
    for(std::size_t i = 0; i < common; i++)
    {
        if(sent[i] != received[i]) mismatches++;
    }
    return mismatches;
}

/*!
 * \brief Builds frames and runs them through the receiver chain in software.
 * \param opts The sim options
 * \param stop Stops the simulation after the current chunk of samples
 * \return Process exit code: 0 if all the data was received intact or the simulation was
 *  stopped, otherwise 1.
 *
 *  This function sends random data, split into frames with sequence numbers, by building each
 *  frame's samples with the frame builder and passing them straight to the receiver chain. It
 *  then checks the data received against the data sent. This function does NOT use the
 *  transmitter and receiver classes.
 */
static int run_sim(const sim_options & opts, std::stop_token stop)
{
    const std::size_t CHUNK_SIZE = 4096;    // Samples passed to the receiver chain at a time
    const std::size_t FLUSH_SIZE = 40960;   // Zeros sent at the end to flush the receiver chain

    std::optional<std::uint64_t> limit = opts.data.limit();
    random_source source(limit ? *limit : SIM_DEFAULT_PACKETS * FRAME_DATA_SIZE);
    fragmenter frames(source);

    frame_builder builder;
    receiver_chain chain;
    reassembler received;
    std::vector<std::vector<unsigned char> > sent; // Each frame's data, by sequence number
    std::uint64_t mismatches = 0;
    transfer_stats stats;

    // Runs samples through the receiver chain and checks the frames that come out of it
    auto receive = [&](const std::vector<std::complex<double> > & samples)
    {
        for(std::size_t start = 0; start < samples.size() && !stop.stop_requested(); start += CHUNK_SIZE)
        {
            std::size_t end = std::min(start + CHUNK_SIZE, samples.size());
            std::vector<std::complex<double> > chunk(samples.begin() + start, samples.begin() + end);

            for(const auto & payload : chain.process_samples(chunk))
            {
                std::optional<received_frame> frame = received.add(payload);
                if(!frame) continue;

                stats.add_packet(frame->data.size());
                if(frame->sequence < sent.size()) mismatches += count_mismatches(sent[frame->sequence], frame->data);
                else mismatches += frame->data.size();
            }
        }
    };

    std::cout << "Simulating at " << RateParams(opts.phy_rate).name << std::endl;
    while(!stop.stop_requested())
    {
        std::vector<unsigned char> payload = frames.next_frame();
        if(payload.empty()) break;

        sent.emplace_back(payload.begin() + FRAME_HEADER_SIZE, payload.end());
        receive(builder.build_frame(payload, opts.phy_rate));
    }
    receive(std::vector<std::complex<double> >(FLUSH_SIZE));

    stats.print_summary("Received");

    if(stop.stop_requested())
    {
        std::cout << "Stopped early, so not checking the received data" << std::endl;
        return 0;
    }

    std::uint64_t lost = sent.size() - stats.packets();
    print_losses(stats.packets(), lost, received.duplicates());

    if(lost == 0 && mismatches == 0)
    {
        std::cout << "PASS: all " << sent.size() << " packets received intact" << std::endl;
        return 0;
    }

    std::cout << "FAIL: " << lost << " packets lost, " << mismatches << " bytes mismatched" << std::endl;
    return 1;
}

/*!
 * \brief Requests a stop on a stop source when the program receives SIGINT (Ctrl+C) or SIGTERM.
 * \param stop The stop source that the first signal stops
 * \return The thread that waits for the signals. Destroying it stops and joins the thread.
 *
 *  This blocks both signals in the calling thread, and so in every thread it creates afterwards,
 *  so call it before any other threads are started. The returned thread waits for the signals
 *  with sigwait(). The first signal requests a stop on \p stop, so the program can finish what it
 *  is doing and shut down cleanly. A second signal exits the program immediately, in case
 *  shutting down hangs.
 *
 *  Waiting with sigwait() instead of installing a signal handler means the stop is requested
 *  from an ordinary thread. std::stop_source::request_stop() isn't safe to call from a signal
 *  handler.
 */
static std::jthread stop_on_signals(std::stop_source stop)
{
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    return std::jthread([signals, stop](std::stop_token thread_stop) mutable
    {
        // The jthread's destructor requests a stop on this thread. Wake the thread up from
        // sigwait() then by sending it one of the signals it waits for.
        pthread_t self = pthread_self();
        std::stop_callback wake(thread_stop, [self] { pthread_kill(self, SIGTERM); });

        while(1)
        {
            int signal;
            if(sigwait(&signals, &signal) != 0) continue;

            if(thread_stop.stop_requested()) return;

            if(!stop.stop_requested())
            {
                std::cerr << std::endl << "Stopping... (press Ctrl+C again to quit immediately)" << std::endl;
                stop.request_stop();
            }
            else
            {
                // Exit with the shell's usual status for a process killed by a signal
                std::_Exit(128 + signal);
            }
        }
    });
}

int main(int argc, char * argv[])
{
    CLI::App app("802.11a OFDM PHY for Ettus USRPs", "fun-ofdm");
    app.set_version_flag("--version", "2.0.0"); // Keep in step with MODULE.bazel
    app.require_subcommand(1);

    tx_options tx_opts;
    CLI::App * tx = app.add_subcommand("tx", "Transmit test frames");
    add_options(tx, tx_opts);

    rx_options rx_opts;
    CLI::App * rx = app.add_subcommand("rx", "Receive frames");
    add_options(rx, rx_opts);

    sim_options sim_opts;
    CLI::App * sim = app.add_subcommand("sim", "Run frames through the receiver chain in software (no USRP)");
    add_options(sim, sim_opts);

    CLI11_PARSE(app, argc, argv);

    std::stop_source stop;

    // Ctrl+C or SIGTERM requests a stop. This blocks those signals, so call it before any
    // other threads start; they inherit the blocked signals.
    std::jthread signal_waiter = stop_on_signals(stop);

    try
    {
        if(tx->parsed()) return run_tx(tx_opts, stop.get_token());
        if(rx->parsed()) return run_rx(rx_opts, stop.get_token());
        if(sim->parsed()) return run_sim(sim_opts, stop.get_token());
    }
    catch(const std::exception & e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 1;
}

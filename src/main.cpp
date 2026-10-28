/*! \file main.cpp
 *  \brief Entry point for the fun-ofdm binary.
 *
 *  Parses the command line and runs the selected mode: transmitting frames over the air
 *  (tx), receiving them (rx), or building frames and running them through the receive
 *  chain in software (sim).
 */

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stop_token>
#include <thread>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <CLI/CLI.hpp>
#include "frame_builder.h"
#include "options.h"
#include "realtime.h"
#include "receiver.h"
#include "receiver_chain.h"
#include "stats.h"
#include "transmitter.h"

using namespace fun;

/*!
 * \brief Transmits test frames over the air with a USRP.
 * \param opts The tx options
 * \param stop Stops transmitting after the current packet
 * \return Process exit code.
 *
 *  This function transmits 1000 packets. The data in the transmitted packets
 *  is mostly random except for the first, middle, and last 100 bytes which are a
 *  known string. This string is used to verify that the packet was in fact received
 *  along with the IEEE CRC-32 check.
 */
static int run_tx(const tx_options & opts, std::stop_token stop)
{
    Rate phy_rate = opts.phy_rate;

    if(!opts.radio.no_realtime) set_realtime_priority();

    std::cout << "Testing transmit chain..." << std::endl;

    srand(time(NULL)); //Initialize random seed

    transmitter tx = transmitter(to_usrp_params(opts));
    std::string known_string("This known string is used to verify the correctness of the received data along with the IEEE CRC-32!");

    int num_packets = 1000;
    int packet_length = 1500;
    std::vector<std::vector<unsigned char> > packets(num_packets, std::vector<unsigned char>(packet_length));

    // Build all the packets
    for(int i = 0; i < num_packets; i++)
    {
        //Insert the known string in the beginning, middle, and end with random data inbetween
        memcpy(&packets[i][0], &known_string[0], known_string.length());
        for(int j = 100; j < 1000; j++) packets[i][j] = (unsigned char) rand() % 256;
        memcpy(&packets[i][1000], &known_string[0], known_string.length());
        for(int j = 1100; j < 1400; j++) packets[i][j] = (unsigned char) rand() % 256;
        memcpy(&packets[i][1400], &known_string[0], known_string.length());
    }

    //Transmit all the packets
    std::string tx_phy_rate = RateParams(phy_rate).name;
    transfer_stats stats;
    for(int i = 0; i < num_packets && !stop.stop_requested(); i++)
    {
        std::cout << "Sending packet " << i + 1 << " of " << num_packets << " at " << tx_phy_rate << std::endl;
        tx.send_frame(packets[i], phy_rate);
        stats.add_packet(packets[i].size());
    }

    stats.print_summary("Sent");

    return 0;
}

/*!
 * Counts the received packets. The receiver's callback is a plain function pointer, so the
 * callback finds this through a global. It is created when the receiver starts running.
 */
static std::optional<transfer_stats> rx_stats;

/*!
 * \brief The callback function for processing the Received Packets
 * \param packets The successfully received packets in from the receiver_chain
 *
 *  This function merely counts the number of received packets and prints the timestamps
 *  of when the packets were received.
 */
static void process_packets_callback(std::vector<std::vector<unsigned char> > packets)
{
    for(const auto & packet : packets) rx_stats->add_packet(packet.size());

    boost::posix_time::ptime rx_time = boost::posix_time::microsec_clock::local_time();
    if(packets.size() > 0)
    {
        std::cout << "Received " << rx_stats->packets() << " packets at " << rx_time.time_of_day() << std::endl;
    }
}

/*!
 * \brief Receives frames over the air with a USRP.
 * \param opts The rx options
 * \param stop Stops receiving
 * \return Process exit code.
 *
 *  This function creates a receiver object, passing it a pointer to the #process_packets_callback()
 *  function which is used to process the received packets (in this case it just counts them).
 *  It then runs the receiver until a stop is requested.
 */
static int run_rx(const rx_options & opts, std::stop_token stop)
{
    std::cout << "Testing receive chain..." << std::endl;

    if(!opts.radio.no_realtime) set_realtime_priority();

    // Instantiate a usrp
    printf("Instantiating the usrp.\n");

    receiver rx = receiver(&process_packets_callback, to_usrp_params(opts));

    std::cout << "Receiving (press Ctrl+C to stop)" << std::endl;
    rx_stats.emplace();
    rx.run(stop);

    rx_stats->print_summary("Received");

    return 0;
}

/*!
 * \brief Builds frames and runs them through the receiver chain in software.
 * \param opts The sim options
 * \param stop Stops the simulation after the current chunk of samples
 * \return Process exit code.
 *
 *  This function builds some packets using the frame builder and sends them through
 *  the receiver chain.  This function does NOT use the transmitter and receiver classes.
 */
static int run_sim(const sim_options & opts, std::stop_token stop)
{
    Rate phy_rate = opts.phy_rate;

    std::cout << "Running Simulation..." << std::endl;

    frame_builder * fb = new frame_builder();
    receiver_chain * receiver = new receiver_chain();

    std::string data("I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!");
    int repeat = 15;

    std::vector<unsigned char> payload(data.length()*repeat); //Payload = 1500 bytes
    for(int x = 0; x < repeat; x++) memcpy(&payload[x*data.length()], &data[0], data.length());

    // Build a frame
    std::vector<std::complex<double>> samples = fb->build_frame(payload, phy_rate);

    int pad_length = samples.size()*1000;

    // Concatenate num_frames frames together
    int num_frames = 100;
    std::cout << "Transmitting " << num_frames << " frames" << std::endl;
    std::vector<std::complex<double>> samples_con(samples.size() * num_frames + pad_length);
    for(int x = 0; x < num_frames; x++)
    {
        memcpy(&samples_con[x*samples.size()], &samples[0], samples.size() * sizeof(std::complex<double>));
    }

    //Pad the end with 0's to flush receive chain
    std::vector<std::complex<double> > zeros(pad_length);
    memcpy(&samples_con[num_frames*samples.size()], &zeros[0], zeros.size()*sizeof(std::complex<double>));

    // Run the samples through the receiver chain

    int chunk_size = 4096;

    transfer_stats stats;
    for(int x = 0; x < samples_con.size() && !stop.stop_requested(); x += chunk_size)
    {
        int start = x;
        int end = x + chunk_size;
        if(end > samples_con.size()) end = samples_con.size();
        std::vector<std::complex<double> > chunk(&samples_con[start], &samples_con[end]);

        std::vector<std::vector<unsigned char> > rec_frames = receiver->process_samples(chunk);

        if(rec_frames.size()){
            for(int i = 0; i < rec_frames.size(); i++){
                stats.add_packet(rec_frames[i].size());
                for(int j = 0; j < rec_frames[i].size(); j++)
                    std::cout << rec_frames[i][j];
                std::cout << std::endl << std::endl;
            }
        }
    }

    stats.print_summary("Received");

    delete receiver;
    delete fb;

    return 0;
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

/*! \file main.cpp
 *  \brief Entry point for the fun-ofdm binary.
 *
 *  Parses the command line and runs the selected mode: transmitting frames over
 * the air (tx), receiving them (rx), or building frames and running them
 * through the receive chain in software (sim).
 */

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

#include <CLI/CLI.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "realtime.h"
#include "receiver.h"
#include "src/phy/frame_builder.h"
#include "src/phy/receiver_chain.h"
#include "transmitter.h"

using namespace fun;

/*!
 * \brief Transmits test frames over the air with a USRP.
 * \return Process exit code.
 *
 *  This function transmits 1000 packets. The data in the transmitted packets
 *  is mostly random except for the first, middle, and last 100 bytes which are
 * a known string. This string is used to verify that the packet was in fact
 * received along with the IEEE CRC-32 check.
 */
static int run_tx() {
  double freq = 5.72e9;
  double sample_rate = 5e6;
  double tx_gain = 30;
  double amp = 0.5;
  Rate phy_rate = RATE_1_2_BPSK;

  set_realtime_priority();

  std::cout << "Testing transmit chain..." << std::endl;

  std::srand(std::time(nullptr));  // Initialize random seed

  transmitter tx = transmitter(
      {.freq = freq, .rate = sample_rate, .tx_gain = tx_gain, .tx_amp = amp});
  std::string known_string(
      "This known string is used to verify the correctness of the received "
      "data along with the IEEE CRC-32!");

  std::size_t num_packets = 1000;
  std::size_t packet_length = 1500;
  std::vector<std::vector<std::uint8_t>> packets(
      num_packets, std::vector<std::uint8_t>(packet_length));

  // Build all the packets
  for (std::size_t i = 0; i < num_packets; i++) {
    // Insert the known string in the beginning, middle, and end with random
    // data inbetween
    std::memcpy(&packets[i][0], &known_string[0], known_string.length());
    for (std::size_t j = 100; j < 1000; j++) packets[i][j] = std::rand() % 256;
    std::memcpy(&packets[i][1000], &known_string[0], known_string.length());
    for (std::size_t j = 1100; j < 1400; j++) packets[i][j] = std::rand() % 256;
    std::memcpy(&packets[i][1400], &known_string[0], known_string.length());
  }

  // Transmit all the packets
  std::string tx_phy_rate = RateParams(phy_rate).name;
  for (std::size_t i = 0; i < num_packets; i++) {
    std::cout << "Sending packet " << i + 1 << " of " << num_packets << " at "
              << tx_phy_rate << std::endl;
    tx.send_frame(packets[i], phy_rate);
  }

  return 0;
}

static int rx_count =
    0;  //!< Total number of packets received since the receiver was activated

/*!
 * \brief The callback function for processing the Received Packets
 * \param packets The successfully received packets in from the receiver_chain
 *
 *  This function merely counts the number of received packets and prints the
 * timestamps of when the packets were received.
 */
static void process_packets_callback(
    std::vector<std::vector<std::uint8_t>> packets) {
  rx_count += packets.size();

  boost::posix_time::ptime rx_time =
      boost::posix_time::microsec_clock::local_time();
  if (packets.size() > 0) {
    std::cout << "Received " << rx_count << " packets at "
              << rx_time.time_of_day() << std::endl;
  }
}

/*!
 * \brief Receives frames over the air with a USRP.
 * \return Process exit code.
 *
 *  This function creates a receiver object which spawns its own thread for the
 * receiver chain. It passes a pointer to the  #process_packets_callback()
 * function which is used to process the received packets (in this case it just
 * counts them). This function then sleeps forever until the user kills the
 * program externally (preferably by using something like Ctrl+c or the kill
 * command in the shell).
 */
static int run_rx() {
  double freq = 5.72e9;
  double sample_rate = 5e6;
  double rx_gain = 30;

  std::cout << "Testing receive chain..." << std::endl;

  set_realtime_priority();

  // Instantiate a usrp
  std::printf("Instantiating the usrp.\n");

  receiver rx =
      receiver(&process_packets_callback,
               {.freq = freq, .rate = sample_rate, .rx_gain = rx_gain});

  while (1) sleep(1);
}

/*!
 * \brief Builds frames and runs them through the receiver chain in software.
 * \return Process exit code.
 *
 *  This function builds some packets using the frame builder and sends them
 * through the receiver chain.  This function does NOT use the transmitter and
 * receiver classes.
 */
static int run_sim() {
  Rate phy_rate = RATE_3_4_QAM16;

  std::cout << "Running Simulation..." << std::endl;

  frame_builder* fb = new frame_builder();
  receiver_chain* receiver = new receiver_chain();

  std::string data(
      "I'm a little tea pot, short and stout.....here is my handle.....blah "
      "blah blah.....this rhyme sucks!");
  std::size_t repeat = 15;

  std::vector<std::uint8_t> payload(data.length() *
                                    repeat);  // Payload = 1500 bytes
  for (std::size_t x = 0; x < repeat; x++)
    std::memcpy(&payload[x * data.length()], &data[0], data.length());

  // Build a frame
  std::vector<std::complex<double>> samples =
      fb->build_frame(payload, phy_rate);

  int pad_length = samples.size() * 1000;

  // Concatenate num_frames frames together
  std::size_t num_frames = 100;
  std::cout << "Transmitting " << num_frames << " frames" << std::endl;
  std::vector<std::complex<double>> samples_con(samples.size() * num_frames +
                                                pad_length);
  for (std::size_t x = 0; x < num_frames; x++) {
    std::memcpy(&samples_con[x * samples.size()], &samples[0],
                samples.size() * sizeof(std::complex<double>));
  }

  // Pad the end with 0's to flush receive chain
  std::vector<std::complex<double>> zeros(pad_length);
  std::memcpy(&samples_con[num_frames * samples.size()], &zeros[0],
              zeros.size() * sizeof(std::complex<double>));

  boost::posix_time::ptime start =
      boost::posix_time::microsec_clock::local_time();

  // Run the samples through the receiver chain

  int chunk_size = 4096;

  int count = 0;
  for (std::size_t x = 0; x < samples_con.size(); x += chunk_size) {
    std::size_t start = x;
    std::size_t end = x + chunk_size;
    if (end > samples_con.size()) end = samples_con.size();
    std::vector<std::complex<double>> chunk(&samples_con[start],
                                            &samples_con[end]);

    std::vector<std::vector<std::uint8_t>> rec_frames =
        receiver->process_samples(chunk);
    count += rec_frames.size();

    if (rec_frames.size()) {
      for (std::size_t i = 0; i < rec_frames.size(); i++) {
        for (std::size_t j = 0; j < rec_frames[i].size(); j++)
          std::cout << rec_frames[i][j];
        std::cout << std::endl << std::endl;
      }
    }
  }

  boost::posix_time::time_duration elapsed =
      boost::posix_time::microsec_clock::local_time() - start;

  std::printf("Received %i packets\n", count);

  std::printf("Time elapsed: %f\n", elapsed.total_microseconds() / 1000.0);

  return 0;
}

int main(int argc, char* argv[]) {
  CLI::App app("802.11a OFDM PHY for Ettus USRPs", "fun-ofdm");
  app.require_subcommand(1);

  CLI::App* tx = app.add_subcommand("tx", "Transmit test frames");
  CLI::App* rx = app.add_subcommand("rx", "Receive frames");
  CLI::App* sim = app.add_subcommand(
      "sim", "Run frames through the receiver chain in software (no USRP)");

  CLI11_PARSE(app, argc, argv);

  if (tx->parsed()) return run_tx();
  if (rx->parsed()) return run_rx();
  if (sim->parsed()) return run_sim();
  return 1;
}

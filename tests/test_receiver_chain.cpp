/*! \file test_sim.cpp
 *  \brief Simulates the building of packets and sending them through the receive chain.
 *
 *  This file is used to simulate building packets with the frame_builder class and then
 *  sending them through the receive chain.
 */

#include "gtest/gtest.h"

#include <iostream>
#include <random>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>

#include "ppdu.h"
#include "preamble.h"
#include "rates.h"
#include "usrp.h"
#include "frame_builder.h"
#include "receiver_chain.h"


using namespace fun;

class fun_ofdm_test : public ::testing::TestWithParam<Rate> {
  public:

    fun_ofdm_test() :
        // First 72 characters of Ode to Joy
        t_ode_to_joy("Joy, bright spark of divinity, Daughter of Elysium, Fire-insired we trea"),
        t_ode_to_joy_payload(t_ode_to_joy.begin(), t_ode_to_joy.end()),
        // Exactly 100 characters
        t_tea_pot("I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!"),
        t_tea_pot_payload(t_tea_pot.begin(), t_tea_pot.end())
    {}

    frame_builder fb;
    receiver_chain receiver;
    std::string t_ode_to_joy;
    std::vector<unsigned char> t_ode_to_joy_payload;

    std::string t_tea_pot;
    std::vector<unsigned char> t_tea_pot_payload;

    // double signal_power(std::vector<std::complex<double>> &signal) {
    //     // ASSERT_GT(signal.size(), 0);
    //     double power = 0.0;
    //     for(int i = 0; i < signal.size(); i++) {
    //         power += std::norm(signal[i]);
    //     }
    //     power /= signal.size();
    //     return power;
    // }

    // void add_awgn(std::vector<std::complex<double>> &signal, double SNR) {
    //     double sig_pwr = signal_power(signal);
    //     double noise_std_dev = std::sqrt(sig_pwr) / 2.0;
    //     auto dist = std::bind(std::normal_distribution<double>{0.0, noise_std_dev}, std::default_random_engine{});
    //     for(size_t i = 0; i < signal.size(); i++) {
    //         signal[i] += std::complex<double>(dist(), dist());
    //     }
    // }
};

// TEST_P(fun_ofdm_test, tx_rx_serial) {
//     Rate rate = GetParam();
//     frame_detector rx_frame_detector;
//     timing_sync rx_timing_sync;
//     fft_symbols rx_fft_symbols;
//     channel_est rx_channel_est;
//     phase_tracker rx_phase_tracker;
//     frame_decoder rx_frame_decoder;

//     size_t pad_length = 1000;
//     auto frame = fb.build_frame(t_ode_to_joy_payload, rate);
//     std::vector<std::complex<double>> signal(frame.size() + 2 * pad_length, std::complex<double>(0.0, 0.0));
//     signal.insert(signal.begin()+pad_length, frame.begin(), frame.end());

//     rx_frame_detector.input_buffer.swap(signal);
//     rx_frame_detector.work();
//     rx_timing_sync.input_buffer.swap(rx_frame_detector.output_buffer);
//     rx_timing_sync.work();
//     rx_fft_symbols.input_buffer.swap(rx_timing_sync.output_buffer);
//     rx_fft_symbols.work();
//     rx_channel_est.input_buffer.swap(rx_fft_symbols.output_buffer);
//     rx_channel_est.work();
//     rx_phase_tracker.input_buffer.swap(rx_channel_est.output_buffer);
//     rx_phase_tracker.work();
//     rx_frame_decoder.input_buffer.swap(rx_phase_tracker.output_buffer);
//     ASSERT_EQ(1, rx_frame_decoder.output_buffer.size());

//     auto rx_t_ode_to_joy_payload = rx_frame_decoder.output_buffer[0];
//     std::string rx_msg(rx_t_ode_to_joy_payload.begin(), rx_t_ode_to_joy_payload.end());
//     EXPECT_EQ(t_ode_to_joy, rx_msg);
// }
// "Joy, bright spark of divinity, Daughter of Elysium, Fire-insired we tread"
// "Thy sanctuary."
// "Thy magic power re-unites"
// "All that custom has divided,"
// "All men become brothers"
// "Under the sway of thy gentle wings..."
// TODO: This is currently failing. Comment it back in when it's not

// TEST_P(fun_ofdm_test, perfect_channel) {
//     Rate rate = GetParam();
//     RateParams params(rate);

//     size_t pad_length = 100000;
//     size_t num_rx_packets = 0;
//     std::vector<std::complex<double>> frame = fb.build_frame(t_ode_to_joy_payload, rate);

//     // TODO: Refactor this so that passing data through teh pipeline is it's own function
//     // and then pass a function pointer / lambda function / anonymous function with the "noise"
//     // for(size_t snr = 1; snr < 20; snr++) {
//     //     add_awgn(frame, snr);
//     // }

//     std::vector<std::complex<double>> signal(frame.size() + 2*pad_length, std::complex<double>(0.0,0.0));
//     signal.insert(signal.begin()+pad_length, frame.begin(), frame.end());
//     auto packets = receiver.process_samples(frame);
//     EXPECT_GE(packets.size(), 1) << "Expected to receive at least one frame with " << params.name << " encoding";
//     for(auto &packet : packets) {
//         bool decoded = packet == t_ode_to_joy_payload;
//         if(decoded) {
//             std::string rx_msg(packet.begin(), packet.end());
//             std::cout << rx_msg << std::endl;
//             num_rx_packets++;
//             EXPECT_EQ(rx_msg, t_ode_to_joy);
//         }
//         EXPECT_TRUE(decoded);
//     }

//     // EXPECT_EQ(num_rx_packets, num_tx_packets);
// }

// const size_t NUM_HEADER_SAMPLES = 48;

// TEST_P(fun_ofdm_test, encode_decode) {
//     Rate rate = GetParam();
//     RateParams rate_params(rate);

//     ppdu tx_ppdu(t_ode_to_joy_payload, rate);
//     std::vector<std::complex<double>> samples = tx_ppdu.encode();

//     ppdu rx_ppdu;
//     bool decoded_header = rx_ppdu.decode_header(std::vector<std::complex<double>>(samples.begin(), samples.begin()+NUM_HEADER_SAMPLES));

//     EXPECT_TRUE(decoded_header);
//     EXPECT_EQ(tx_ppdu.header, rx_ppdu.header);

//     bool decoded_t_ode_to_joy_payload = rx_ppdu.decode_data(std::vector<std::complex<double>>(samples.begin()+NUM_HEADER_SAMPLES, samples.end()));
//     EXPECT_TRUE(decoded_t_ode_to_joy_payload);
//     EXPECT_EQ(t_ode_to_joy_payload, rx_ppdu.t_ode_to_joy_payload);
// }

// TEST_P(fun_ofdm_test, mod_demod) {
//     Rate rate = GetParam();
//     frame_builder fb;
//     auto frame = fb.build_frame(t_ode_to_joy_payload, rate);
// }


/*!
 *  This function builds some packets using the frame builder and sends them through
 *  the receiver chain.  This function does NOT use the transmitter and receiver classes.
*/
TEST_P(fun_ofdm_test, simple_sim) {
    Rate phy_rate = GetParam();
    RateParams params(phy_rate);
    size_t repeat = 15;

    t_ode_to_joy_payload.resize(t_ode_to_joy.size()*repeat); // ~1500 bytes
    for(size_t i = 0; i < repeat; i++) {
        t_ode_to_joy_payload.insert(t_ode_to_joy_payload.begin() + i*t_ode_to_joy.length(), t_ode_to_joy.begin(), t_ode_to_joy.end());
    }

    // Build a frame
    std::vector<std::complex<double>> tx_frame = fb.build_frame(t_ode_to_joy_payload, phy_rate);

    // int pad_length = tx_frame.size()*1000;

    // TODO: Test how changing these affects the receiver
    // Pad the end with 0's to flush receive chain
    size_t padding = 1000;
    // size_t padding = 0;
    // Space the frames out a bit
    // size_t frame_buffer = 0;
    size_t frame_buffer = 1000;

    // Concatenate num_frames frames together
    // size_t num_frames = 10;
    size_t num_frames = 10;
    size_t sample_length = ((tx_frame.size() + frame_buffer) * num_frames) + padding;
    // std::vector<std::complex<double>> tx_samples_con(tx_samples.size() * num_frames + pad_length);
    std::vector<std::complex<double>> tx_samples(sample_length, std::complex<double>(0.0));
    for(size_t i = 0; i < num_frames; i++) {
        tx_samples.insert(tx_samples.begin() + (i * tx_frame.size()) + (frame_buffer/2), tx_frame.begin(), tx_frame.end());
    }

    // std::vector<std::complex<double> > zeros(pad_length);
    // memcpy(&tx_samples_con[num_frames*tx_samples.size()], &zeros[0], zeros.size()*sizeof(std::complex<double>));

    // Run the samples through the receiver chain
    size_t chunk_size = 4096;
    size_t rx_count = 0;
    for(size_t i = 0; i < tx_samples.size(); i += chunk_size) {
        // size_t start = i;
        // size_t end = i + chunk_size;
        auto start = tx_samples.begin() + i;
        auto end = tx_samples.begin() + i + chunk_size;
        if(end > tx_samples.end()) end = tx_samples.end();
        std::vector<std::complex<double>> chunk(start, end);
        // std::vector<std::complex<double> > chunk(&samples_con[start], &samples_con[end]);
        // std::cout << "Processing samples from " << i << " to " << i + chunk_size << std::endl;
        std::vector<std::vector<unsigned char>> rec_frames = receiver.process_samples(chunk);
        rx_count += rec_frames.size();

        for(size_t j = 0; j < rec_frames.size(); j++) {
            EXPECT_EQ(t_ode_to_joy_payload, rec_frames[j]);
        }
    }

    // boost::posix_time::time_duration elapsed = boost::posix_time::microsec_clock::local_time() - start;

    // printf("Received %u packets\n", rx_count);

    // printf("Time elapsed: %f\n", elapsed.total_microseconds() / 1000.0);

    EXPECT_EQ(num_frames, rx_count) << "Failed to receive all the frames with " << params.name << " encoding";

}

// INSTANTIATE_TEST_CASE_P(DISABLED_modulations, fun_ofdm_test, ::testing::Values(
INSTANTIATE_TEST_CASE_P(modulations, fun_ofdm_test, ::testing::Values(
        RATE_1_2_BPSK,
        RATE_2_3_BPSK,
        RATE_3_4_BPSK,
        RATE_1_2_QPSK,
        RATE_2_3_QPSK,
        RATE_3_4_QPSK,
        RATE_1_2_QAM16,
        RATE_2_3_QAM16,
        RATE_3_4_QAM16,
        RATE_2_3_QAM64,
        RATE_3_4_QAM64
    )
);
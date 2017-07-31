/*! \file test_over_the_air.cpp
 *  \brief C++ file for running an over the air hardware-in-the-loop test
 *
 *  This block is in charge of using the two LTS symbols to align the received frame in time.
 *  It also uses the two LTS symbols to perform an initial frequency offset estimation and
 *  applying the necessary correction.
 */

#include "gtest/gtest.h"

#include <random>

#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <uhd/utils/thread_priority.hpp>

#include "rates.h"
#include "receiver.h"
#include "transmitter.h"

using namespace fun;

class test_over_the_air: public ::testing::TestWithParam<Rate> {
  public:

    test_over_the_air() :
        t_packet_length(1472),
        t_tx_packets(
            std::vector<std::vector<unsigned char>>(
                1000,
                std::vector<unsigned char>(
                    t_packet_length
                )
            )
        ),
        t_receiver(std::bind(
            &test_over_the_air::rx_packets_callback,
            this,
            std::placeholders::_1)
        )
    {
        // t_tx_packets(std::vector<unsigned char>(t_packet_length,0),t_num_tx_packets);
        auto dist = std::bind(std::uniform_int_distribution<unsigned char>{0, 255}, std::default_random_engine{});
        std::string known_string("This known string is used to verify the correctness of the received data along with the IEEE CRC-32!"); // length 100

        // TODO: Figure out how to make sure these values are legit or the test could crash
        // ASSERT_LT(known_string.length(), t_packet_length) << "known string won't fit inside a packet";

        // Build all the packets
        for(size_t i = 0; i < t_tx_packets.size(); i++)
        {
            //Insert the known string shifted right by one character each time
            size_t known_start = i % (t_packet_length - known_string.length());
            memcpy(&t_tx_packets[i][known_start], &known_string[0], known_string.length());
        }

    }

    void rx_packets_callback(std::vector<std::vector<unsigned char>> packets) {
        for(int i = 0; i < packets.size(); i++) {
            this->t_rx_packets.push_back(packets[i]);
        }
    }

    // This order needs to be the same as in the initializer list above
    size_t t_packet_length;
    std::vector<std::vector<unsigned char>> t_tx_packets;
    std::vector<std::vector<unsigned char>> t_rx_packets;

    transmitter t_transmitter;
    receiver t_receiver;
};

TEST_P(test_over_the_air, usrp_n210) {
    ASSERT_NO_THROW(uhd::set_thread_priority()) << "Unable to set thread priority to realtime...did you forget sudo?" << std::endl;
    Rate rate = GetParam();
    RateParams phy_rate(rate);

    std::cout << "Transmitting " << t_tx_packets.size() << " packets at " << phy_rate.name << "...";

    t_receiver.run();

    boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();
    for(int i = 0; i < t_tx_packets.size() ; i++) {
        t_transmitter.send_frame(t_tx_packets[i], phy_rate.rate);

        // TODO: Possibly put random space inbetween these frames to show that it still works
        //       Want to show that continuous streams as well as individual packets work
    }
    boost::posix_time::time_duration elapsed = boost::posix_time::microsec_clock::local_time() - start;
    std::cout << "..." << elapsed.total_milliseconds() << "ms" << std::endl;

    // Let the receiver chain flush
    boost::this_thread::sleep(boost::posix_time::seconds(1));

    // This needs to be called before to clean up the 
    // t_receiver.halt();

    size_t num_rx_success = 0;
    size_t num_rx_corrupt = 0;

    for(size_t i = 0; i < t_rx_packets.size(); i++) {
        bool found = false;
        for(size_t j = 0; j < t_tx_packets.size(); j++) {
            if(t_rx_packets[i] == t_tx_packets[j]) {
                found = true;
                num_rx_success++;
            }
        }
        if(!found) {
            num_rx_corrupt++;
        }
    }

    size_t ninety_percent_of_num_tx = (t_tx_packets.size() * 9) / 10;
    size_t num_rx = t_rx_packets.size();

    EXPECT_GE(num_rx, ninety_percent_of_num_tx);

    std::cout << "Received " << num_rx << " packets at " << phy_rate.name;
    if(num_rx_corrupt > 0) {
        std::cout << "..." << num_rx_corrupt << " corrupt";
    }
    std::cout << std::endl << std::endl;

}

// INSTANTIATE_TEST_CASE_P(DISABLED_modulations, fun_ofdm_test, ::testing::Values(
INSTANTIATE_TEST_CASE_P(modulations, test_over_the_air, ::testing::Values(
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
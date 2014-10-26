/*! \file test_tx.cpp
 *  \brief Transmitter test
 *
 *  This file is used to test transmitting OFDM PHY frames over the air.
 */

#include "stdio.h"
#include "stdlib.h"
#include "time.h"
#include <iostream>
#include "transmitter.h"

using namespace fun;

void test_tx(double freq, double sample_rate, double tx_gain, double amp, Rate phy_rate);
bool set_realtime_priority();

double freq = 5.72e9;
double sample_rate = 5e6;
double tx_gain = 30;
//double rx_gain = 30;
double amp = 0.5;
Rate phy_rate = RATE_1_2_BPSK;

int main(int argc, char * argv[]){

    set_realtime_priority();

	std::cout << "Testing transmit chain..." << std::endl;
    test_tx(freq, sample_rate, tx_gain, amp, phy_rate);

	return 0;
}

/*!
 * \brief Function for testing the fun_ofdm transmitter
 * \param freq Center Frequency
 * \param sample_rate Sample Rate
 * \param tx_gain Transmitter Gain
 * \param amp Transmit Amplitude
 * \param phy_rate The PHY Rate used for all packets in this tx test
 *
 *  This function transmits 1000 packets. The data in the transmitted packets
 *  is mostly random except for the first, middle, and last 100 bytes which are a
 *  known string. This string is used to verify that the packet was in fact received
 *  along with the IEEE CRC-32 check.
 */
void test_tx(double freq, double sample_rate, double tx_gain, double amp, Rate phy_rate)
{
    srand(time(NULL)); //Initialize random seed

    transmitter tx = transmitter(freq, sample_rate, tx_gain, amp);
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
    for(int i = 0; i < num_packets; i++)
    {
        std::cout << "Sending packet " << i + 1 << " of " << num_packets << " at " << tx_phy_rate << std::endl;
        tx.send_frame(packets[i], phy_rate);
    }

}

/*!
 * \brief Attempt to set real time priority for thread scheduling
 * \return Whether or not real time priority was succesfully set.
 */
bool set_realtime_priority()
{
    // Get the current thread
    pthread_t this_thread = pthread_self();

    // Set priority to SCHED_FIFO
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_RR);
    if (pthread_setschedparam(this_thread, SCHED_RR, &params) != 0)
    {
        std::cout << "Unable to set realtime priority. Did you forget to sudo?" << std::endl;
        return false;
    }

    return true;
}





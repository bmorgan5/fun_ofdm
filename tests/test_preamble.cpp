#include "gtest/gtest.h"
#include "test_utils.h"

#include <cmath>
#include <complex>
#include <vector>

#include "frame_detector.h"
#include "rates.h"
#include "preamble.h"
#include "timing_sync.h"

using namespace fun;

typedef double SNR;

#define TEST_SAMPLE_LEN 4096

class test_preamble: public ::testing::TestWithParam<SNR> {
public:
    test_preamble() :
        t_samples(TEST_SAMPLE_LEN,std::complex<double>(0.0,0.0))
    {
        add_awgn(t_samples, 0.01);
        // insert_preamble_at(1024); // Allow for some padding at the begging
        // insert_preamble_at(1703);
        // insert_preamble_at(2224);
        insert_preamble_at(1000);
        insert_preamble_at(2000);
        insert_preamble_at(3000);
   }

    std::vector<std::complex<double>> t_samples;
    std::vector<int> t_preamble_starts;
    std::vector<int> t_lts_start; // The pairs are the indicies of LTS1 and LTS2 respectively
    frame_detector t_frame_detector;
    timing_sync t_timing_sync;

    private:
        void insert_preamble_at(size_t index) {
            ASSERT_LT(index, t_samples.size() - PREAMBLE_LEN) << "Not enough room in t_samples to insert a PREAMBLE";

            t_preamble_starts.push_back(index);
            int lts1 = index + (PREAMBLE_LEN/2) + (LTS_LENGTH/2); 
            t_lts_start.push_back(lts1);
            t_samples.insert(t_samples.begin() + index, &PREAMBLE_SAMPLES[0], &PREAMBLE_SAMPLES[PREAMBLE_LEN]);
        }

};

TEST_P(test_preamble, sync_start)
{
    // TODO: Figure out if this works
    // SNR snr = GetParam();
    // add_awgn(t_samples, snr);


    std::vector<tagged_sample> input(t_samples.size());
    size_t p = 0; // Tracks t_preamble_starts
    for(size_t i = 0; i < t_samples.size(); i++) {
        input[i].sample = t_samples[i];
        if(i == t_preamble_starts[p]) {
            size_t start = i;
            size_t end = i + PREAMBLE_LEN/2;
            input[start].tag = STS_START;
            input[end].tag = STS_END;
            p++;
        }
    }

    t_timing_sync.input_buffer = input;
    t_timing_sync.work();
    std::vector<tagged_sample> output(t_timing_sync.output_buffer);

    std::vector<int> found_lts;
    for(int i = 0; i < output.size(); i++) {
        if(output[i].tag == LTS1) {
            found_lts.push_back(i);
        }
    }

    ASSERT_EQ(t_lts_start.size(), found_lts.size()) << "Failed to find the same number of LTS's that we inserted";
    for(int i = 0; i < t_lts_start.size(); i++) {
        int offset = CARRYOVER_LENGTH;
        int known_lts_start = t_lts_start[i] + CARRYOVER_LENGTH; //Normalize because timing_sync adds carryover to the start
        int found_lts_start = found_lts[i];
        // The 16 sample window lets the start be set somewhere in the cyclic prefix
        EXPECT_GE(found_lts_start, known_lts_start - 16) << "LTS1 was set too soon before the frame actually started";
        EXPECT_LE(found_lts_start, known_lts_start) << "LTS1 was set after the frame actually started";
    }

}

TEST_P(test_preamble, detect_start)
{
    // Add some noise
    SNR snr = GetParam();
    add_awgn(t_samples, snr);

    t_frame_detector.input_buffer = t_samples;
    t_frame_detector.work();
    std::vector<tagged_sample> output(t_frame_detector.output_buffer);

    int p_index = 0; // Keeps track of which preamble_start we are looking for
    bool found_sts = false;
    for(int i = 0; i < output.size(); i++) {
        if(output[i].tag == STS_START) {
            int preamble_start = t_preamble_starts[p_index];
            int preamble_end = t_preamble_starts[p_index] + (PREAMBLE_LEN/2) + (LTS_LENGTH/2);

            int dif = i - preamble_start;
            // std::cout << "Preamble start at " << preamble_start << " detected at " << i << " (" << dif << " samples later)" << std::endl;
            // The STS_START needs to be tagged before the full LTS starts

            EXPECT_GE(i, preamble_start) << "Found STS_START before preamble started";
            EXPECT_LE(i, preamble_end) << "Found STS_START too late";
            if(preamble_start <= i && i <= preamble_end) {
                EXPECT_FALSE(found_sts) << "Duplicate STS_START for preamble starting at " << preamble_start << " (" << i - preamble_start << " samples later)";
                if(!found_sts) p_index++;
                found_sts = true;
            }
        } else if(output[i].tag == STS_END) {
            // std::cout << "Preamble end at " << i << std::endl;
            found_sts = false;
        }
    }

    EXPECT_EQ(t_preamble_starts.size(), p_index) << "Failed to find all the preamble starts";
}

INSTANTIATE_TEST_CASE_P(preamble_snrs, test_preamble, ::testing::Values(
        0.0
        // 10.0 This fails atm
    )
);

TEST(DISABLED_math, norm_and_abs) {
    // std::vector<unsigned char> t_ode_to_joy(
    //     "Joy, bright spark of divinity, Daughter of Elysium, Fire-insired we tread"
    //     "Thy sanctuary."
    //     "Thy magic power re-unites"
    //     "All that custom has divided,"
    //     "All men become brothers"
    //     "Under the sway of thy gentle wings..."
    // );


    double norm = 0.0;
    double absolute = 0.0;

    norm = std::norm(PREAMBLE_SAMPLES[0]);
    absolute = std::abs(PREAMBLE_SAMPLES[0]);

    // std::cout << "Norm: " << norm << std::endl;
    // std::cout << "Abs:  " << absolute << std::endl;

    // std::cout << "---------------" << std::endl;

    norm = 0.0;
    absolute = 0.0;

    int num_samples = 160;
    for(int i = 0; i < num_samples; i++) {
        norm += std::norm(PREAMBLE_SAMPLES[i]);
        absolute += std::abs(PREAMBLE_SAMPLES[i]);
    }
    norm /= num_samples;
    absolute /= num_samples;

    // std::cout << "Norm: " << norm << std::endl;
    // std::cout << "Abs:  " << absolute << std::endl;

    // std::cout << "---------------" << std::endl;

    std::complex<double> a(2.0, 2.0);
    std::cout << "Norm(a): " << std::norm(a) << std::endl;
    std::cout << "Abs(a):  " << std::abs(a) << std::endl;

    // std::cout << "---------------" << std::endl;

    std::complex<double> b = a*std::conj(a);
    std::cout << b << std::endl;

    std::cout << "Norm(b): " << std::norm(b) << std::endl;
    std::cout << "Abs(b):  " << std::abs(b) << std::endl;


    EXPECT_DOUBLE_EQ(std::norm(a), std::abs(b));

}


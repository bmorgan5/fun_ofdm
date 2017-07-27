#include "gtest/gtest.h"
#include "frame_detector.h"
#include "rates.h"

using namespace fun;

class test_frame_detector : public ::testing::TestWithParam<Rate> {
public:
    test_frame_detector() {}

private:
    frame_detector t_frame_detector;
};

TEST_P(test_frame_detector, foo)
{
    EXPECT_EQ(1, 1);
}
// int main(int argc, char **argv)
// {
//     testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();

INSTANTIATE_TEST_CASE_P(modulations, test_frame_detector, ::testing::Values(
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
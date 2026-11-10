#include "workload.h"

#include <gtest/gtest.h>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "pipeline.h"
#include "src/framing.h"
#include "src/phy/receiver_chain.h"

namespace fun {
namespace {
using samples = std::vector<std::complex<double>>;

// A standard workload, shortened.
workload short_workload(const std::string& name, double seconds) {
  for (workload spec : standard_workloads()) {
    if (spec.name != name) continue;
    spec.seconds = seconds;
    return spec;
  }
  ADD_FAILURE() << "No workload " << name;
  return {};
}

// All of a source's samples, read size at a time.
samples read_all(workload_source& source, std::size_t size) {
  samples all;
  for (samples part = source.read(size); !part.empty();
       part = source.read(size)) {
    EXPECT_LE(part.size(), size);
    all.insert(all.end(), part.begin(), part.end());
  }
  return all;
}

double mean_power(const samples& x) {
  double power = 0;
  for (const auto& sample : x) power += std::norm(sample);
  return power / static_cast<double>(x.size());
}

TEST(WorkloadTest, HasTheStandardWorkloads) {
  std::vector<workload> workloads = standard_workloads();
  ASSERT_EQ(workloads.size(), 3u);

  EXPECT_EQ(workloads[0].name, "W-idle");
  EXPECT_EQ(workloads[0].rate, std::nullopt);
  EXPECT_EQ(workloads[0].snr_db, 20);

  EXPECT_EQ(workloads[1].name, "W-6");
  EXPECT_EQ(workloads[1].rate, RATE_1_2_BPSK);
  EXPECT_EQ(workloads[1].airtime, 0.5);
  EXPECT_EQ(workloads[1].snr_db, 20);

  EXPECT_EQ(workloads[2].name, "W-54");
  EXPECT_EQ(workloads[2].rate, RATE_3_4_QAM64);
  EXPECT_EQ(workloads[2].airtime, 0.9);
  EXPECT_EQ(workloads[2].snr_db, 30);

  for (const workload& spec : workloads) {
    EXPECT_EQ(spec.payload_bytes, 1000u) << spec.name;
    EXPECT_EQ(spec.samples(), 200000000u) << spec.name;  // 10 s at 20 MS/s
    EXPECT_EQ(spec.seed, WORKLOAD_SEED) << spec.name;
  }
}

TEST(WorkloadTest, GivesEverySampleOnceInReadsOfAnySize) {
  workload spec = short_workload("W-6", 0.003);
  ASSERT_EQ(spec.samples(), 60000u);
  workload_source whole(spec);
  samples expected = whole.read(spec.samples());
  ASSERT_EQ(expected.size(), spec.samples());
  EXPECT_TRUE(whole.read(1).empty());

  for (std::size_t size : {1, 4096, 30000, 1000000}) {
    workload_source source(spec);
    EXPECT_EQ(read_all(source, size), expected) << size << " at a time";
  }
}

TEST(WorkloadTest, GivesTheSameSamplesForTheSameSeed) {
  workload spec = short_workload("W-54", 0.001);
  workload_source first(spec);
  workload_source second(spec);
  samples expected = first.read(spec.samples());
  EXPECT_EQ(second.read(spec.samples()), expected);

  spec.seed++;
  workload_source other(spec);
  EXPECT_NE(other.read(spec.samples()), expected);
}

// A 1000-byte payload is 8054 data bits, with SERVICE, the CRC-32 and the
// tail (see src/phy/README.md): 336 symbols of 24 bits at 6 Mbit/s, and 38
// of 216 bits at 54 Mbit/s. Each frame also has the preamble and SIGNAL, 400
// samples.
TEST(WorkloadTest, SpacesTheFramesForTheAirtime) {
  workload_source w6(short_workload("W-6", 0.1));
  EXPECT_EQ(w6.frame_samples(), 400u + 80 * 336);
  EXPECT_EQ(w6.idle_samples(), w6.frame_samples());
  EXPECT_EQ(w6.whole_frames(), 2000000 / (2 * w6.frame_samples()));

  workload_source w54(short_workload("W-54", 0.1));
  EXPECT_EQ(w54.frame_samples(), 400u + 80 * 38);
  EXPECT_EQ(w54.idle_samples(), (w54.frame_samples() + 4) / 9);

  workload_source idle(short_workload("W-idle", 0.1));
  EXPECT_EQ(idle.frame_samples(), 0u);
  EXPECT_EQ(idle.whole_frames(), 0u);
}

TEST(WorkloadTest, IdlesAtTheNoiseFloorForTheSnr) {
  workload spec = short_workload("W-idle", 0.01);
  workload_source source(spec);
  EXPECT_NEAR(mean_power(source.read(spec.samples())) / OFDM_SIGNAL_POWER, 0.01,
              0.0005);  // 20 dB
}

TEST(WorkloadTest, StartsEachFrameAfterItsIdleTime) {
  workload spec = short_workload("W-6", 0.01);
  spec.snr_db = 300;  // No noise to speak of
  workload_source source(spec);
  samples all = source.read(spec.samples());
  std::size_t idle = source.idle_samples();
  EXPECT_LT(mean_power(samples(all.begin(), all.begin() + idle)), 1e-20);
  EXPECT_NEAR(mean_power(samples(all.begin() + idle,
                                 all.begin() + idle + source.frame_samples())) /
                  OFDM_SIGNAL_POWER,
              1, 0.05);
}

// The receiver gets every whole frame, in order, with its sequence number.
TEST(WorkloadTest, CarriesFramesTheReceiverDecodes) {
  for (const std::string& name : {"W-6", "W-54"}) {
    workload spec = short_workload(name, 0.02);
    workload_source source(spec);
    pipeline receiver;
    reassembler frames;
    std::uint64_t received = 0;
    // Then zeros, which take the last frames through the receiver
    for (int zeros = 0; zeros < 6;) {
      samples batch = source.read(4096);
      if (batch.empty()) {
        batch.resize(4096);
        zeros++;
      }
      for (const auto& payload : receiver.step(batch)) {
        std::optional<received_frame> frame = frames.add(payload);
        if (!frame) continue;
        EXPECT_EQ(frame->sequence, received) << name;
        EXPECT_EQ(frame->data.size(), spec.payload_bytes - FRAME_HEADER_SIZE);
        received++;
      }
    }
    EXPECT_GT(source.whole_frames(), 0u) << name;
    EXPECT_EQ(received, source.whole_frames()) << name;
  }
}

// So the blocks' benchmarks, which capture their inputs from a pipeline, get
// what they'd get in receiver_chain.
TEST(PipelineTest, DecodesWhatTheReceiverChainDecodes) {
  workload spec = short_workload("W-54", 0.02);
  workload_source source(spec);
  pipeline sequential;
  receiver_chain threaded;
  std::size_t payloads = 0;
  for (samples batch = source.read(4096); !batch.empty();
       batch = source.read(4096)) {
    std::vector<std::vector<std::uint8_t>> expected =
        threaded.process_samples(batch);
    EXPECT_EQ(sequential.step(batch), expected);
    payloads += expected.size();
  }
  EXPECT_GT(payloads, 0u);
}
}  // namespace
}  // namespace fun

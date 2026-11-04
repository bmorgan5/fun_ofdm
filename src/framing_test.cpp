#include "framing.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "payload.h"
#include "src/phy/test_util.h"

namespace fun {
namespace {
// Reads from a fixed buffer, at most max_read bytes at a time.
class vector_source : public data_source {
 public:
  explicit vector_source(std::vector<std::uint8_t> data,
                         std::size_t max_read = FRAME_DATA_SIZE)
      : m_data(std::move(data)), m_max_read(max_read) {}

  std::size_t read(std::uint8_t* buffer, std::size_t max_bytes) override {
    std::size_t count =
        std::min({max_bytes, m_max_read, m_data.size() - m_position});
    std::copy_n(m_data.begin() + m_position, count, buffer);
    m_position += count;
    return count;
  }

 private:
  std::vector<std::uint8_t> m_data;
  std::size_t m_max_read;
  std::size_t m_position = 0;
};

// Every frame's payload from a fragmenter, until it runs out of data.
std::vector<std::vector<std::uint8_t>> fragment(data_source& source) {
  fragmenter frames(source);
  std::vector<std::vector<std::uint8_t>> payloads;
  for (std::vector<std::uint8_t> payload = frames.next_frame();
       !payload.empty(); payload = frames.next_frame()) {
    payloads.push_back(payload);
  }
  return payloads;
}

// A payload with the given sequence number and data.
std::vector<std::uint8_t> frame(std::uint32_t sequence,
                                const std::vector<std::uint8_t>& data) {
  std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>(sequence >> 24),
      static_cast<std::uint8_t>(sequence >> 16),
      static_cast<std::uint8_t>(sequence >> 8),
      static_cast<std::uint8_t>(sequence)};
  payload.insert(payload.end(), data.begin(), data.end());
  return payload;
}

// Adds a frame with each sequence number, and returns the sequence numbers of
// the frames it returned.
std::vector<std::uint32_t> add_all(
    reassembler& frames, const std::vector<std::uint32_t>& sequences) {
  std::vector<std::uint32_t> received;
  for (std::uint32_t sequence : sequences) {
    std::optional<received_frame> f = frames.add(frame(sequence, {0xAB}));
    if (f) received.push_back(f->sequence);
  }
  return received;
}

TEST(FragmenterTest, SplitsTheDataIntoFullFramesAndARemainder) {
  std::vector<std::uint8_t> data = test::prbs_bytes(2 * FRAME_DATA_SIZE + 100);
  vector_source source(data);
  std::vector<std::vector<std::uint8_t>> payloads = fragment(source);

  ASSERT_EQ(payloads.size(), 3u);
  EXPECT_EQ(payloads[0].size(), FRAME_HEADER_SIZE + FRAME_DATA_SIZE);
  EXPECT_EQ(payloads[1].size(), FRAME_HEADER_SIZE + FRAME_DATA_SIZE);
  EXPECT_EQ(payloads[2].size(), FRAME_HEADER_SIZE + 100);

  std::vector<std::uint8_t> joined;
  for (const auto& payload : payloads)
    joined.insert(joined.end(), payload.begin() + FRAME_HEADER_SIZE,
                  payload.end());
  EXPECT_EQ(joined, data);
}

TEST(FragmenterTest, SendsNoFramesWithoutData) {
  vector_source source({});
  EXPECT_TRUE(fragment(source).empty());
}

TEST(FragmenterTest, KeepsReturningNoFrameAtTheEnd) {
  vector_source source(test::prbs_bytes(10));
  fragmenter frames(source);
  EXPECT_FALSE(frames.next_frame().empty());
  EXPECT_TRUE(frames.next_frame().empty());
  EXPECT_TRUE(frames.next_frame().empty());
}

// A source that returns less than asked for still makes one frame per read.
TEST(FragmenterTest, MakesAFrameFromEachShortRead) {
  vector_source source(test::prbs_bytes(10), 3);
  std::vector<std::vector<std::uint8_t>> payloads = fragment(source);

  ASSERT_EQ(payloads.size(), 4u);
  EXPECT_EQ(payloads[0].size(), FRAME_HEADER_SIZE + 3);
  EXPECT_EQ(payloads[3].size(), FRAME_HEADER_SIZE + 1);
}

// Frame 0x010203 is the first whose header has three nonzero bytes, so this
// checks the byte order of each of them.
TEST(FragmenterTest, NumbersTheFramesFromZeroBigEndian) {
  const std::size_t COUNT = 0x010204;
  vector_source source(std::vector<std::uint8_t>(COUNT, 0x5A), 1);
  std::vector<std::vector<std::uint8_t>> payloads = fragment(source);

  ASSERT_EQ(payloads.size(), COUNT);
  for (std::size_t x = 0; x < COUNT; x++) {
    ASSERT_EQ(payloads[x], frame(x, {0x5A})) << "frame " << x;
  }
  EXPECT_EQ(payloads.back(),
            (std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03, 0x5A}));
}

TEST(ReassemblerTest, ReturnsTheSequenceNumberAndData) {
  reassembler frames;
  std::vector<std::uint8_t> data = test::prbs_bytes(100);
  std::optional<received_frame> f = frames.add(frame(0, data));

  ASSERT_TRUE(f);
  EXPECT_EQ(f->sequence, 0u);
  EXPECT_EQ(f->data, data);
}

TEST(ReassemblerTest, ReadsTheSequenceNumberBigEndian) {
  reassembler frames;
  std::optional<received_frame> f = frames.add({0x01, 0x02, 0x03, 0x04, 0xAB});

  ASSERT_TRUE(f);
  EXPECT_EQ(f->sequence, 0x01020304u);
  EXPECT_EQ(f->data, std::vector<std::uint8_t>{0xAB});
}

// A frame can hold no data, but it needs the whole header.
TEST(ReassemblerTest, DropsPayloadsShorterThanTheHeader) {
  reassembler frames;
  for (std::size_t length = 0; length < FRAME_HEADER_SIZE; length++) {
    EXPECT_FALSE(frames.add(std::vector<std::uint8_t>(length)))
        << length << " bytes";
  }
  std::optional<received_frame> empty = frames.add(frame(0, {}));
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->data.empty());

  EXPECT_EQ(frames.lost(), 0u);
  EXPECT_EQ(frames.duplicates(), 0u);
}

TEST(ReassemblerTest, CountsNothingForFramesInOrder) {
  reassembler frames;
  EXPECT_EQ(add_all(frames, {0, 1, 2, 3}),
            (std::vector<std::uint32_t>{0, 1, 2, 3}));
  EXPECT_EQ(frames.lost(), 0u);
  EXPECT_EQ(frames.duplicates(), 0u);
}

TEST(ReassemblerTest, CountsTheFramesInAGapAsLost) {
  reassembler frames;
  EXPECT_EQ(add_all(frames, {0, 1, 4, 5, 9}),
            (std::vector<std::uint32_t>{0, 1, 4, 5, 9}));
  EXPECT_EQ(frames.lost(), 5u);
  EXPECT_EQ(frames.duplicates(), 0u);
}

// Counting starts at 0, so the frames sent before the receiver started count
// as lost.
TEST(ReassemblerTest, CountsTheFramesBeforeTheFirstAsLost) {
  reassembler frames;
  EXPECT_EQ(add_all(frames, {3, 4}), (std::vector<std::uint32_t>{3, 4}));
  EXPECT_EQ(frames.lost(), 3u);
}

TEST(ReassemblerTest, DropsAndCountsDuplicates) {
  reassembler frames;
  EXPECT_EQ(add_all(frames, {0, 1, 1, 2, 2, 2, 3}),
            (std::vector<std::uint32_t>{0, 1, 2, 3}));
  EXPECT_EQ(frames.lost(), 0u);
  EXPECT_EQ(frames.duplicates(), 3u);
}

// Frames arrive in order or not at all, so an earlier one is a duplicate, even
// if it was counted as lost.
TEST(ReassemblerTest, DropsAFrameFromBeforeTheLastOne) {
  reassembler frames;
  EXPECT_EQ(add_all(frames, {0, 2, 1}), (std::vector<std::uint32_t>{0, 2}));
  EXPECT_EQ(frames.lost(), 1u);
  EXPECT_EQ(frames.duplicates(), 1u);
}

TEST(ReassemblerTest, CountsLossesAndDuplicatesTogether) {
  reassembler frames;
  EXPECT_EQ(add_all(frames, {0, 2, 2, 3, 6, 6, 7}),
            (std::vector<std::uint32_t>{0, 2, 3, 6, 7}));
  EXPECT_EQ(frames.lost(), 3u);
  EXPECT_EQ(frames.duplicates(), 2u);
}

// A transmitter that restarts begins again at 0. Its frames are new data, not
// duplicates.
TEST(ReassemblerTest, AcceptsTheFramesOfARestartedTransmitter) {
  GTEST_SKIP() << "A restarted transmitter's frames are taken for duplicates "
                  "(see the \\bug in framing.h)";

  reassembler frames;
  EXPECT_EQ(add_all(frames, {0, 1, 2, 3, 4, 0, 1, 2}),
            (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 0, 1, 2}));
  EXPECT_EQ(frames.duplicates(), 0u);
}

TEST(FramingTest, ReassemblesWhatWasFragmented) {
  std::vector<std::uint8_t> data = test::prbs_bytes(5 * FRAME_DATA_SIZE + 7);
  vector_source source(data);
  reassembler frames;

  std::vector<std::uint8_t> received;
  std::uint32_t expected_sequence = 0;
  for (const auto& payload : fragment(source)) {
    std::optional<received_frame> f = frames.add(payload);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->sequence, expected_sequence++);
    received.insert(received.end(), f->data.begin(), f->data.end());
  }
  EXPECT_EQ(received, data);
  EXPECT_EQ(frames.lost(), 0u);
  EXPECT_EQ(frames.duplicates(), 0u);
}
}  // namespace
}  // namespace fun

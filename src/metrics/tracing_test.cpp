#include "tracing.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "metrics.h"

namespace fun::trace {
namespace {
std::string read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

// A trace is protobuf, where the names of events, threads and arguments are
// plain strings.
TEST(TracingTest, WritesTheEventsToTheFile) {
  if (!metrics::ENABLED) GTEST_SKIP() << "Built without metrics";
  std::filesystem::path path =
      std::filesystem::path(std::getenv("TEST_TMPDIR")) / "trace.pftrace";
  start_file(path.string());
  name_thread("test_thread");
  metrics::time_point start = metrics::now();
  slice("test_slice", start, start + 1000, {{"test_arg", 42}});
  instant("test_instant", start + 2000);
  stop();

  std::string trace = read(path);
  EXPECT_GT(trace.size(), 0u);
  for (const char* name :
       {"test_slice", "test_instant", "test_arg", "test_thread"})
    EXPECT_TRUE(trace.contains(name)) << name;
}

TEST(TracingTest, RecordsNothingWithoutASession) {
  if (!metrics::ENABLED) GTEST_SKIP() << "Built without metrics";
  std::filesystem::path path =
      std::filesystem::path(std::getenv("TEST_TMPDIR")) / "second.pftrace";
  metrics::time_point start = metrics::now();
  slice("before_the_session", start, start + 1000);
  start_file(path.string());
  stop();
  EXPECT_FALSE(read(path).contains("before_the_session"));
}

TEST(TracingTest, ThrowsIfTheFileCantBeOpened) {
  if (!metrics::ENABLED) GTEST_SKIP() << "Built without metrics";
  EXPECT_THROW(start_file("/nonexistent/dir/trace.pftrace"),
               std::runtime_error);
}
}  // namespace
}  // namespace fun::trace

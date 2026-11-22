#include "run_mode.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <stop_token>
#include <string>

#include <boost/json.hpp>

#include "options.h"
#include "summary.h"

namespace fun {
namespace {
namespace json = boost::json;

// Modes are plain functions, so the fake modes below talk to the tests
// through these.
std::string g_json_path;     // The --json file
std::string g_seen_in_mode;  // What the file held before the mode returned

std::string read_file(const std::string& path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), {}};
}

// The seed of the result in a summary, which tells the results apart.
std::uint64_t seed_of(const std::string& summary) {
  return json::parse(summary).at("seed").to_number<std::uint64_t>();
}

class RunModeTest : public testing::Test {
 protected:
  void SetUp() override {
    g_json_path =
        (std::filesystem::path(testing::TempDir()) /
         (std::string(
              testing::UnitTest::GetInstance()->current_test_info()->name()) +
          ".json"))
            .string();
    std::filesystem::remove(g_json_path);
    g_seen_in_mode.clear();
    opts.json_file = g_json_path;
  }

  sim_options opts;
  std::stop_source stop;
};

// Reports a result with seed 1, looks at the file, then returns one with
// seed 2: like a mode that reports, then closes its USRP.
sim_result reports_then_returns(const sim_options&, std::stop_token,
                                const reporter<sim_result>& report) {
  report(sim_result{.seed = 1});
  g_seen_in_mode = read_file(g_json_path);
  return sim_result{.seed = 2};
}

TEST_F(RunModeTest, WritesTheSummaryWhenTheModeReportsIt) {
  run_mode(&reports_then_returns, opts, stop.get_token(), "1.2.3");
  ASSERT_FALSE(g_seen_in_mode.empty());
  EXPECT_EQ(seed_of(g_seen_in_mode), 1u);
}

TEST_F(RunModeTest, WritesTheSummaryOnlyOnce) {
  run_mode(&reports_then_returns, opts, stop.get_token(), "1.2.3");
  std::string written = read_file(g_json_path);
  EXPECT_EQ(written, g_seen_in_mode);
  EXPECT_EQ(seed_of(written), 1u);
}

sim_result returns_without_reporting(const sim_options&, std::stop_token,
                                     const reporter<sim_result>&) {
  return sim_result{.seed = 2};
}

TEST_F(RunModeTest, WritesTheSummaryWhenAModeReturnsWithoutReporting) {
  run_mode(&returns_without_reporting, opts, stop.get_token(), "1.2.3");
  EXPECT_EQ(seed_of(read_file(g_json_path)), 2u);
}

// Like a mode whose USRP fails as it closes.
sim_result reports_then_throws(const sim_options&, std::stop_token,
                               const reporter<sim_result>& report) {
  report(sim_result{.seed = 1});
  throw std::runtime_error("The USRP didn't answer");
}

TEST_F(RunModeTest, KeepsTheSummaryIfTheModeThrowsAfterReporting) {
  EXPECT_THROW(run_mode(&reports_then_throws, opts, stop.get_token(), "1.2.3"),
               std::runtime_error);
  EXPECT_EQ(seed_of(read_file(g_json_path)), 1u);
}

sim_result throws(const sim_options&, std::stop_token,
                  const reporter<sim_result>&) {
  throw std::runtime_error("No USRP");
}

TEST_F(RunModeTest, LeavesTheFileEmptyIfTheModeThrowsBeforeReporting) {
  EXPECT_THROW(run_mode(&throws, opts, stop.get_token(), "1.2.3"),
               std::runtime_error);
  EXPECT_EQ(read_file(g_json_path), "");
}

TEST_F(RunModeTest, RejectsAFileItCantOpen) {
  opts.json_file = "/nonexistent/dir/summary.json";
  EXPECT_THROW(
      run_mode(&returns_without_reporting, opts, stop.get_token(), "1.2.3"),
      std::runtime_error);
}

TEST_F(RunModeTest, WritesNothingWithoutAFile) {
  opts.json_file.reset();
  run_mode(&reports_then_returns, opts, stop.get_token(), "1.2.3");
  EXPECT_FALSE(std::filesystem::exists(g_json_path));
}

sim_result fails_its_check(const sim_options&, std::stop_token,
                           const reporter<sim_result>&) {
  return sim_result{.check = sim_check{.lost = 1}};
}

TEST_F(RunModeTest, ReturnsTheResultsExitCode) {
  EXPECT_EQ(
      run_mode(&returns_without_reporting, opts, stop.get_token(), "1.2.3"), 0);
  EXPECT_EQ(run_mode(&fails_its_check, opts, stop.get_token(), "1.2.3"), 1);
}

// Prints, then reports.
sim_result prints_then_reports(const sim_options&, std::stop_token,
                               const reporter<sim_result>& report) {
  std::cout << "Simulating" << std::endl;
  report(sim_result{.seed = 1});
  return sim_result{.seed = 2};
}

TEST_F(RunModeTest, WritesOnlyTheSummaryToStdoutForADash) {
  opts.json_file = "-";
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  run_mode(&prints_then_reports, opts, stop.get_token(), "1.2.3");
  std::string out = testing::internal::GetCapturedStdout();
  std::string err = testing::internal::GetCapturedStderr();
  EXPECT_EQ(seed_of(out), 1u);
  EXPECT_EQ(err, "Simulating\n");
}
}  // namespace
}  // namespace fun

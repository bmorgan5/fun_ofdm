#include "host_info.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fun {
namespace {
// The start of /proc/cpuinfo on an x86 machine and on a Pi 5.
const char* X86_CPUINFO =
    "processor\t: 0\n"
    "vendor_id\t: GenuineIntel\n"
    "cpu family\t: 6\n"
    "model\t\t: 165\n"
    "model name\t: Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz\n"
    "cpu MHz\t\t: 3792.014\n";
const char* PI5_CPUINFO =
    "processor\t: 0\n"
    "BogoMIPS\t: 108.00\n"
    "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32\n"
    "CPU implementer\t: 0x41\n"
    "CPU architecture: 8\n"
    "CPU variant\t: 0x4\n"
    "CPU part\t: 0xd0b\n"
    "CPU revision\t: 1\n";

TEST(HostInfoTest, NamesAnX86Processor) {
  EXPECT_EQ(cpu_model_from_cpuinfo(X86_CPUINFO),
            "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz");
}

TEST(HostInfoTest, NamesAnArmCore) {
  EXPECT_EQ(cpu_model_from_cpuinfo(PI5_CPUINFO), "ARM Cortex-A76");
}

TEST(HostInfoTest, GivesTheNumbersOfAnUnknownArmCore) {
  EXPECT_EQ(
      cpu_model_from_cpuinfo("CPU implementer\t: 0x61\nCPU part\t: 0x022\n"),
      "CPU implementer 0x61, part 0x022");
}

TEST(HostInfoTest, NamesNoProcessorWithoutCpuinfo) {
  EXPECT_EQ(cpu_model_from_cpuinfo(""), "");
}

TEST(HostInfoTest, ReadsTheCpuQuota) {
  EXPECT_EQ(cpu_quota_from_cpu_max("400000 100000\n"), 4);
  EXPECT_EQ(cpu_quota_from_cpu_max("150000 100000\n"), 1.5);
  EXPECT_EQ(cpu_quota_from_cpu_max("max 100000\n"), std::nullopt);
  EXPECT_EQ(cpu_quota_from_cpu_max(""), std::nullopt);
  EXPECT_EQ(cpu_quota_from_cpu_max("abc 100000"), std::nullopt);
}

// Writes a file under the test's temporary directory, and its directories.
void write(const std::filesystem::path& root, const std::string& path,
           const std::string& text) {
  std::filesystem::path file = root / path;
  std::filesystem::create_directories(file.parent_path());
  std::ofstream(file) << text;
}

// A Pi 5 in a Docker container, two cgroups below a limit of 2 CPUs.
TEST(HostInfoTest, ReadsTheFilesUnderTheRoot) {
  std::filesystem::path root =
      std::filesystem::path(std::getenv("TEST_TMPDIR")) / "pi";
  write(root, "proc/cpuinfo", PI5_CPUINFO);
  write(root, "proc/self/cgroup", "0::/a/b\n");
  write(root, "sys/fs/cgroup/a/cpu.max", "200000 100000\n");
  write(root, "sys/fs/cgroup/a/b/cpu.max", "max 100000\n");
  write(root, "sys/firmware/devicetree/base/model",
        std::string("Raspberry Pi 5 Model B Rev 1.0", 30) + '\0');
  write(root, ".dockerenv", "");

  host_info host = read_host_info(root.string());
  EXPECT_EQ(host.cpu_model, "ARM Cortex-A76");
  EXPECT_EQ(host.board, "Raspberry Pi 5 Model B Rev 1.0");
  EXPECT_EQ(host.cpu_quota, 2);
  EXPECT_EQ(host.container, "docker");
  EXPECT_FALSE(host.arch.empty());
  EXPECT_GT(host.online_cpus, 0);
  EXPECT_GT(host.usable_cpus, 0);
}

TEST(HostInfoTest, FindsNothingInAnEmptyRoot) {
  std::filesystem::path root =
      std::filesystem::path(std::getenv("TEST_TMPDIR")) / "empty";
  std::filesystem::create_directories(root);
  host_info host = read_host_info(root.string());
  EXPECT_EQ(host.cpu_model, "");
  EXPECT_EQ(host.board, "");
  EXPECT_EQ(host.cpu_quota, std::nullopt);
  EXPECT_EQ(host.container, "");
}

TEST(HostInfoTest, WarnsOnlyAboutALimit) {
  host_info host = {.online_cpus = 16, .usable_cpus = 16};
  EXPECT_EQ(cpu_limit_warning(host), "");

  host.cpu_quota = 16;
  EXPECT_EQ(cpu_limit_warning(host), "");

  host.usable_cpus = 4;
  EXPECT_EQ(cpu_limit_warning(host),
            "This process may use only 4 of the 16 CPUs (CPU affinity, like "
            "Docker's --cpuset-cpus)");

  host.usable_cpus = 16;
  host.cpu_quota = 2.5;
  EXPECT_EQ(cpu_limit_warning(host),
            "This process may use only 2.5 of the 16 CPUs (a CPU quota, like "
            "Docker's --cpus)");

  host.usable_cpus = 4;
  EXPECT_EQ(cpu_limit_warning(host),
            "This process may use only 2.5 of the 16 CPUs (CPU affinity, like "
            "Docker's --cpuset-cpus, and a CPU quota, like Docker's --cpus)");
}
}  // namespace
}  // namespace fun

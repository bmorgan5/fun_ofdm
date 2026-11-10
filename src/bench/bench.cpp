/*! \file bench.cpp
 * \brief Entry point for fun-ofdm-bench: the receiver's benchmarks, on the
 * standard workloads.
 *
 * Each benchmark is named `<what>/<workload>`, for example
 * `receiver_chain/W-54`:
 *
 * - **`receiver_chain`** runs the whole workload through a receiver_chain, with
 *   its six threads, and times it on the wall clock. It's the receiver's
 *   throughput: real time at 20 MHz needs 20 MS/s.
 * - **One per block** (`frame_detector`, `timing_sync`, ...) runs that block
 *   alone, on one thread, on the batches it gets from the start of the
 *   workload. That's its cost per sample.
 *
 * Each reports `samples_per_second` and `time_per_sample` (in seconds, which
 * the console prints in ns), counting the
 * workload's samples, so a block's numbers compare directly with the chain's,
 * even for the blocks after fft_symbols, whose inputs are symbols.
 * receiver_chain also reports `frames_sent` and `frames_received`, so a
 * benchmark of a receiver that has stopped decoding doesn't go unnoticed, and
 * `frames_detected`, which on W-idle are all false alarms.
 *
 * google_benchmark's flags work as usual, e.g. `--benchmark_filter=W-54`,
 * `--benchmark_repetitions=20` or `--benchmark_out=FILE` with
 * `--benchmark_out_format=json`. `--help` lists them, and this file's own.
 * JSON belongs in a file, not on stdout, where frame_decoder prints an
 * "Invalid CRC" line for each frame that fails its CRC check.
 */

#include <algorithm>
#include <any>
#include <charconv>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "host_info.h"
#include "pipeline.h"
#include "src/framing.h"
#include "src/metrics/metrics.h"
#include "src/metrics/tracing.h"
#include "src/phy/receiver_chain.h"
#include "workload.h"

using namespace fun;

namespace {
//! Samples per call to the receiver, as rx reads them from the USRP
//! (NUM_RX_SAMPLES in receiver.h)
const std::size_t BATCH_SAMPLES = 4096;

//! Batches generated at a time for receiver_chain, with the clock paused
const std::size_t GENERATED_BATCHES = 256;

//! The default of --stage_seconds
const double DEFAULT_STAGE_SECONDS = 0.5;

/*!
 * \brief This binary's own flags, which google_benchmark doesn't know.
 */
struct flags {
  //! --workload_seconds: each workload's length, or std::nullopt for the
  //! standard 10 s
  std::optional<double> workload_seconds;

  //! --stage_seconds: how much of each workload the blocks' benchmarks use
  double stage_seconds = DEFAULT_STAGE_SECONDS;

  //! --trace: the file to record a trace of every benchmark into, if any
  std::optional<std::string> trace_file;
};

void print_help() {
  benchmark::PrintDefaultHelp();
  std::printf(
      "fun-ofdm-bench flags:\n"
      "  [--workload_seconds=<seconds>]\n"
      "      Each workload's length (default: the standard 10 s). The\n"
      "      receiver_chain benchmarks receive all of it.\n"
      "  [--stage_seconds=<seconds>]\n"
      "      How much of the start of each workload the blocks' benchmarks\n"
      "      use, repeating it as often as they need (default: %g s).\n"
      "  [--trace=<file>]\n"
      "      Record a trace of the benchmarks into the file, for\n"
      "      https://ui.perfetto.dev, and to measure what tracing costs.\n",
      DEFAULT_STAGE_SECONDS);
}

/*!
 * \brief Parses a flag's number of seconds.
 * \param arg The whole flag, for the error message
 * \param value The number
 * \return The seconds, or std::nullopt if they aren't a number greater than
 * 0, which this prints.
 */
std::optional<double> parse_seconds(std::string_view arg,
                                    std::string_view value) {
  double seconds;
  const char* end = value.data() + value.size();
  auto [last, error] = std::from_chars(value.data(), end, seconds);
  if (error != std::errc() || last != end || !(seconds > 0)) {
    std::fprintf(stderr, "%.*s: not a number of seconds greater than 0\n",
                 static_cast<int>(arg.size()), arg.data());
    return std::nullopt;
  }
  return seconds;
}

/*!
 * \brief Takes this binary's flags out of the command line, so that
 * google_benchmark doesn't reject them.
 * \return The flags, or std::nullopt if one is invalid, which this prints.
 */
std::optional<flags> parse_flags(int* argc, char** argv) {
  const std::string_view WORKLOAD = "--workload_seconds=";
  const std::string_view STAGE = "--stage_seconds=";
  const std::string_view TRACE = "--trace=";

  flags parsed;
  int kept = 1;
  for (int i = 1; i < *argc; i++) {
    std::string_view arg = argv[i];
    if (arg.starts_with(WORKLOAD)) {
      parsed.workload_seconds = parse_seconds(arg, arg.substr(WORKLOAD.size()));
      if (!parsed.workload_seconds) return std::nullopt;
    } else if (arg.starts_with(STAGE)) {
      std::optional<double> seconds =
          parse_seconds(arg, arg.substr(STAGE.size()));
      if (!seconds) return std::nullopt;
      parsed.stage_seconds = *seconds;
    } else if (arg.starts_with(TRACE) && arg.size() > TRACE.size()) {
      parsed.trace_file = std::string(arg.substr(TRACE.size()));
    } else {
      argv[kept++] = argv[i];
    }
  }
  *argc = kept;
  argv[kept] = nullptr;
  return parsed;
}

/*!
 * \brief Adds the machine's details to the output's context, which
 * google_benchmark's own lack, and warns if the process can't use every CPU.
 */
void add_host_context(const host_info& host) {
  auto or_none = [](const std::string& value) {
    return value.empty() ? std::string("none") : value;
  };
  benchmark::AddCustomContext("cpu_model", or_none(host.cpu_model));
  if (!host.board.empty()) benchmark::AddCustomContext("board", host.board);
  benchmark::AddCustomContext("arch", host.arch);
  benchmark::AddCustomContext("kernel", host.kernel);
  benchmark::AddCustomContext("cpus_online", std::to_string(host.online_cpus));
  benchmark::AddCustomContext("cpus_usable", std::to_string(host.usable_cpus));
  char quota[32] = "none";
  if (host.cpu_quota)
    std::snprintf(quota, sizeof(quota), "%g", *host.cpu_quota);
  benchmark::AddCustomContext("cpu_quota", quota);
  benchmark::AddCustomContext("container", or_none(host.container));

  std::string warning = cpu_limit_warning(host);
  if (!warning.empty()) {
    std::fprintf(stderr,
                 "***WARNING*** %s. receiver_chain runs 7 threads, so its "
                 "throughput depends on the limit.\n",
                 warning.c_str());
  }
}

/*!
 * \brief Reports the workload's samples that a benchmark processed.
 */
void report_samples(benchmark::State& state, double samples) {
  state.counters["samples_per_second"] =
      benchmark::Counter(samples, benchmark::Counter::kIsRate);
  // The rate's inverse: seconds per sample, which the console prints as ns
  state.counters["time_per_sample"] = benchmark::Counter(
      samples, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

/*!
 * \brief Runs a whole workload through a receiver_chain.
 *
 * Generating the samples, splitting them into batches, and creating and
 * destroying the chain, aren't timed. Neither are the zeros at the end that
 * flush the last frames out of the chain, which aren't counted as samples
 * either.
 */
void bench_chain(benchmark::State& state, const workload& spec) {
  double frames_sent = 0;
  double frames_received = 0;
  double frames_detected = 0;
  for (auto _ : state) {
    state.PauseTiming();
    workload_source source(spec);
    auto chain = std::make_unique<receiver_chain>();
    reassembler received;
    auto receive = [&](std::vector<std::complex<double>> batch) {
      for (const auto& payload : chain->process_samples(std::move(batch))) {
        if (received.add(payload)) frames_received++;
      }
    };
    // Outside the loop, so that the last ones are freed with the clock paused
    std::vector<std::complex<double>> samples;
    std::vector<std::vector<std::complex<double>>> batches;
    state.ResumeTiming();

    while (true) {
      state.PauseTiming();
      samples = source.read(GENERATED_BATCHES * BATCH_SAMPLES);
      batches.clear();
      for (std::size_t start = 0; start < samples.size();
           start += BATCH_SAMPLES) {
        std::size_t end = std::min(start + BATCH_SAMPLES, samples.size());
        batches.emplace_back(samples.begin() + start, samples.begin() + end);
      }
      state.ResumeTiming();
      if (batches.empty()) break;

      for (auto& batch : batches) receive(std::move(batch));
    }

    state.PauseTiming();
    // A batch takes 5 calls to reach the last block
    for (int i = 0; i < 6; i++)
      receive(std::vector<std::complex<double>>(BATCH_SAMPLES));
    frames_sent += static_cast<double>(source.whole_frames());
    frames_detected += static_cast<double>(chain->metrics().frames_detected);
    chain.reset();
    state.ResumeTiming();
  }

  double iterations = static_cast<double>(state.iterations());
  report_samples(state, iterations * static_cast<double>(spec.samples()));
  state.counters["frames_sent"] = frames_sent / iterations;
  state.counters["frames_received"] = frames_received / iterations;
  state.counters["frames_detected"] = frames_detected / iterations;
}

//! The type of a block's input batches
template <typename Block>
using batch_of = decltype(Block::input_buffer);

//! The captured inputs of the block benchmarked last, which stay until the
//! next block or workload, since each benchmark runs several times in a row
std::any g_captured;
std::string g_captured_key;  //!< The block and workload of #g_captured

/*!
 * \brief A block's input batches over the start of a workload, as
 * receiver_chain would give them to it.
 * \param spec The workload
 * \param name The block's name
 * \param member The block in a pipeline
 * \param samples How many of the workload's samples to capture the batches
 * for, rounded down to whole batches
 */
template <typename Block>
std::vector<batch_of<Block>>& captured_inputs(const workload& spec,
                                              const std::string& name,
                                              Block pipeline::* member,
                                              std::size_t samples) {
  std::string key = name + "/" + spec.name;
  if (g_captured_key != key) {
    g_captured.reset();  // Frees the last block's inputs first
    std::vector<batch_of<Block>> batches;
    workload_source source(spec);
    auto chain = std::make_unique<pipeline>();
    for (std::size_t i = 0; i < samples / BATCH_SAMPLES; i++) {
      std::vector<std::complex<double>> batch = source.read(BATCH_SAMPLES);
      if (batch.size() < BATCH_SAMPLES) break;
      // step() moves the samples into frame_detector itself
      if constexpr (std::is_same_v<Block, frame_detector>)
        batches.push_back(batch);
      else
        batches.push_back((*chain.*member).input_buffer);
      chain->step(std::move(batch));
    }
    g_captured = std::move(batches);
    g_captured_key = key;
  }
  return std::any_cast<std::vector<batch_of<Block>>&>(g_captured);
}

/*!
 * \brief Runs one block on its own, on its inputs from the start of a
 * workload, a batch per iteration, repeating them as often as it needs.
 *
 * A batch is swapped into the block's input buffer and back out again, which
 * takes no time: no block changes its input.
 */
template <typename Block>
void bench_block(benchmark::State& state, const workload& spec,
                 const std::string& name, Block pipeline::* member,
                 std::size_t samples) {
  std::vector<batch_of<Block>>& batches =
      captured_inputs(spec, name, member, samples);
  if (batches.empty()) {
    state.SkipWithError("The workload is shorter than one batch");
    return;
  }

  auto block = std::make_unique<Block>();
  std::size_t next = 0;
  for (auto _ : state) {
    block->input_buffer.swap(batches[next]);
    block->work();
    block->input_buffer.swap(batches[next]);
    benchmark::DoNotOptimize(block->output_buffer.data());
    benchmark::ClobberMemory();
    if (++next == batches.size()) next = 0;
  }
  report_samples(state, static_cast<double>(state.iterations()) *
                            static_cast<double>(BATCH_SAMPLES));
}

/*!
 * \brief Registers the benchmark of one block, on one workload.
 */
template <typename Block>
void register_block(const workload& spec, const std::string& name,
                    Block pipeline::* member, std::size_t samples) {
  benchmark::RegisterBenchmark(name + "/" + spec.name,
                               [=](benchmark::State& state) {
                                 bench_block(state, spec, name, member,
                                             samples);
                               })
      ->UseRealTime()
      ->Unit(benchmark::kMicrosecond);
}
}  // namespace

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  std::optional<flags> parsed = parse_flags(&argc, argv);
  if (!parsed) return 1;
  benchmark::Initialize(&argc, argv, print_help);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;

  // In the JSON output's context, for the scorecard
  benchmark::AddCustomContext("fun_ofdm_version", FUN_OFDM_VERSION);
  add_host_context(read_host_info());
  benchmark::AddCustomContext("batch_samples", std::to_string(BATCH_SAMPLES));
  benchmark::AddCustomContext("stage_seconds",
                              std::to_string(parsed->stage_seconds));
  if (parsed->workload_seconds) {
    benchmark::AddCustomContext("workload_seconds",
                                std::to_string(*parsed->workload_seconds));
  }

  for (workload spec : standard_workloads()) {
    if (parsed->workload_seconds) spec.seconds = *parsed->workload_seconds;
    auto samples = static_cast<std::size_t>(
        std::min(static_cast<double>(spec.samples()),
                 parsed->stage_seconds * spec.sample_rate));

    register_block(spec, "frame_detector", &pipeline::detector, samples);
    register_block(spec, "timing_sync", &pipeline::sync, samples);
    register_block(spec, "fft_symbols", &pipeline::fft, samples);
    register_block(spec, "channel_est", &pipeline::equalizer, samples);
    register_block(spec, "phase_tracker", &pipeline::tracker, samples);
    register_block(spec, "frame_decoder", &pipeline::decoder, samples);
    benchmark::RegisterBenchmark(
        "receiver_chain/" + spec.name,
        [spec](benchmark::State& state) { bench_chain(state, spec); })
        ->UseRealTime()
        ->Unit(benchmark::kMillisecond);
  }

  // What the build and run record, which changes what they cost
  benchmark::AddCustomContext("metrics", metrics::ENABLED ? "on" : "off");
  benchmark::AddCustomContext("trace", parsed->trace_file ? "on" : "off");
  if (parsed->trace_file) trace::start_file(*parsed->trace_file);

  benchmark::RunSpecifiedBenchmarks();
  trace::stop();
  benchmark::Shutdown();
  return 0;
}

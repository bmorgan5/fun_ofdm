/*! \file tracing.cpp
 * \brief C++ file for the receiver's traces, with the Perfetto SDK.
 *
 * perfetto.h is 8 MB, so only this file includes it, and the rest of the code
 * calls these functions.
 */

#include "tracing.h"

#include <pthread.h>

#if FUN_OFDM_METRICS
#include <fcntl.h>
#include <perfetto.h>
#include <unistd.h>

#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category(fun::trace::CATEGORY)
        .SetDescription("fun-ofdm's receiver: batches, blocks and frames"));
PERFETTO_TRACK_EVENT_STATIC_STORAGE();
#endif

namespace fun::trace {
#if FUN_OFDM_METRICS
namespace {
//! Guards the globals below
std::mutex g_mutex;

//! The backends Perfetto has been initialized with, which can only grow
std::uint32_t g_backends = 0;

//! The file's session, if one is running
std::unique_ptr<perfetto::TracingSession> g_session;

//! The file the session writes to
int g_fd = -1;

/*!
 * \brief Prints Perfetto's errors and warnings to stderr, but not the
 * messages it logs for every session.
 */
void log_message(perfetto::base::LogMessageCallbackArgs message) {
  if (message.level < perfetto::base::kLogImportant) return;
  std::fprintf(stderr, "Perfetto: %s\n", message.message);
}

/*!
 * \brief Initializes Perfetto with a backend, the first time it's asked for.
 * Perfetto can only be initialized once, so a second backend gets a second
 * call, which Perfetto allows for adding backends.
 */
void initialize(perfetto::BackendType backend) {
  if (g_backends & backend) return;
  perfetto::TracingInitArgs args;
  args.backends = backend;
  args.log_message_callback = log_message;
  perfetto::Tracing::Initialize(args);
  if (!g_backends) perfetto::TrackEvent::Register();
  g_backends |= backend;
}

//! A time point as Perfetto's timestamp, on CLOCK_MONOTONIC
perfetto::TraceTimestamp timestamp(metrics::time_point at) {
  return {perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC,
          static_cast<std::uint64_t>(at)};
}

//! Records an event's arguments
void add_args(perfetto::EventContext& ctx, std::initializer_list<arg> args) {
  for (const arg& a : args) ctx.AddDebugAnnotation(a.name, a.value);
}
}  // namespace

/*!
 * The buffer holds what's recorded between writes to the file, every 500 ms.
 * sim records about 100 events per batch, a few MB/s, so 64 MB leaves plenty
 * of room.
 */
void start_file(const std::string& path) {
  std::lock_guard lock(g_mutex);
  if (g_session) throw std::runtime_error("A trace is already running");
  initialize(perfetto::kInProcessBackend);

  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) throw std::runtime_error("Can't open " + path);

  perfetto::TraceConfig config;
  config.add_buffers()->set_size_kb(64 * 1024);
  config.set_write_into_file(true);
  config.set_file_write_period_ms(500);
  perfetto::protos::gen::TrackEventConfig track_events;
  track_events.add_enabled_categories(CATEGORY);
  auto* source = config.add_data_sources()->mutable_config();
  source->set_name("track_event");
  source->set_track_event_config_raw(track_events.SerializeAsString());

  g_session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
  g_session->Setup(config, fd);
  g_session->StartBlocking();
  g_fd = fd;
}

void start_system() {
  std::lock_guard lock(g_mutex);
  initialize(perfetto::kSystemBackend);
}

void stop() {
  std::lock_guard lock(g_mutex);
  if (!g_session) return;
  perfetto::TrackEvent::Flush();
  g_session->StopBlocking();
  g_session.reset();
  close(g_fd);
  g_fd = -1;
}

void name_thread(const std::string& name) {
  pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
  std::lock_guard lock(g_mutex);
  if (!g_backends) return;
  perfetto::ThreadTrack track = perfetto::ThreadTrack::Current();
  perfetto::protos::gen::TrackDescriptor descriptor = track.Serialize();
  descriptor.mutable_thread()->set_thread_name(name);
  perfetto::TrackEvent::SetTrackDescriptor(track, descriptor);
}

void slice(const char* name, metrics::time_point start, metrics::time_point end,
           std::initializer_list<arg> args) {
  TRACE_EVENT_BEGIN(CATEGORY, perfetto::StaticString{name}, timestamp(start),
                    [&](perfetto::EventContext ctx) { add_args(ctx, args); });
  TRACE_EVENT_END(CATEGORY, timestamp(end));
}

void instant(const char* name, metrics::time_point at,
             std::initializer_list<arg> args) {
  TRACE_EVENT_INSTANT(CATEGORY, perfetto::StaticString{name}, timestamp(at),
                      [&](perfetto::EventContext ctx) { add_args(ctx, args); });
}
#else
void start_file(const std::string&) {}
void start_system() {}
void stop() {}
void name_thread(const std::string& name) {
  pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
}
void slice(const char*, metrics::time_point, metrics::time_point,
           std::initializer_list<arg>) {}
void instant(const char*, metrics::time_point, std::initializer_list<arg>) {}
#endif
}  // namespace fun::trace

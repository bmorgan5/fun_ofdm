/*! \file realtime.cpp
 *  \brief C++ file for setting real time thread priority.
 */

#include "realtime.h"

#include <pthread.h>
#include <sched.h>

#include <iostream>

namespace fun {
bool set_realtime_priority() {
  // Get the current thread
  pthread_t this_thread = pthread_self();

  // Set priority to SCHED_RR
  struct sched_param params;
  params.sched_priority = sched_get_priority_max(SCHED_RR);
  if (pthread_setschedparam(this_thread, SCHED_RR, &params) != 0) {
    std::cout << "Unable to set realtime priority. Did you forget to sudo?"
              << std::endl;
    return false;
  }

  return true;
}
}  // namespace fun

/*! \file block.cpp
 *  \brief Base class for receiver chain blocks.
 *
 *  This file implements the synchronization and locking
 *  functions that can be used to synchronize blocks running
 *  in multiple threads
 *
 */

#include "block.h"

#include <mach/mach_init.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
// #include <mach/sched.h>

#include <iostream>
#include <sstream>

namespace fun{

    // int set_realtime(int period, int computation, int constraint)
    // {
    //     // std::cout << "setting realtime with HZ=" << HZ << std::endl;
    //     // return 0;

    //     struct thread_time_constraint_policy ttcpolicy;
    //     int ret;
    //     thread_port_t threadport = pthread_mach_thread_np(pthread_self());

    //     ttcpolicy.period = period;           // HZ/160
    //     ttcpolicy.computation = computation; // HZ/3300;
    //     ttcpolicy.constraint = constraint;   // HZ/2200;
    //     ttcpolicy.preemptible = 1;

    //     if ((ret = thread_policy_set(threadport,
    //                                 THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&ttcpolicy,
    //                                 THREAD_TIME_CONSTRAINT_POLICY_COUNT)) != KERN_SUCCESS)
    //     {
    //         std::cout << "set_realtime() failed" << std::endl;
    //         return 0;
    //     }
    //     return 1;
    // }

    void block_base::set_priority(float p) {
        if(p < 0.0 || 1.0 < p) {
            std::stringstream msg;
            msg << "Priority of " << p << " is outside acceptable range of [0.0, 1.0]...not changing priority" << std::endl;
            std::cout << msg.str();
            return;
        }
        priority = p;
    }

    void block_base::set_status(enum STATUS status)
    {
        // This scope is important so that wake_lock goes out of scope and is destructed
        // and unlocks (because of RAII) before the call to notify_all()
        {
            std::lock_guard<std::mutex> status_lock(m_status_mtx);
            m_status = status;
        }
        m_status_cv.notify_all();
    }

    void block_base::wait_on_status(enum STATUS status)
    {
        std::unique_lock<std::mutex> status_lock(m_status_mtx);
        m_status_cv.wait(status_lock, [=]{
            if(m_status == status)
            {
                return true;
            }
            return false;
        });
    }

}
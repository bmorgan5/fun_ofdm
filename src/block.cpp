/*! \file block.cpp
 *  \brief Base class for receiver chain blocks.
 *
 *  This file implements the synchronization and locking
 *  functions that can be used to synchronize blocks running
 *  in multiple threads
 *
 */

#include "block.h"

namespace fun{

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
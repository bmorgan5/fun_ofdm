/*! \file receiver_chain.cpp
 *  \brief C++ file for Receiver Chain class.
 *
 *  The Receiver Chain class is the main controller for the blocks that are
 *  used to receive and decode PHY layer frames. It holds the instances of each block
 *  and shifts the data through the receive chain as it is processed eventually returning
 *  the correctly received payloads (MPDUs) which can then be passed to the upper layers.
 */

#include <iostream>
#include <functional>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "receiver_chain.h"

namespace fun
{
    /*!
     * -Initializes each receiver chain block:
     *  + frame_detector
     *  + timing_sync
     *  + fft_symbols
     *  + channel_est
     *  + phase_tracker
     *  + frame_decoder
     *
     *  Adds each block to the receiver chain.
     */

    //TODO: Figure out how to calculate this programmatically
    const int num_blocks = 6;
    receiver_chain::receiver_chain()
    {

        m_wake_mtxs = std::vector<std::mutex>(num_blocks);
        m_wake_conditions = std::vector<std::condition_variable>(num_blocks);
        m_wake_flags = std::vector<status>(num_blocks, status::DONE);
        // TODO: Creating an object on the heap is pretty unnecessary here
        // instead it should be created locally and then use c++ casting features
        // to pass it to a function if needed
        m_frame_detector = new frame_detector();
        m_timing_sync = new timing_sync();
        m_fft_symbols = new fft_symbols();
        m_channel_est = new channel_est();
        m_phase_tracker = new phase_tracker();
        m_frame_decoder = new frame_decoder();


        // Add the blocks to the receiver chain
        add_block(m_frame_detector);
        add_block(m_timing_sync);
        add_block(m_fft_symbols);
        add_block(m_channel_est);
        add_block(m_phase_tracker);
        add_block(m_frame_decoder);


        // m_done_mtxs = std::vector<std::mutex>(m_threads.size());
        // m_done_conditions = std::vector<std::condition_variable>(m_threads.size());
        // m_done_flags = std::vector<bool>(m_threads.size(), false);
    }

    // TODO: Stop threads when destructing
    receiver_chain::~receiver_chain()
    {
        for(int i = 0; i < num_blocks; ++i)
        {
            std::stringstream msg;
            msg << "Joining thread " << i << std::endl;
            std::cout << msg.str();

            m_threads[i].join();
        }
        std::cout << "Done" << std::endl;
    }

    /*!
     * The #add_block function creates a wake & done semaphore for each block.
     * It then creates a new thread for the block to run in and adds that thread
     * to the thread vector for reference.
     */
    void receiver_chain::add_block(block_base * block)
    {
        // Initialize all the threads to be NOT awake
        // m_wake_flags.push_back(false);
        // m_wake_mtxs.emplace_back();
        // m_wake_conditions.emplace_back();

        // // Initialize all the threads to be done
        // m_done_flags.push_back(true);
        // m_done_mtxs.emplace_back();
        // m_done_conditions.emplace_back();

        size_t index = m_threads.size();
        m_threads.push_back(std::thread(&receiver_chain::run_block, this, index, block));
    }

    /*!
     * TODO: Write me!
     */
    // TODO: maybe I should add a wait to this as well
    // but if it blocks on wake_lock it shouldn't block long b/c the other lock is using a wait
    // which releases the lock very quickly
    void receiver_chain::post_to_wake(size_t index)
    {
        // This scope is important so that wake_lock goes out of scope and is destructed
        // and unlocks (because of RAII) before the call to notify_all()
        {
            std::lock_guard<std::mutex> wake_lock(m_wake_mtxs[index]);

            std::stringstream msg;
            msg << "Waking thread " << index << std::endl;
            std::cout << msg.str();

            m_wake_flags[index] = status::READY;
        }
        m_wake_conditions[index].notify_all(); // This is important if someone in blocked on this thread
    }

    void receiver_chain::wait_on_wake(size_t index)
    {
        std::unique_lock<std::mutex> wake_lock(m_wake_mtxs[index]);
        m_wake_conditions[index].wait(wake_lock, [=] {
            std::stringstream msg;
            msg << "Waiting on wake for thread " << index << std::endl;
            std::cout << msg.str();

            if(m_wake_flags[index] == status::READY)
            {
                // m_wake_flags[index] = status::RUNNING;
                return true;
            }
            return false;
        });
    }

    void receiver_chain::post_to_done(size_t index)
    {
        // This scope is important so that wake_lock goes out of scope and is destructed
        // and unlocks (because of RAII) before the call to notify_all()
        {
            std::lock_guard<std::mutex> wake_lock(m_wake_mtxs[index]);

            std::stringstream msg;
            msg << "Done with thread " << index << std::endl;
            std::cout << msg.str();

            m_wake_flags[index] = status::DONE;
        }
        m_wake_conditions[index].notify_all(); // There should only be at most one thread blocked on this at a time
    }

    void receiver_chain::wait_on_done(size_t index)
    {
        std::unique_lock<std::mutex> wake_lock(m_wake_mtxs[index]);
        m_wake_conditions[index].wait(wake_lock, [=] {
            std::stringstream msg;
            msg << "Waiting on done for thread " << index << std::endl;
            std::cout << msg.str();
            if(m_wake_flags[index] == status::DONE) {
                return true;
                // m_wake_flags[index] =
            }
            return false;
            // return m__flags[index] == true;
        });
    }

    /*!
     * The #run_block function is the main thread for controlling the calls to
     * each block's work function. This function is a forever loops that first waits
     * for the wake_sempahore to post indicating its time for the block to "wake up" and
     * process the data that has just been placed in its input_buffer by running its work()
     * function. Then once, the work() function returns run_block posts to the done sempahore
     * that the block has finished processing everything in the input_buffer. At this point
     * it loops back around and waits for the block to be "woken up" again when the next set
     * of input data is ready.
     */
    void receiver_chain::run_block(size_t index, block_base * block)
    {
        while(1)
        {
            wait_on_wake(index);

            std::stringstream msg;
            msg << "run_block thread " << index << std::endl;
            std::cout << msg.str();

            boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();
            block->work();
            boost::posix_time::time_duration elapsed = boost::posix_time::microsec_clock::local_time() - start;

            if(elapsed.total_microseconds() > (2000 / 5e6 * 1e6))
            {
                //std::cout << "! - " <<  block->name << std::endl;
            }

            post_to_done(index);
        }
    }

    /*!
     * TODO: Update me!
     * This function is the main scheduler for the receive chain. It takes in raw complex samples
     * from the usrp block and passes them first into the Frame Detector block's input buffer.
     * It then unlocks each of the threads by posting to each block's "wake" semaphore. It then
     * waits for each thread to post that it is done with that call to its work() function.
     * Once all the threads are done it shifts the contents of each blocks output buffer to the input
     * buffer of the next block in the chain and returns the contents of the Frame Decoder's
     * output buffer.
     */
    std::vector<std::vector<unsigned char> > receiver_chain::process_samples(std::vector<std::complex<double> > samples)
    {
        // samples -> sync short in
        m_frame_detector->input_buffer.swap(samples);

        // Unlock the threads
        // This could possibly be done with a single notify_all
        for(size_t x = 0; x < num_blocks; x++) post_to_wake(x);

        // Wait for the threads to finish
        for(size_t x = 0; x < num_blocks; x++) wait_on_done(x);

        // Update the buffers
        m_timing_sync->input_buffer.swap(m_frame_detector->output_buffer);
        m_fft_symbols->input_buffer.swap(m_timing_sync->output_buffer);
        m_channel_est->input_buffer.swap(m_fft_symbols->output_buffer);
        m_phase_tracker->input_buffer.swap(m_channel_est->output_buffer);
        m_frame_decoder->input_buffer.swap(m_phase_tracker->output_buffer);

        // Return any completed packets
        return m_frame_decoder->output_buffer;
        // return std::vector<std::vector<unsigned char>>();
    }

}

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
#include <uhd/utils/thread_priority.hpp>
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
    // const int num_blocks = 1;
receiver_chain::receiver_chain() :
    block("receiver_chain"),
    m_halt(false)
{

    // m_wake_mtxs = std::vector<std::mutex>(num_blocks);
    // m_wake_conditions = std::vector<std::condition_variable>(num_blocks);
    // m_wake_flags = std::vector<status>(num_blocks, status::DONE);
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
        halt_blocks();
        for(int i = 0; i < m_blocks.size(); ++i)
        {
            // std::stringstream msg;
            // msg << "Joining thread " << i << std::endl;
            // std::cout << msg.str();

            m_threads[i].join();
        }
    }

    /*!
     * The #add_block function creates a wake & done semaphore for each block.
     * It then creates a new thread for the block to run in and adds that thread
     * to the thread vector for reference.
     */
    void receiver_chain::add_block(block_base * block)
    {
        m_blocks.push_back(block);
        m_threads.push_back(std::thread(&receiver_chain::run_block, this, block));
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
    void receiver_chain::run_block(block_base * block)
    {
        std::stringstream msg;
        uhd::set_thread_priority_safe();

        sched_param sch;
        int policy;
        pthread_getschedparam(pthread_self(), &policy, &sch);
        msg << block->name << " is executing with policy " << policy << " at priority " << sch.sched_priority << std::endl;
        std::cout << msg.str();
        msg.str("");

        size_t num_loops = 0;
        boost::posix_time::time_duration total_elapsed(0,0,0,0);
        while(1)
        {
            ++num_loops;

            /* ---- core ------------------------------------------------------------------------*/
            block->wait_on_status(STATUS::WAKE);
            // This context is important for RAII style
            // {
            //     std::lock_guard<std::mutex> halt_lock(m_halt_mtx);
            //     if(m_halt) return;
            // }
            boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();

            block->work();

            boost::posix_time::time_duration elapsed = boost::posix_time::microsec_clock::local_time() - start;

            block->set_status(STATUS::DONE);
            /* ---- core ------------------------------------------------------------------------*/

            total_elapsed += elapsed;
            if (num_loops % NUM_DEBUG_LOOPS == 0)
            {
                msg << block->name << " avg work time = " << total_elapsed.total_microseconds() / num_loops << "us" << std::endl;
                std::cout << msg.str();
                msg.str("");
            }
            // if(elapsed.total_microseconds() > (2000 / 5e6 * 1e6))
            // {
            //std::cout << "! - " <<  block->name << std::endl;
            // }
        }
    }

    void receiver_chain::halt_blocks() {
        std::lock_guard<std::mutex> halt_lock(m_halt_mtx);
        m_halt = true;
        // Wake the blocks so they can exit their loops
        // for(size_t x = 0; x < num_blocks; x++) post_to_wake(x);
        for(auto block : m_blocks) block->set_status(STATUS::WAKE);
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
    std::vector<std::vector<unsigned char>> receiver_chain::process_samples(std::vector<std::complex<double>> &samples)
    {
        // samples -> sync short in
        m_frame_detector->input_buffer.swap(samples);

        // Unlock the threads
        for(auto block : m_blocks) block->set_status(STATUS::WAKE);

        // Wait for the blocks to finish
        for(auto block : m_blocks) block->wait_on_status(STATUS::DONE);

        // Update the buffers
        m_timing_sync->input_buffer.swap(m_frame_detector->output_buffer);
        m_fft_symbols->input_buffer.swap(m_timing_sync->output_buffer);
        m_channel_est->input_buffer.swap(m_fft_symbols->output_buffer);
        m_phase_tracker->input_buffer.swap(m_channel_est->output_buffer);
        m_frame_decoder->input_buffer.swap(m_phase_tracker->output_buffer);

        // // Return any completed packets
        return m_frame_decoder->output_buffer;
        // return std::vector<std::vector<unsigned char>>();
    }

    void receiver_chain::work()
    {
        // TODO: Why do this here
        m_frame_detector->input_buffer.swap(this->input_buffer);

        // Wake all the blocks
        for (auto block : this->m_blocks) block->set_status(STATUS::WAKE);

        // Wait for the blocks to finish
        for (auto block : this->m_blocks) block->wait_on_status(STATUS::DONE);

        // Update the buffers
        m_timing_sync->input_buffer.swap(m_frame_detector->output_buffer);
        m_fft_symbols->input_buffer.swap(m_timing_sync->output_buffer);
        m_channel_est->input_buffer.swap(m_fft_symbols->output_buffer);
        m_phase_tracker->input_buffer.swap(m_channel_est->output_buffer);
        m_frame_decoder->input_buffer.swap(m_phase_tracker->output_buffer);

        // Return any completed packets
        this->output_buffer.swap(m_frame_decoder->output_buffer);
    }

}

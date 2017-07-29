/*! \file receiver.h
 *  \brief C++ file for the receiver class.
 *
 *  The receiver class is the public interface for the fun_ofdm receiver.
 *  This is the easiest way to start receiving 802.11a OFDM frames out of the box.
 */

#include "receiver.h"

#include <uhd/utils/thread_priority.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <pthread.h>
// #include <pthread_impl.h>

namespace fun
{

    /*!
     * This constructor shows exactly what parameters need to be set for the receiver.
     */
    receiver::receiver(void (*callback)(std::vector<std::vector<unsigned char>> &packets), double freq, double samp_rate, double rx_gain, std::string device_addr) :
        receiver(callback, usrp_params(freq, samp_rate, 20, rx_gain, 1.0, device_addr))
    {
    }

    /*!
     * This constructor is for those who feel more comfortable using the usrp_params struct.
     */
    receiver::receiver(void (*callback)(std::vector<std::vector<unsigned char>> &packets), usrp_params params) :
        // m_usrp(params),
        m_halt(false),
        m_samples(NUM_RX_SAMPLES),
        m_callback(callback)
        // m_rec_chain()
    {
        m_usrp = new usrp(params);
        m_usrp->set_priority(0.0);

        m_rec_chain = new receiver_chain();
        m_rec_chain->set_priority(0.75);

        // sem_init(&m_pause, 0, 1); //Initial value is 1 so that the receiver_chain_loop() will begin executing immediately
        // m_rec_thread = std::thread(&receiver::receiver_chain_loop, this); //Initialize the main receiver thread
    }

    /*!
     *  This function loops forever (unless it is paused) pulling samples from the USRP and passing them through the
     *  receiver chain. It then passes any successfully decoded packets to the callback function for the user
     *  to process further. This function can be paused by the user by calling the receiver::pause() function,
     *  presumably so that the user can transmit packets over the air using the transmitter. Once the user is finished
     *  transmitting he/she can resume the receiver by called the receiver::resume() function. These two functions use
     *  an internal semaphore to block the receiver code execution while in the paused state.
     */
    void receiver::receiver_chain_loop()
    {
        // std::stringstream msg;
        // size_t num_loops = 1000;
        // boost::posix_time::time_duration total_time(0,0,0,0);
        // m_usrp->start_stream();
        // // m_samples.resize(NUM_RX_SAMPLES);
        // // while(1)
        // for(size_t i = 0; i < num_loops; i++)
        // {
        //     // sem_wait(&m_pause); // Block if the receiver is paused
        //     boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();

        //     // std::cout << "Get samples" << std::endl;
        //     // TODO: This needs to run in its own thread!!!!
        //     m_usrp->get_samples(NUM_RX_SAMPLES, m_samples);

        //     // std::vector<std::vector<unsigned char>> packets =
        //             m_rec_chain->process_samples(m_samples);

        //     boost::posix_time::time_duration elapsed = boost::posix_time::microsec_clock::local_time() - start;
        //     total_time += elapsed;
        //     // m_callback(packets);

        //     // sem_post(&m_pause); // Flags the end of this loop and wakes up any other threads waiting on this semaphore
        //                         // i.e. a call to the pause() function in the main thread.
        // }

        // boost::posix_time::time_duration avg_time = total_time/num_loops;
        // std::cout << "Average time = " << avg_time.total_microseconds() << "us" << std::endl;
    }

    void receiver::run_block(block_base * block)
    {
        std::stringstream msg;
        sched_param sch;
        int policy;

        uhd::set_thread_priority_safe(block->priority);
        pthread_getschedparam(pthread_self(), &policy, &sch);

        if(block == m_usrp) {
            // policy = SCHED_FIFO;
            policy = SCHED_RR;
            sch.sched_priority = sched_get_priority_min(policy);
            int err = pthread_setschedparam(pthread_self(), policy, &sch);
            if(err != 0){
                msg << "Failed to " << block->name << " to SCHED_FIFO...performance may be negatively affected" << std::endl;
                std::cout << msg.str();
                msg.str("");
            }
        }

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
            boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();
            // This context is important for RAII style
            // {
            //     std::lock_guard<std::mutex> halt_lock(m_halt_mtx);
            //     // std::cout << block->name << " acquired halt lock" << std::endl;
            //     if (m_halt) return;
            // }
            block->work();

            total_elapsed += boost::posix_time::microsec_clock::local_time() - start;
            block->set_status(STATUS::DONE);
            /* ---- core ------------------------------------------------------------------------*/

            if (num_loops % NUM_DEBUG_LOOPS == 0)
            {
                msg << block->name << " avg work time = " << total_elapsed.total_microseconds() / num_loops << "us" << std::endl;
                std::cout << msg.str();
                msg.str("");
            }

        }
    }

    void receiver::rx_chain_loop() {
        std::stringstream msg;
        uhd::set_thread_priority_safe(0.75);
        sched_param sch;
        int policy;
        pthread_getschedparam(pthread_self(), &policy, &sch);
        msg << "Rx chain LOOP is executing with policy " << policy << " at priority " << sch.sched_priority << std::endl;
        std::cout << msg.str();
        msg.str("");

        size_t num_loops = 0;
        boost::posix_time::time_duration total_elapsed(0,0,0,0);
        boost::posix_time::time_duration total_wake_usrp_time(0, 0, 0, 0);
        boost::posix_time::time_duration total_wake_recv_time(0, 0, 0, 0);
        while(1)
        {
            ++num_loops;
            boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();

            /* ---- core ------------------------------------------------------------------------*/
            // This context is important for RAII style
            // {
            //     std::lock_guard<std::mutex> halt_lock(m_halt_mtx);
            //     if (m_halt)
            //     {
            //         std::cout << "recv chain loop halted" << std::endl;
            //         return;
            //     }
            // }

            boost::posix_time::ptime wake_usrp_start = boost::posix_time::microsec_clock::local_time();
            m_usrp->set_status(STATUS::WAKE);
            total_wake_usrp_time += boost::posix_time::microsec_clock::local_time() - wake_usrp_start;

            boost::posix_time::ptime wake_recv_start = boost::posix_time::microsec_clock::local_time();
            m_rec_chain->set_status(STATUS::WAKE);
            total_wake_recv_time += boost::posix_time::microsec_clock::local_time() - wake_recv_start;

            m_usrp->wait_on_status(STATUS::DONE);
            m_rec_chain->wait_on_status(STATUS::DONE);

            m_rec_chain->input_buffer.swap(m_usrp->output_buffer);
            m_packets.swap(m_rec_chain->output_buffer);
            // m_callback_thread = std::thread(m_callback, std::ref(m_packets));
            /* ---- core ------------------------------------------------------------------------*/

            total_elapsed += boost::posix_time::microsec_clock::local_time() - start;

            if (num_loops % NUM_DEBUG_LOOPS == 0)
            {
                msg << "receiver chain loop" << " avg work time = " << total_elapsed.total_microseconds() / num_loops << "us" << std::endl;
                msg << "    " << "wake usrp time = " << total_wake_usrp_time.total_microseconds() / num_loops << "us" << std::endl;
                msg << "    " << "wake recv time = " << total_wake_recv_time.total_microseconds() / num_loops << "us" << std::endl;
                std::cout << msg.str();
                msg.str("");
            }
        }
    }

    /*!
     *  This function posts to (clears) the internal semaphore that is blocking the receiver loop code execution
     *  due to a previous call to the receiver::pause() function, thus allowing the main receiver loop to begin
     *  executing again.
     */
    void receiver::start()
    {
        std::stringstream msg;
        int policy;
        sched_param sch;
        uhd::set_thread_priority_safe(0.0, false);
        pthread_getschedparam(pthread_self(), &policy, &sch);
        msg << "Main thread is executing with policy " << policy << " at priority " << sch.sched_priority << std::endl;
        std::cout << msg.str();
        msg.str("");

        msg << "Start receiver" << std::endl;
        m_usrp->start_stream();
        // m_usrp->set_status(STATUS::WAKE);



        m_usrp_thread = std::thread(&receiver::run_block, this, m_usrp);

        m_rec_chain_thread = std::thread(&receiver::run_block, this, m_rec_chain); //Initialize the main receiver thread

        m_rec_chain_loop_thread = std::thread(&receiver::rx_chain_loop, this);

        std::cout << "Press any key to halt..." << std::endl;
        std::cin.get();
        halt();
        m_usrp_thread.join();
        // std::cout << "USRP halted" << std::endl;

        m_rec_chain_thread.join();
        // std::cout << "Recv halted" << std::endl;

        // m_callback_thread.join();
        // std::cout << "Callback halted" << std::endl;

        m_rec_chain_loop_thread.join();
        // std::cout << "Rx chain loop halted" << std::endl;
    }

    void receiver::halt()
    {
        // This context is important for RAII style
        {
            std::lock_guard<std::mutex> halt_lock(m_halt_mtx);
            m_halt = true;
        }
        // std::cout << "Halt: waking m_usrp";
        m_usrp->set_status(STATUS::WAKE);
        // std::cout << "Halt woke usrp" << std::endl;
        m_rec_chain->set_status(STATUS::WAKE);
        m_usrp->set_status(STATUS::DONE);
        m_rec_chain->set_status(STATUS::DONE);

    }

    // This can block until
    // bool receiver::check_halt()
    // {
    //     std::lock_guard<std::mutex> halt_lock(m_halt_mtx);
    //     if (m_halt)
    //     {
    //         return true;
    //     }
    //     return false;
    //     // std::lock_guard<std::mutex> halt_lock(m_halt_mtx);
    //     // return m_halt;
    // }
    /*!
     *  Uses an internal semaphore to block the execution of the receiver loop code effectively pausing
     *  the receiver until the semaphore is posted to (cleared) by the receiver::resume() function.
     */
    void receiver::pause()
    {
        // sem_wait(&m_pause);
    }

    void receiver::resume()
    {

    }
}

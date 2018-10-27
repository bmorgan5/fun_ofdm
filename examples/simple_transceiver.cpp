#include <iostream>

#include <transmitter.h>
#include <receiver.h>

void callback(std::vector<std::vector<unsigned char> > payloads);
bool set_realtime_priority();

using namespace std;
using namespace fun;

int main()
{
    set_realtime_priority();

    usrp_params params = usrp_params();
    transmitter tx = transmitter(params);
    receiver rx = receiver(&callback, params);

    std::string s = "Hello World";
    std::vector<unsigned char> data = std::vector<unsigned char>(12);
    memcpy(&data[0], &s[0], 12);
    tx.send_frame(data);

    while(1)
    {
        sleep(4);
        rx.pause();
        tx.send_frame(data);
        cout << "Sending \"Hello World\" " << std::endl;
        rx.resume();
    }

    return 0;
}

void callback(std::vector<std::vector<unsigned char> > payloads)
{
    for(int i = 0; i < payloads.size(); i++)
    {
        std::cout << "Received a packet" << std::endl;
    }

}

/*!
 * \brief Attempt to set real time priority for thread scheduling
 * \return Whether or not real time priority was succesfully set.
 */
bool set_realtime_priority()
{
    // Get the current thread
    pthread_t this_thread = pthread_self();

    // Set priority to SCHED_FIFO
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_RR);
    if (pthread_setschedparam(this_thread, SCHED_RR, &params) != 0)
    {
        std::cout << "Unable to set realtime priority. Did you forget to sudo?" << std::endl;
        return false;
    }

    return true;
}

#include <iostream>

#include <fun_ofdm/transmitter.h>
#include <fun_ofdm/receiver.h>

void callback(std::vector<std::vector<unsigned char> > payloads);

using namespace std;
using namespace fun;

int main()
{

    usrp_params params = usrp_params();
    transmitter tx = transmitter(params);
    receiver rx = receiver(&callback, params);

    std::string s = "Hello World";
    std::vector<unsigned char> data = std::vector<unsigned char>(12);
    memcpy(&data[0], &s[0], 12);
    tx.send_packet(data);

    while(1)
    {
        sleep(4);
        rx.pause();
        tx.send_packet(data);
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

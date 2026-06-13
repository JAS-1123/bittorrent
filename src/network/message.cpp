#include "message.hpp"
#include <cstring>
#include <arpa/inet.h>
using namespace std;

namespace bencode {

    vector<unsigned char> Message::create_interested() {
        vector<unsigned char> packet(5);
        
        uint32_t msg_len = htonl(1);
        memcpy(&packet[0], &msg_len, 4);
        
        packet[4] = static_cast<uint8_t>(MessageId::INTERESTED);
        
        return packet;
    }

    vector<unsigned char> Message::create_request(uint32_t index, uint32_t begin, uint32_t length) {
        vector<unsigned char> packet(17);

        uint32_t msg_len = htonl(13); 
        memcpy(&packet[0], &msg_len, 4);

        packet[4] = static_cast<uint8_t>(MessageId::REQUEST);

        uint32_t net_index  = htonl(index);
        uint32_t net_begin  = htonl(begin);
        uint32_t net_length = htonl(length);

        memcpy(&packet[5],  &net_index,  4);
        memcpy(&packet[9],  &net_begin,  4);
        memcpy(&packet[13], &net_length, 4);

        return packet;
    }
}
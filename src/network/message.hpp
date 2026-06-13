#ifndef MESSAGE_HPP
#define MESSAGE_HPP
#include <vector>
#include <cstdint>

namespace bencode {
    enum class MessageId : uint8_t {
        CHOKE          = 0,
        UNCHOKE        = 1,
        INTERESTED     = 2,
        NOT_INTERESTED = 3,
        HAVE           = 4,
        BITFIELD       = 5,
        REQUEST        = 6,
        PIECE          = 7,
        CANCEL         = 8
    };

    class Message {
    public:
        static std::vector<unsigned char> create_interested();
        static std::vector<unsigned char> create_request(uint32_t index, uint32_t begin, uint32_t length);
    };
}

#endif
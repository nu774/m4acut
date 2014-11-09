#include "StringConverterUTF8.h"

namespace {
    bool is_lead_byte(unsigned char c)
    {
        return c < 0x80 || (c >= 0xc0 && c <= 0xfd);
    }
    int num_trailing_bytes(unsigned char c)
    {
        if (c < 0x80) return 0;
        else if (c < 0xe0) return 1;
        else if (c < 0xf0) return 2;
        else if (c < 0xf8) return 3;
        else if (c < 0xfc) return 4;
        else return 5;
    }
}

std::pair<bool, std::string>
StringConverterUTF8::convert(const std::string &s, bool flush)
{
    const char *end = s.data() + s.size();
    const char *p = s.data();
    std::string dest;
    bool good = true;
    unsigned char c;

    for (; m_trailing > 0 && p < end; --m_trailing)
        if (((c = *p++) >> 6) != 2)
            good = false;

    while (good && p < end) {
        if (!is_lead_byte(c = *p++))
            good = false;
        m_trailing = num_trailing_bytes(c);
        for (; m_trailing > 0 && p < end; --m_trailing)
            if (((c = *p++) >> 6) != 2)
                good = false;
    }
    if (m_trailing && flush)
        good = false;
    return std::make_pair(good, s);
}

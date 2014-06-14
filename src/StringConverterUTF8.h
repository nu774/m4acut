#ifndef STRING_CONVERTER_UTF8
#define STRING_CONVERTER_UTF8

#include <string>
#include <utility>
#include "StringConverter.h"

class StringConverterUTF8: public IStringConverter {
    int m_trailing;
public:
    StringConverterUTF8() : m_trailing(0) {}
    std::pair<bool, std::string> convert(const std::string &s, bool flush=true);
};

#endif

#ifndef STRING_CONVERTER_ICONV
#define STRING_CONVERTER_ICONV

#include <string>
#include <memory>
#include <utility>
#include "StringConverter.h"

class StringConverterIConv: public IStringConverter {
    std::shared_ptr<void> m_handle;
    std::string           m_remainder;
public:
    StringConverterIConv(const char *tocharset, const char *fromcharset);
    std::pair<bool, std::string> convert(const std::string &s, bool flush=true);
};

#endif

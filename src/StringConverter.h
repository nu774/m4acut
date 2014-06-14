#ifndef STRING_CONVERTER
#define STRING_CONVERTER

#include <string>
#include <utility>

struct IStringConverter {
    virtual ~IStringConverter() {}
    virtual std::pair<bool, std::string> convert(const std::string &s, bool flush)=0;
};

#endif

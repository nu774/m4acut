#ifndef STRING_CONVERTER_WIN32
#define STRING_CONVERTER_WIN32

#include <string>
#include <utility>
#include <windows.h>
#include "StringConverter.h"

class StringConverterWin32: public IStringConverter {
    unsigned    m_from_codepage;
    unsigned    m_to_codepage;
    CPINFOEXW   m_cpinfo;
    std::string m_remainder;
public:
    StringConverterWin32(const char *tocharset, const char *fromcharset);
    std::pair<bool, std::string> convert(const std::string &s, bool flush=true);
private:
    int mbtowc(const char *s, size_t len, wchar_t *wc);
};

#endif

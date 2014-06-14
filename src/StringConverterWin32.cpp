#include <cstdio>
#include <cctype>
#include <vector>
#include <utility>
#include <stdexcept>
#include <windows.h>
#include "StringConverterWin32.h"

namespace {
    std::string cleanse_charset_name(const char *s)
    {
        unsigned char c;
        std::string res;
        while ((c = *s++))
            if (c != '_' && c != '-')
                res.push_back(std::tolower(c));
        return res;
    }
    unsigned get_codepage_from_name(const char *s)
    {
        struct codepage_entry {
            const char *name;
            unsigned codepage;
        } codepages[] = {
            { "tis620",         874 },
            { "shiftjis",       932 },
            { "gb2312",         936 },
            { "gbk",            936 },
            { "big5",           950 },
            { "xmaccyrillic", 10007 },
            { "koi8r",        20866 },
            { "koi8u",        21866 },
            { "iso2022jp",    50220 },
            { "iso2022kr",    50225 },
            { "iso2022cn",    50227 },
            { "eucjp",        51932 },
            { "euccn",        51936 },
            { "euckr",        51949 },
            { "euctw",        51950 },
            { "xeuctw",       51950 },
            { "hzgb2312",     52936 },
            { "gb18030",      54936 },
            { 0,              0     }
        };
        std::string ss = cleanse_charset_name(s);
        unsigned cp;
        if (std::sscanf(ss.c_str(), "%u", &cp) == 1)
            return cp;
        else if (std::sscanf(ss.c_str(), "cp%u", &cp) == 1)
            return cp;
        else if (std::sscanf(ss.c_str(), "ibm%u", &cp) == 1)
            return cp;
        else if (std::sscanf(ss.c_str(), "windows%u", &cp) == 1)
            return cp;
        else if (std::sscanf(ss.c_str(), "iso8859%u", &cp) == 1)
            return cp + 28590;
        else if (ss == "utf7")
            return 65000;
        else if (ss == "utf8")
            return 65001;
        else {
            for (codepage_entry *p = codepages; p->name; ++p)
                if (ss == p->name)
                    return p->codepage;
            return 0;
        }
    }
}

StringConverterWin32::StringConverterWin32(const char *to, const char *from)
{
    m_from_codepage = get_codepage_from_name(from);
    m_to_codepage   = get_codepage_from_name(to);

    CPINFOEXW ci;
    if (!m_from_codepage
     || !m_to_codepage
     || !GetCPInfoExW(m_from_codepage, 0, &m_cpinfo)
     || !GetCPInfoExW(m_to_codepage,   0, &ci))
        throw std::runtime_error("unknown codepage");
}

std::pair<bool, std::string>
    StringConverterWin32::convert(const std::string &s, bool flush)
{
    m_remainder += s;
    std::vector<wchar_t> pivot;
    wchar_t wc;
    int n;
    const char *ip   = m_remainder.data();
    size_t      left = m_remainder.size();
    bool        res  = true;

    while (left > 0) {
        if ((n = mbtowc(ip, left, &wc)) > 0) {
            pivot.push_back(wc);
            ip += n;
            left -= n;
        } else if (flush || left >= m_cpinfo.MaxCharSize) {
            res = false;
            pivot.push_back(0xfffd);
            ip++;
            left--;
        } else
            break;
    }
    ptrdiff_t off = ip - m_remainder.data();
    if (off == m_remainder.size())
        m_remainder.clear();
    else
        m_remainder = m_remainder.substr(off);

    n = 0;
    std::vector<char> buf;
    if (pivot.size()) {
        n = WideCharToMultiByte(m_to_codepage, 0, pivot.data(),
                                pivot.size(), 0, 0, 0, 0);
        buf.resize(n);
        n = WideCharToMultiByte(m_to_codepage, 0, pivot.data(),
                                pivot.size(), buf.data(), buf.size(),
                                0, 0);
    }
    return std::make_pair(res, std::string(buf.data(), n));
}

int StringConverterWin32::mbtowc(const char *s, size_t len, wchar_t *wc)
{
    for (size_t i = 1; i <= len && i <= m_cpinfo.MaxCharSize; ++i)
        if (MultiByteToWideChar(m_from_codepage, MB_ERR_INVALID_CHARS,
                                s, i, wc, 1))
            return i;
    return 0;
}

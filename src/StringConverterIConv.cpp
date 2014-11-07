#include <cstddef>
#include <vector>
#include <cerrno>
#include <stdexcept>
#include <iconv.h>
#include "StringConverterIConv.h"

StringConverterIConv::StringConverterIConv(const char *tocharset,
                                           const char *fromcharset)
{
    iconv_t cd = iconv_open(tocharset, fromcharset);
    if (cd == reinterpret_cast<iconv_t>(-1))
        throw std::runtime_error("iconv_open() failed");
    m_handle = std::shared_ptr<void>(cd, iconv_close);
}

std::pair<bool, std::string>
StringConverterIConv::convert(const std::string &s, bool flush)
{
    m_remainder += s;
    std::vector<char> dest(2);
    size_t iblen = m_remainder.size(),
           oblen = dest.size() - 1;
    char  *ip    = const_cast<char *>(m_remainder.data());
    char  *op    = dest.data();
    bool   res   = true;

    while (int(iconv(static_cast<iconv_t>(m_handle.get()),
                 &ip, &iblen, &op, &oblen)) == -1) {
        if (errno == E2BIG) {
            ptrdiff_t off = op - dest.data();
            dest.resize(dest.size() * 2);
            op    = dest.data() + off;
            oblen = dest.size() - off - 1;
        } else if (flush || errno == EILSEQ) {
            res = false;
            --iblen;
            --oblen;
            ip++;
            *op++ = '?';
        } else
            break;
    }
    ptrdiff_t off = ip - m_remainder.data();
    if (off == int(m_remainder.size()))
        m_remainder.clear();
    else
        m_remainder = m_remainder.substr(off);
    return std::make_pair(res, std::string(dest.data(), op - dest.data()));
}

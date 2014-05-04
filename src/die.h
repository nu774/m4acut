/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef DIE_H
#define DIE_H

#define DieIF(expr) \
    do { \
        if (expr) { \
            throw std::runtime_error(#expr); \
        } \
    } while (0)

void throw_file_error(const std::string &filename, const std::string &msg)
{
    std::string s = msg + ": " + filename;
    throw std::runtime_error(s);
}

#endif

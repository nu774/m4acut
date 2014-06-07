/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include "MP4Edits.h"
#include <limits>
#include <numeric>

uint64_t MP4Edits::total_duration() const
{
    return std::accumulate(m_edits.begin(), m_edits.end(), 0ULL,
                           [](uint64_t n, const entry_t &e) -> uint64_t {
                                return n + e.second;
                           });
}

unsigned MP4Edits::edit_for_position(int64_t position, int64_t *offset) const
{
    int64_t acc = 0;
    int64_t off = 0;
    size_t  i = 0;
    for (; i < m_edits.size(); ++i) {
        off =  position - acc;
        acc += m_edits[i].second;
        if (position < acc)
            break;
    }
    if (offset) *offset = off;
    return i == m_edits.size() ? i - 1 : i;
}

void MP4Edits::shift(int64_t offset, int64_t bound)
{
    std::vector<entry_t> new_edits;
    for (auto e = m_edits.begin(); e != m_edits.end(); ++e) {
        entry_t edit = *e;
        if (edit.first >= 0) {
            edit.first = edit.first + offset;
            if (edit.first < 0) {
                edit.second += edit.first;
                edit.first = 0;
            }
            if (edit.first + edit.second > bound)
                edit.second = bound - edit.first;
        }
        if (edit.second > 0)
            new_edits.push_back(edit);
    }
    m_edits.swap(new_edits);
}

void MP4Edits::crop(int64_t start, int64_t end)
{
    std::vector<entry_t> new_edits;
    int64_t acc = 0;
    for (auto e = m_edits.begin(); e != m_edits.end(); ++e) {
        if (acc < end && acc + e->second > start) {
            entry_t edit = *e;
            if (acc < start) {
                int64_t trim = start - acc;
                if (edit.first >= 0)
                    edit.first  += trim;
                edit.second -= trim;
            }
            if (acc + e->second > end)
                edit.second -= acc + e->second - end;
            new_edits.push_back(edit);
        }
        acc += e->second;
    }
    m_edits.swap(new_edits);
}

int64_t MP4Edits::minimum_media_position()
{
    int64_t candidate = std::numeric_limits<int64_t>::max(),
            limit     = candidate;
    for (auto e = m_edits.begin(); e != m_edits.end(); ++e)
        if (e->first >= 0 && e->first < candidate)
            candidate = e->first;
    return candidate == limit ? 0 : candidate;
}

int64_t MP4Edits::maximum_media_position()
{
    int64_t candidate = 0;
    for (auto e = m_edits.begin(); e != m_edits.end(); ++e)
        if (e->first >= 0 && e->first + e->second > candidate)
            candidate = e->first + e->second;
    return candidate;
}

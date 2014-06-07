/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef MP4Edits_H
#define MP4Edits_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

class MP4Edits {
    typedef std::pair<int64_t, int64_t> entry_t;
    std::vector<entry_t> m_edits;
public:
    void add_entry(int64_t offset, int64_t duration)
    {
        m_edits.push_back(std::make_pair(offset, duration));
    }
    size_t count() const { return m_edits.size(); }
    uint64_t total_duration() const;
    int64_t offset(unsigned edit_index) const
    {
        return m_edits[edit_index].first;
    }
    int64_t duration(unsigned edit_index) const
    {
        return m_edits[edit_index].second;
    }
    /*
     * get edit index which corresponds to given presentation position
     */
    unsigned edit_for_position(int64_t position, int64_t *offset=0) const;
    /*
     * get media offset which corresponds to presentation position
     */
    int64_t media_offset_for_position(int64_t position) const
    {
        int64_t  off;
        unsigned edit = edit_for_position(position, &off);
        return offset(edit) + off;
    }
    /*
     * slide media position of each edit by the given offset.
     * when an edit window is slided to the negative direction
     * and exceeds the origin, negative part is trimmed out.
     */
    void shift(int64_t offset);
    /*
     * crop edits by given presentation positions
     */
    void crop(int64_t start, int64_t end);
    int64_t minimum_media_position();
    int64_t maximum_media_position();
};

#endif

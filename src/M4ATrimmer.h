/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef M4ATrimmer_H
#define M4ATrimmer_H

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <stdexcept>
extern "C" {
#define LSMASH_DEMUXER_ENABLED
#include <lsmash.h>
}
#include "die.h"
#include "MP4Edits.h"

struct TimeSpec {
    bool is_samples;
    union {
        uint64_t samples;
        double   seconds;
    } value;
};

/* ad-hoc pool for storing metadata string */
class StringPool {
    /* use list so that elements won't get relocated */
    std::list<std::string> m_pool;
public:
    const char *append(const char *s)
    {
        m_pool.push_back(s);
        return m_pool.back().c_str();
    }
    const char *append(const char *s, size_t len)
    {
        m_pool.push_back(std::string(s, len));
        return m_pool.back().c_str();
    }
};

class M4ATrimmer {
    struct FileParameters: lsmash_file_parameters_t {
        FileParameters(const std::string &filename, int open_mode)
        {
            if (lsmash_open_file(filename.c_str(), open_mode, this) < 0)
                throw_file_error(filename, "cannot open");
        }
        ~FileParameters() { lsmash_close_file(this); }
    private:
        FileParameters(const FileParameters &);
        FileParameters &operator=(const FileParameters &);
    };
    struct Track {
        lsmash_track_parameters_t track_params;
        lsmash_media_parameters_t media_params;
        std::shared_ptr<lsmash_summary_t> summary;
        uint8_t  upsampled;             /*
                                         * 1: dual-rate SBR is stored in
                                         *    upsampled timescale
                                         * 0: oterwise
                                         */
        MP4Edits edits;

        Track(): upsampled(0)
        {
            memset(&track_params, 0, sizeof track_params);
            memset(&media_params, 0, sizeof media_params);
        }
        uint32_t id() const { return track_params.track_ID; }
        uint32_t timescale() const
        {
            return media_params.timescale >> upsampled;
        }
        uint64_t num_access_units() const
        {
            return ((media_params.duration >> upsampled) + 1023) / 1024;
        }
        uint64_t duration() const
        {
            return edits.total_duration();
        }
    };
    struct Input {
        std::shared_ptr<lsmash_root_t> movie;
        std::shared_ptr<FileParameters> file_params;
        lsmash_movie_parameters_t movie_params;
        Track track;
        std::vector<std::pair<double, std::string> > chapters;
        
        Input()
        {
            memset(&movie_params, 0, sizeof movie_params);
            memset(&file_params, 0, sizeof file_params);
        }
    };
    struct Output {
        std::shared_ptr<lsmash_root_t> movie;
        std::shared_ptr<FileParameters> file_params;
        uint32_t timescale;
        Track track;

        Output(): timescale(0)
        {
            memset(&file_params, 0, sizeof file_params);
        }
    };
    Input m_input;
    Output m_output;
    StringPool m_pool;
    std::map<std::pair<lsmash_itunes_metadata_item, std::string>,
             lsmash_itunes_metadata_t> m_itunes_metadata;
    uint64_t m_current_au;
    uint64_t m_cut_start;  /* in access unit, inclusive */
    uint64_t m_cut_end;    /* in access unit, exclusive */
public:
    M4ATrimmer() : m_current_au(0), m_cut_start(0), m_cut_end(0)
    {
    }
    void open_input(const std::string &filename);
    void open_output(const std::string &filename);
    const std::vector<std::pair<double, std::string> > &chapters() const
    {
        return m_input.chapters;
    }
    void select_cut_point(const TimeSpec &startspec, const TimeSpec &endspec);
    void select_chapter(unsigned nth);
    uint64_t num_access_units() const
    {
        return m_cut_end - m_cut_start;
    }
    uint32_t timescale() const
    {
        return m_input.track.timescale();
    }
    uint64_t duration() const
    {
        return m_input.track.duration();
    }
    bool copy_next_access_unit();
    void finish_write(lsmash_adhoc_remux_callback cb, void *cookie);
    void shift_edits(int64_t offset)
    {
        Track &t = m_input.track;
        t.edits.shift(offset, t.media_params.duration >> t.upsampled);
    }
    void set_text_tag(lsmash_itunes_metadata_item fcc, const std::string &s);
    void set_custom_tag(const std::string &name, const std::string &value);
    void set_int_tag(lsmash_itunes_metadata_item fcc, uint64_t value);
    void set_track_tag(unsigned index, unsigned total);
    void set_disk_tag(unsigned index, unsigned total);
private:
    std::shared_ptr<lsmash_root_t> new_movie()
    {
        lsmash_root_t *root;
        DieIF((root = lsmash_create_root()) == 0);
        return std::shared_ptr<lsmash_root_t>(root, lsmash_destroy_root);
    }
    uint32_t find_aac_track();
    void fetch_track_info(Track *t, uint32_t track_id);
    uint32_t retrieve_au_duration(uint32_t track_id);
    bool parse_iTunSMPB(const lsmash_itunes_metadata_t &item);
    void populate_itunes_metadata(const lsmash_itunes_metadata_t &item);
    void fetch_chapters()
    {
        uint32_t track_id = find_chapter_track();
        if (track_id) fetch_qt_chapters(track_id);
        else fetch_nero_chapters();
    }
    uint32_t find_chapter_track();
    void fetch_qt_chapters(uint32_t trakid);
    void fetch_nero_chapters();
    void add_audio_track();
    void set_iTunSMPB();
};

#endif

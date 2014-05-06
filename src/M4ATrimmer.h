/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef M4ATrimmer_H
#define M4ATrimmer_H

#include <cstdio>
#include <cstring>
#include <memory>
#include <algorithm>
#include <string>
#include <vector>
#include <list>
#include <stdexcept>
extern "C" {
#define LSMASH_DEMUXER_ENABLED
#include <lsmash.h>
}
#include "die.h"

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

struct M4ATrimmer {
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
        uint32_t media_offset;          /*
                                         * start offset in the media
                                         * in media timescale
                                         * downsampled if dual-rate SBR
                                         */
        uint64_t media_valid_duration;  /* 
                                         * valid duration in the media
                                         * in media timescale
                                         * downsampled if dual-rate SBR
                                         */

        Track(): upsampled(0), media_offset(0), media_valid_duration(0)
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
            if (media_valid_duration)
                return media_valid_duration;
            return (media_params.duration >> upsampled) - media_offset;
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
    std::vector<lsmash_itunes_metadata_t> m_itunes_metadata;
    uint64_t m_current_au;
    uint64_t m_cut_start;  /* in access unit, inclusive */
    uint64_t m_cut_end;    /* in access unit, exclusive */
public:
    M4ATrimmer() : m_current_au(0), m_cut_start(0), m_cut_end(0)
    {
    }
    void open_input(const std::string &filename)
    {
        m_input.movie = new_movie();
        lsmash_root_t *mov = m_input.movie.get();
        m_input.file_params = std::make_shared<FileParameters>(filename, 1);
        {
            lsmash_file_t *f;
            lsmash_file_parameters_t *fp = m_input.file_params.get();
            DieIF((f = lsmash_set_file(mov, fp)) == 0);
            if (lsmash_read_file(f, fp) < 0)
                throw_file_error(filename, "parse failed");
        }
        lsmash_initialize_movie_parameters(&m_input.movie_params);
        DieIF(lsmash_get_movie_parameters(mov, &m_input.movie_params));

        uint32_t track_id;
        if ((track_id = find_aac_track()) == 0)
            throw std::runtime_error("available track not found in the movie");
        fetch_track_info(&m_input.track, track_id);

        uint32_t num_metadata = lsmash_count_itunes_metadata(mov);
        for (uint32_t i = 0; i < num_metadata; ++i) {
            lsmash_itunes_metadata_t item;
            if (lsmash_get_itunes_metadata(mov, i + 1, &item))
                break;
            populate_itunes_metadata(item);
            lsmash_cleanup_itunes_metadata(&item);
        }
        fetch_chapters();
    }
    void open_output(const std::string &filename)
    {
        m_output.movie = new_movie();
        lsmash_root_t *mov = m_output.movie.get();
        m_output.file_params = std::make_shared<FileParameters>(filename, 0);
        {
            lsmash_file_parameters_t *ofp = m_output.file_params.get(),
                                     *ifp = m_input.file_params.get();

            ofp->major_brand   = ifp->major_brand;
            ofp->minor_version = ifp->minor_version;
            ofp->brands        = ifp->brands;
            ofp->brand_count   = ifp->brand_count;
            lsmash_file_t *f;
            DieIF((f = lsmash_set_file(mov, ofp)) == 0);
        }
        {
            lsmash_movie_parameters_t omp = m_input.movie_params;
            omp.timescale = m_input.track.timescale();
            DieIF(lsmash_set_movie_parameters(mov, &omp));
        }
        for (size_t i = 0; i < m_itunes_metadata.size(); ++i)
            lsmash_set_itunes_metadata(mov, m_itunes_metadata[i]);

        add_audio_track();
    }

    const std::vector<std::pair<double, std::string> > &chapters() const
    {
        return m_input.chapters;
    }

    /*
     * start_sec: in seconds, inclusive
     * end_sec:   in seconds, exclusive
     */
    void select_cut_point(const TimeSpec &startspec,
                          const TimeSpec &endspec)
    {
        int32_t off = m_input.track.media_offset;

        int64_t start = startspec.is_samples ?
            startspec.value.samples
          : startspec.value.seconds * m_input.track.timescale() + .5;
        int64_t end = endspec.is_samples ?
            endspec.value.samples
          : endspec.value.seconds * m_input.track.timescale() + .5;

        if (start > m_input.track.duration())
            throw std::runtime_error("the start position for trimming exceeds "
                                     "the length of input");
        m_cut_start = std::max(static_cast<int64_t>(0),
                               start + off - 1024) / 1024;
        m_output.track.media_offset = start + off - m_cut_start * 1024;

        if (end <= 0)
            end = m_input.track.duration();
        if (end <= start)
            throw std::runtime_error("the end position of trimming is before "
                                     "the start position");
        m_cut_end = (end + off + 1023) / 1024;
        /* for the sake of SBR, we extend padding if len(padding) < 481 */
        if (m_cut_end * 1024 - (end + off) < 481)
            ++m_cut_end;
        uint64_t num_au = m_input.track.num_access_units();
        if (m_cut_end > num_au) m_cut_end = num_au;

        int64_t duration_plus_padding =
            m_cut_end * 1024 - m_output.track.media_offset;
        m_output.track.media_valid_duration =
            std::min(end - start, duration_plus_padding);

        m_current_au = m_cut_start;
    }
    void select_chapter(unsigned nth)
    {
        if (nth >= m_input.chapters.size())
            throw std::runtime_error("chapter index out of range");
        TimeSpec start = { 0 }, end = { 0 };
        start.value.seconds = m_input.chapters[nth].first;
        if (nth < m_input.chapters.size() - 1)
            end.value.seconds = m_input.chapters[nth + 1].first;
        select_cut_point(start, end);
        write_title_tag(m_input.chapters[nth].second);
        write_track_tag(nth + 1, m_input.chapters.size());
    }
    uint64_t num_access_units() const
    {
        return m_cut_end - m_cut_start;
    }
    bool copy_next_access_unit()
    {
        if (m_current_au == m_cut_end)
            return false;
        lsmash_sample_t *sample =
            lsmash_get_sample_from_media_timeline(m_input.movie.get(),
                                                  m_input.track.id(),
                                                  m_current_au + 1);
        if (!sample) {
            /* treat as EOF, update duration */
            m_output.track.media_valid_duration =
                m_current_au * 1024 - m_output.track.media_offset;
            return false;
        }
        sample->dts = sample->cts = (m_current_au - m_cut_start) * 1024;
        /*
         * XXX: leaks a sample when lsmash_append_sample() fails.
         * Otherwise samples is deallocated internally by lsmash_append_sample()
         */
        DieIF(lsmash_append_sample(m_output.movie.get(),
                                   m_output.track.id(), sample));
        ++m_current_au;
        return true;
    }
    void finish_write(lsmash_adhoc_remux_callback cb, void *cookie)
    {
        DieIF(lsmash_flush_pooled_samples(m_output.movie.get(),
                                          m_output.track.id(), 1024));
        write_iTunSMPB();
        lsmash_adhoc_remux_t param;
        param.func = cb;
        param.buffer_size = 4 * 1024 * 1024;
        param.param = cookie;
        DieIF(lsmash_finish_movie(m_output.movie.get(), &param));
    }
private:
    std::shared_ptr<lsmash_root_t> new_movie()
    {
        lsmash_root_t *root;
        DieIF((root = lsmash_create_root()) == 0);
        return std::shared_ptr<lsmash_root_t>(root, lsmash_destroy_root);
    }
    uint32_t find_aac_track()
    {
        uint32_t track_id;
        lsmash_root_t *mov = m_input.movie.get();

        for (uint32_t i = 0; i < m_input.movie_params.number_of_tracks; ++i) {
            DieIF((track_id = lsmash_get_track_ID(mov, i + 1)) == 0);
            uint32_t ns = lsmash_count_summary(mov, track_id);
            /* ignore tracks having multiple sample descriptions */
            if (ns != 1)
                continue;
            lsmash_summary_t *summary;
            if ((summary = lsmash_get_summary(mov, track_id, 1)) == NULL)
                continue;
            if (summary->summary_type != LSMASH_SUMMARY_TYPE_AUDIO
             || !lsmash_check_codec_type_identical(summary->sample_type,
                                                   ISOM_CODEC_TYPE_MP4A_AUDIO))
                continue;
            lsmash_audio_summary_t *asummary =
                reinterpret_cast<lsmash_audio_summary_t*>(summary);
            if (asummary->aot != MP4A_AUDIO_OBJECT_TYPE_AAC_LC)
                continue;
            lsmash_cleanup_summary(summary);
            return track_id;
        }
        return 0;
    }
    void fetch_track_info(Track *t, uint32_t track_id)
    {
        lsmash_root_t *mov = m_input.movie.get();

        lsmash_initialize_track_parameters(&t->track_params);
        DieIF(lsmash_get_track_parameters(mov, track_id, &t->track_params));
        lsmash_initialize_media_parameters(&t->media_params);
        DieIF(lsmash_get_media_parameters(mov, track_id, &t->media_params));
        DieIF(lsmash_construct_timeline(mov, track_id));

        if (t->media_params.handler_type != ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK)
            return;

        lsmash_summary_t *summary;
        DieIF((summary = lsmash_get_summary(mov, track_id, 1)) == NULL);
        t->summary =
            std::shared_ptr<lsmash_summary_t>(summary,
                                              lsmash_cleanup_summary);

        uint32_t au_duration = retrieve_au_duration(track_id);
        if (au_duration != 1024 && au_duration != 2048)
            throw std::runtime_error("unexpected access unit duration");
        if (au_duration == 2048)
            t->upsampled = 1;

        if (lsmash_count_explicit_timeline_map(mov, track_id) == 1) {
            lsmash_edit_t edit;
            DieIF(lsmash_get_explicit_timeline_map(mov, track_id, 1, &edit));
            t->media_offset = edit.start_time >> t->upsampled;
            if (edit.duration > 0 && edit.duration < t->track_params.duration) {
                double duration = static_cast<double>(edit.duration);
                duration /= m_input.movie_params.timescale;
                duration *= t->timescale();
                t->media_valid_duration = static_cast<uint64_t>(duration + 0.5);
            }
        }
    }
    uint32_t retrieve_au_duration(uint32_t track_id)
    {
        /*
         * Sometimes the last delta can be subtracted by the amount of padding.
         * Therefore, we compute the first delta when possible and pick it.
         */
        lsmash_root_t *mov = m_input.movie.get();
        uint64_t delta = lsmash_get_last_sample_delta(mov, track_id);
        if (lsmash_get_sample_count_in_media_timeline(mov, track_id) > 1) {
            lsmash_sample_t *s1, *s2;
            s1 = lsmash_get_sample_from_media_timeline(mov, track_id, 1);
            s2 = lsmash_get_sample_from_media_timeline(mov, track_id, 2);
            if (s1 && s2) delta = s2->dts - s1->dts;
            if (s1) lsmash_delete_sample(s1);
            if (s2) lsmash_delete_sample(s2);
        }
        return static_cast<uint32_t>(delta);
    }
    bool parse_iTunSMPB(const lsmash_itunes_metadata_t &item)
    {
        if (item.item != ITUNES_METADATA_ITEM_CUSTOM
            || !item.meaning
            || std::strcmp(item.meaning, "com.apple.iTunes")
            || !item.name
            || std::strcmp(item.name, "iTunSMPB"))
            return false;

        const char *s;
        size_t len;
        if (item.type == ITUNES_METADATA_TYPE_STRING) {
            s = item.value.string;
            len = strlen(s);
        } else if (item.type == ITUNES_METADATA_TYPE_BINARY) {
            s = reinterpret_cast<char *>(item.value.binary.data);
            len = item.value.binary.size;
        } else
            return true;

        std::stringstream ss(std::string(s, len));
        uint32_t junk, priming, padding;
        uint64_t duration;
        if (ss >> std::hex >> junk
               >> std::hex >> priming
               >> std::hex >> padding
               >> std::hex >> duration)
        {
            unsigned shift = m_input.track.upsampled;
            m_input.track.media_offset = priming >> shift;
            m_input.track.media_valid_duration = duration >> shift;
        }
        return true;
    }
    void populate_itunes_metadata(const lsmash_itunes_metadata_t &item)
    {
        lsmash_itunes_metadata_t res = item;

        if (parse_iTunSMPB(item))
            return;
        if (item.meaning)
            res.meaning = const_cast<char*>(m_pool.append(res.meaning));
        if (item.name)
            res.name = const_cast<char*>(m_pool.append(res.name));

        if (item.type == ITUNES_METADATA_TYPE_STRING)
            res.value.string = 
                const_cast<char*>(m_pool.append(res.value.string));
        else if (item.type == ITUNES_METADATA_TYPE_BINARY) {
            res.value.binary = res.value.binary;
            const char *d = reinterpret_cast<char*>(res.value.binary.data);
            d  = m_pool.append(d, res.value.binary.size);
            res.value.binary.data =
                reinterpret_cast<uint8_t*>(const_cast<char*>(d));
        }
        m_itunes_metadata.push_back(res);
    }

    void fetch_chapters()
    {
        uint32_t track_id = find_chapter_track();
        if (track_id) fetch_qt_chapters(track_id);
        else fetch_nero_chapters();
    }
    uint32_t find_chapter_track()
    {
        uint32_t track_id;
        lsmash_root_t *mov = m_input.movie.get();

        for (uint32_t i = 0; i < m_input.movie_params.number_of_tracks; ++i) {
            lsmash_media_parameters_t params;
            DieIF((track_id = lsmash_get_track_ID(mov, i + 1)) == 0);
            DieIF(lsmash_get_media_parameters(mov, track_id, &params));
            if (params.handler_type == ISOM_MEDIA_HANDLER_TYPE_TEXT_TRACK)
                return track_id;
        }
        return 0;
    }
    void fetch_qt_chapters(uint32_t trakid)
    {
        lsmash_root_t *mov = m_input.movie.get();
        lsmash_sample_t *sample;
        Track t;
        fetch_track_info(&t, trakid);
        for (uint32_t i = 1;
             (sample = lsmash_get_sample_from_media_timeline(mov, trakid, i));
             ++i)
        {
            int len = ((sample->data[0] << 8) | sample->data[1]);
            double timestamp = static_cast<double>(sample->cts);
            timestamp /= t.media_params.timescale;
            const char *s = reinterpret_cast<char*>(sample->data + 2);
            std::string title = std::string(s, len);
            m_input.chapters.push_back(std::make_pair(timestamp, title));
            lsmash_delete_sample(sample);
        }
    }
    void fetch_nero_chapters()
    {
        double start_time = 0;
        for (uint32_t i = 1; ; ++i) {
            double ss;
            char *title = lsmash_get_tyrant_chapter(m_input.movie.get(),
                                                    i, &ss);
            if (!title) break;
            if (i == 1) start_time = ss;
            auto p = std::make_pair(ss - start_time, std::string(title));
            m_input.chapters.push_back(p);
        }
        if (start_time && !m_input.track.media_offset) {
            m_input.track.media_offset = m_input.track.timescale() * start_time;
        }
    }
    void add_audio_track()
    {
        lsmash_root_t *mov = m_output.movie.get();
        m_output.track = m_input.track;

        uint32_t trakid;
        DieIF(!(trakid =
                lsmash_create_track(mov, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK)));
        m_output.track.track_params.track_ID = trakid;
        DieIF(lsmash_set_track_parameters(mov, trakid,
                                          &m_output.track.track_params));
        m_output.track.upsampled = 0;
        m_output.track.media_params.timescale = m_input.track.timescale();
        DieIF(lsmash_set_media_parameters(mov, trakid,
                                          &m_output.track.media_params));
        DieIF(!lsmash_add_sample_entry(mov, trakid,
                                       m_input.track.summary.get()));
    }
    void write_title_tag(const std::string &title)
    {
        lsmash_itunes_metadata_t tag;
        memset(&tag, 0, sizeof tag);
        tag.item = ITUNES_METADATA_ITEM_TITLE;
        tag.value.string = const_cast<char *>(title.c_str());
        lsmash_set_itunes_metadata(m_output.movie.get(), tag);
    }
    void write_track_tag(unsigned index, unsigned total)
    {
        uint8_t data[8] = { 0 };
        data[2] = index >> 8;
        data[3] = index & 0xff;
        data[4] = total >> 8;
        data[5] = total & 0xff;

        lsmash_itunes_metadata_t tag;
        memset(&tag, 0, sizeof tag);
        tag.item = ITUNES_METADATA_ITEM_TRACK_NUMBER;
        tag.value.binary.subtype = ITUNES_METADATA_SUBTYPE_IMPLICIT;
        tag.value.binary.size = 8;
        tag.value.binary.data = data;
        lsmash_set_itunes_metadata(m_output.movie.get(), tag);
    }
    void write_iTunSMPB()
    {
        const char *fmt = " 00000000 %08X %08X %08X%08X 00000000 00000000 "
            "00000000 00000000 00000000 00000000 00000000 00000000";
        char buf[256];

        uint64_t total_duration = (m_current_au - m_cut_start) * 1024;
        uint32_t padding =
            total_duration - m_output.track.media_offset 
                           - m_output.track.media_valid_duration;
        std::sprintf(buf, fmt, m_output.track.media_offset, padding,
                     int(m_output.track.media_valid_duration >> 32),
                     int(m_output.track.media_valid_duration & 0xffffffff));

        lsmash_itunes_metadata_t tag;
        memset(&tag, 0, sizeof tag);
        tag.item = ITUNES_METADATA_ITEM_CUSTOM;
        tag.type = ITUNES_METADATA_TYPE_STRING;
        tag.meaning = const_cast<char *>("com.apple.iTunes");
        tag.name = const_cast<char *>("iTunSMPB");
        tag.value.string = buf;
        lsmash_set_itunes_metadata(m_output.movie.get(), tag);
    }
};

#endif

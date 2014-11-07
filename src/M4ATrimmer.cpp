/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#if HAVE_CONFIG_H
# include "config.h"
#endif
#include "M4ATrimmer.h"
#include <sstream>

void M4ATrimmer::open_input(const std::string &filename)
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
        if (!parse_iTunSMPB(item))
            populate_itunes_metadata(item);
        lsmash_cleanup_itunes_metadata(&item);
    }
    fetch_chapters();
    if (!m_input.track.edits.count()) {
        int64_t duration =
            m_input.track.media_params.duration >> m_input.track.upsampled;
        m_input.track.edits.add_entry(0, duration);
    }
}

void M4ATrimmer::open_output(const std::string &filename)
{
    m_output.movie = new_movie();
    lsmash_root_t *mov = m_output.movie.get();
    m_output.file_params = std::make_shared<FileParameters>(filename, 0);
    {
        lsmash_file_parameters_t *ofp = m_output.file_params.get(),
                                 *ifp = m_input.file_params.get();

        ofp->major_brand   = ISOM_BRAND_TYPE_M4A;
        ofp->minor_version = ifp->minor_version;
        ofp->brands        = ifp->brands;
        ofp->brand_count   = ifp->brand_count;
        lsmash_file_t *f;
        DieIF((f = lsmash_set_file(mov, ofp)) == 0);
    }
    {
        lsmash_movie_parameters_t omp;
        lsmash_initialize_movie_parameters(&omp);
        omp.timescale = m_input.track.timescale();
        DieIF(lsmash_set_movie_parameters(mov, &omp));
    }
    add_audio_track();
}

void M4ATrimmer::select_cut_point(const TimeSpec &startspec,
                                  const TimeSpec &endspec)
{
    int64_t start = startspec.is_samples ?
        startspec.value.samples >> m_input.track.upsampled
      : startspec.value.seconds * m_input.track.timescale() + .5;
    int64_t end = endspec.is_samples ?
        endspec.value.samples >> m_input.track.upsampled
      : endspec.value.seconds * m_input.track.timescale() + .5;

    if (start > int64_t(m_input.track.duration()))
        throw std::runtime_error("the start position for trimming exceeds "
                                 "the length of input");
    if (end <= 0)
        end = m_input.track.duration();
    if (end <= start)
        throw std::runtime_error("the end position of trimming is before "
                                 "the start position");

    m_output.track.edits = m_input.track.edits;
    MP4Edits &edits = m_output.track.edits;
    edits.crop(start, end);
    int64_t media_start = edits.minimum_media_position();
    int64_t media_end   = edits.maximum_media_position();
    m_cut_start = m_current_au =
        std::max(static_cast<int64_t>(0), media_start - 1024) / 1024;
    m_cut_end = (media_end + 1023) / 1024 + 1;
    uint64_t num_au = m_input.track.num_access_units();
    if (m_cut_end > num_au) m_cut_end = num_au;
    if (m_cut_start > 0)
        edits.shift(-1 * m_cut_start * 1024);

    unsigned count = m_output.track.edits.count();
    for (unsigned i = 0; i < count; ++i) {
        lsmash_edit_t edit = { 0 };
        edit.duration   = m_output.track.edits.duration(i);
        edit.start_time = m_output.track.edits.offset(i);
        edit.rate       = ISOM_EDIT_MODE_NORMAL;
        lsmash_create_explicit_timeline_map(m_output.movie.get(),
                                            m_output.track.id(), edit);
    }
}

void M4ATrimmer::select_chapter(unsigned nth)
{
    if (nth >= m_input.chapters.size())
        throw std::runtime_error("chapter index out of range");
    TimeSpec start = { 0 }, end = { 0 };
    start.value.seconds = m_input.chapters[nth].first;
    if (nth < m_input.chapters.size() - 1)
        end.value.seconds = m_input.chapters[nth + 1].first;
    select_cut_point(start, end);
    set_text_tag(ITUNES_METADATA_ITEM_TITLE, m_input.chapters[nth].second);
    set_track_tag(nth + 1, m_input.chapters.size());
}

bool M4ATrimmer::copy_next_access_unit()
{
    if (m_current_au == m_cut_end)
        return false;
    lsmash_sample_t *sample =
        lsmash_get_sample_from_media_timeline(m_input.movie.get(),
                                              m_input.track.id(),
                                              m_current_au + 1);
    if (!sample)
        return false;
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

void M4ATrimmer::finish_write(lsmash_adhoc_remux_callback cb, void *cookie)
{
    lsmash_root_t *mov = m_output.movie.get();
    DieIF(lsmash_flush_pooled_samples(mov, m_output.track.id(), 1024));
    if (m_output.track.edits.count() == 1)
        set_iTunSMPB();
    for (auto e = m_itunes_metadata.begin(); e != m_itunes_metadata.end(); ++e)
        lsmash_set_itunes_metadata(mov, e->second);

    lsmash_adhoc_remux_t param;
    param.func = cb;
    param.buffer_size = 4 * 1024 * 1024;
    param.param = cookie;
    DieIF(lsmash_finish_movie(mov, &param));
}

uint32_t M4ATrimmer::find_aac_track()
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

void M4ATrimmer::fetch_track_info(Track *t, uint32_t track_id)
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
        std::shared_ptr<lsmash_summary_t>(summary, lsmash_cleanup_summary);

    uint32_t au_duration = retrieve_au_duration(track_id);
    if (au_duration != 1024 && au_duration != 2048)
        throw std::runtime_error("unexpected access unit duration");
    if (au_duration == 2048)
        t->upsampled = 1;

    if (m_input.movie_params.timescale >= t->media_params.timescale) {
        uint32_t nedits = lsmash_count_explicit_timeline_map(mov, track_id);
        for (uint32_t i = 1; i <= nedits; ++i) {
            lsmash_edit_t edit;
            DieIF(lsmash_get_explicit_timeline_map(mov, track_id, i, &edit));
            double duration = static_cast<double>(edit.duration);
            duration /= m_input.movie_params.timescale;
            duration *= t->media_params.timescale;
            if (duration == 0.0)
                duration = t->media_params.duration - edit.start_time;
            t->edits.add_entry(edit.start_time >> t->upsampled,
                               int64_t(duration + .5) >> t->upsampled);
        }
    }
}

uint32_t M4ATrimmer::retrieve_au_duration(uint32_t track_id)
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

bool M4ATrimmer::parse_iTunSMPB(const lsmash_itunes_metadata_t &item)
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

    if (m_input.track.edits.count())
        return true;

    std::stringstream ss(std::string(s, len));
    uint32_t junk, priming, padding;
    uint64_t duration;
    if (ss >> std::hex >> junk
           >> std::hex >> priming
           >> std::hex >> padding
           >> std::hex >> duration)
    {
        priming  >>= m_input.track.upsampled;
        duration >>= m_input.track.upsampled;
        m_input.track.edits.add_entry(priming, duration);
    }
    return true;
}

void M4ATrimmer::populate_itunes_metadata(const lsmash_itunes_metadata_t &item)
{
    lsmash_itunes_metadata_t res = item;

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
    auto k = std::make_pair(res.item,
                            res.name ? std::string(res.name) : std::string());
    m_itunes_metadata[k] = res;
}

uint32_t M4ATrimmer::find_chapter_track()
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

void M4ATrimmer::fetch_qt_chapters(uint32_t trakid)
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

void M4ATrimmer::fetch_nero_chapters()
{
    double start_time = 0;
    for (uint32_t i = 1; ; ++i) {
        double ss;
        char *title = lsmash_get_tyrant_chapter(m_input.movie.get(), i, &ss);
        if (!title) break;
        if (i == 1) start_time = ss;
        auto p = std::make_pair(ss - start_time, std::string(title));
        m_input.chapters.push_back(p);
    }
    if (start_time && !m_input.track.edits.count()) {
        int64_t off = m_input.track.timescale() * start_time + .5;
        int64_t duration =
            m_input.track.media_params.duration >> m_input.track.upsampled;
        duration -= off;
        m_input.track.edits.add_entry(off, duration);
    }
}

void M4ATrimmer::add_audio_track()
{
    lsmash_root_t *mov = m_output.movie.get();
    m_output.track.track_params = m_input.track.track_params;
    m_output.track.media_params = m_input.track.media_params;

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
    DieIF(!lsmash_add_sample_entry(mov, trakid, m_input.track.summary.get()));
}

void M4ATrimmer::set_text_tag(lsmash_itunes_metadata_item fcc,
                              const std::string &s)
{
    lsmash_itunes_metadata_t tag;
    memset(&tag, 0, sizeof tag);
    tag.item         = fcc;
    tag.type         = ITUNES_METADATA_TYPE_STRING;
    tag.value.string = const_cast<char *>(s.c_str());
    populate_itunes_metadata(tag);
}

void M4ATrimmer::set_custom_tag(const std::string &name,
                                const std::string &value)
{
    lsmash_itunes_metadata_t tag;
    memset(&tag, 0, sizeof tag);
    tag.item         = ITUNES_METADATA_ITEM_CUSTOM;
    tag.type         = ITUNES_METADATA_TYPE_STRING;
    tag.meaning      = const_cast<char *>("com.apple.iTunes");
    tag.name         = const_cast<char *>(name.c_str());
    tag.value.string = const_cast<char *>(value.c_str());
    populate_itunes_metadata(tag);
}

void M4ATrimmer::set_int_tag(lsmash_itunes_metadata_item fcc, uint64_t value)
{
    lsmash_itunes_metadata_t tag;
    memset(&tag, 0, sizeof tag);
    tag.item          = fcc;
    tag.type          = ITUNES_METADATA_TYPE_INTEGER;
    tag.value.integer = value;
    populate_itunes_metadata(tag);
}

void M4ATrimmer::set_track_tag(unsigned index, unsigned total)
{
    uint8_t data[8] = { 0 };
    data[2] = index >> 8;
    data[3] = index & 0xff;
    data[4] = total >> 8;
    data[5] = total & 0xff;

    lsmash_itunes_metadata_t tag;
    memset(&tag, 0, sizeof tag);
    tag.item                 = ITUNES_METADATA_ITEM_TRACK_NUMBER;
    tag.type                 = ITUNES_METADATA_TYPE_BINARY;
    tag.value.binary.subtype = ITUNES_METADATA_SUBTYPE_IMPLICIT;
    tag.value.binary.size    = 8;
    tag.value.binary.data    = data;
    populate_itunes_metadata(tag);
}

void M4ATrimmer::set_disk_tag(unsigned index, unsigned total)
{
    uint8_t data[6] = { 0 };
    data[2] = index >> 8;
    data[3] = index & 0xff;
    data[4] = total >> 8;
    data[5] = total & 0xff;

    lsmash_itunes_metadata_t tag;
    memset(&tag, 0, sizeof tag);
    tag.item                 = ITUNES_METADATA_ITEM_DISC_NUMBER;
    tag.type                 = ITUNES_METADATA_TYPE_BINARY;
    tag.value.binary.subtype = ITUNES_METADATA_SUBTYPE_IMPLICIT;
    tag.value.binary.size    = 6;
    tag.value.binary.data    = data;
    populate_itunes_metadata(tag);
}

void M4ATrimmer::set_iTunSMPB()
{
    const char *fmt = " 00000000 %08X %08X %08X%08X 00000000 00000000 "
        "00000000 00000000 00000000 00000000 00000000 00000000";
    char buf[256];

    uint64_t total_duration = (m_current_au - m_cut_start) * 1024;
    unsigned offset   = m_output.track.edits.offset(0);
    uint64_t duration = m_output.track.edits.duration(0); 
    int32_t padding = total_duration - offset - duration;
    if (padding < 0) {
        padding = 0;
        duration += padding;
    }
    std::sprintf(buf, fmt, offset, padding, int(duration >> 32),
                 int(duration & 0xffffffff));
    set_custom_tag("iTunSMPB", buf);
}


#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <locale>
#include <stdexcept>
#include "cuesheet.h"

static inline
unsigned msf2frames(unsigned mm, unsigned ss, unsigned ff)
{
    return (mm * 60 + ss) * 75 + ff;
}

static inline
uint64_t frame2sample(double sampling_rate, uint32_t nframe)
{
    return static_cast<uint64_t>(nframe / 75.0 * sampling_rate + 0.5);
}

template <typename CharT>
bool CueTokenizer<CharT>::nextline()
{
    m_fields.clear();
    int_type c;
    std::basic_string<CharT> field;
    while (traits_type::not_eof(c = m_sb->sbumpc())) {
        if (c == '"') {
            // eat until closing quote
            while (traits_type::not_eof(c = m_sb->sbumpc())) {
                if (c == '\n') {
                    char buf[128];
                    std::sprintf(buf, "cuesheet: runaway string at line %u",
                                 m_lineno + 1);
                    throw std::runtime_error(buf);
                } else if (c != '"')
                    field.push_back(c);
                else if (m_sb->sgetc() != '"') // closing quote
                    break;
                else { // escaped quote
                    m_sb->snextc();
                    field.push_back(c);
                }
            }
        }
        else if (c == '\n') {
            ++m_lineno;
            break;
        }
        else if (std::strchr(" \r\t", c)) {
            if (field.size()) {
                m_fields.push_back(field);
                field.clear();
            }
            while (std::strchr(" \r\t", m_sb->sgetc()))
                m_sb->snextc();
        }
        else
            field.push_back(c);
    }
    if (field.size()) m_fields.push_back(field);
    return m_fields.size() > 0 || c == '\n';
}

template struct CueTokenizer<char>;

void CueTrack::add_segment(const CueSegment &seg)
{
    if (m_segments.size()) {
        CueSegment &last = m_segments.back();
        if (last.m_index >= seg.m_index) {
            char msg[256];
            if (last.m_index == 0x7fffffff)
                std::sprintf(msg, "cuesheet: conflicting use of INDEX00/PREGAP"
                             " found on track %u-%u", m_number, m_number + 1);
            else
                std::sprintf(msg, "cuesheet: INDEX shall be in strictly "
                             "ascending order: track %u", m_number);
            throw std::runtime_error(msg);
        }
        if (last.m_filename == seg.m_filename && last.m_end == seg.m_begin)
        {
            last.m_end = seg.m_end;
            return;
        }
    }
    m_segments.push_back(seg);
}

void CueTrack::get_tags(std::map<std::string, std::string> *tags) const
{
    std::map<std::string, std::string> result;
    m_cuesheet->get_tags(&result);
    std::for_each(m_meta.begin(), m_meta.end(),
                  [&](decltype(*m_meta.begin()) &tag) {
                      auto key = tag.first;
                      if (key == "PERFORMER")
                          key = "ARTIST";
                      result[key] = tag.second;
                  });
    char buf[32];
    std::sprintf(buf, "%u/%u", number(), m_cuesheet->count());
    result["TRACK"] = buf;
    tags->swap(result);
}

void CueSheet::parse(std::streambuf *src)
{
    static struct handler_t {
        const char *cmd;
        void (CueSheet::*mf)(const std::string *args);
        size_t nargs;
    } handlers[] = {
        { "FILE",       &CueSheet::parse_file,    3 },
        { "TRACK",      &CueSheet::parse_track,   3 },
        { "INDEX",      &CueSheet::parse_index,   3 },
        { "POSTGAP",    &CueSheet::parse_postgap, 2 },
        { "PREGAP",     &CueSheet::parse_pregap,  2 },
        { "REM",        &CueSheet::parse_rem,     3 },
        { "CATALOG",    &CueSheet::parse_meta,    2 },
        { "ISRC",       &CueSheet::parse_meta,    2 },
        { "PERFORMER",  &CueSheet::parse_meta,    2 },
        { "SONGWRITER", &CueSheet::parse_meta,    2 },
        { "TITLE",      &CueSheet::parse_meta,    2 },
        { 0, 0, 0 }
    };

    CueTokenizer<char> tokenizer(src);
    while (tokenizer.nextline()) {
        if (!tokenizer.m_fields.size())
            continue;
        m_lineno = tokenizer.m_lineno;
        std::string cmd = tokenizer.m_fields[0];
        for (handler_t *p = handlers; p->cmd; ++p) {
            if (cmd != p->cmd)
                continue;
            if (tokenizer.m_fields.size() == p->nargs)
                (this->*p->mf)(&tokenizer.m_fields[0]);
            else if (cmd != "REM") {
                char msg[128];
                std::sprintf(msg, "wrong num ars for %s command", p->cmd);
                die(msg);
            }
            break;
        }
        // if (!p->cmd) die("Unknown command");
    }
    validate();
}

void CueSheet::as_chapters(double duration,
                           std::vector<chapter_entry_t> *chapters) const
{
    if (m_has_multiple_files)
        throw std::runtime_error("Multiple FILE present in cuesheet");

    std::vector<chapter_entry_t> chaps;
    unsigned tbeg, tend, last_end = 0;
    std::for_each(begin(), end(), [&](const CueTrack &track) {
        tbeg = track.begin()->m_begin;
        tend = track.begin()->m_end;
        double track_duration;
        if (tend != ~0U)
            track_duration = (tend - tbeg) / 75.0;
        else
            track_duration = duration - (last_end / 75.0);
        std::string title = track.name();
        if (title == "") {
            char buf[64];
            std::sprintf(buf, "Track %02d", track.number());
            title = buf;
        }
        chaps.push_back(std::make_pair(track_duration, title));
        last_end = tend;
    });
    chapters->swap(chaps);
}

void CueSheet::get_tags(std::map<std::string, std::string> *tags) const
{
    std::map<std::string, std::string> result;
    std::for_each(m_meta.begin(), m_meta.end(),
                  [&](decltype(*m_meta.begin()) &tag) {
        if (tag.first == "PERFORMER") {
            result["ARTIST"] = tag.second;
            result["ALBUMARTIST"] = tag.second;
        } else if (tag.first == "TITLE")
            result["ALBUM"] = tag.second;
        else if (tag.first != "DISCNUMBER" && tag.first != "TOTALDISCS")
            result[tag.first] = tag.second;
    });
    if (m_meta.find("DISCNUMBER") != m_meta.end()
     && m_meta.find("TOTALDISCS") != m_meta.end()) {
        std::string sdn = m_meta.find("DISCNUMBER")->first.c_str();
        std::string std = m_meta.find("TOTALDISCS")->first.c_str();
        unsigned dn = 0, td = 0;
        if (std::sscanf(sdn.c_str(), "%u", &dn) == 1
         && std::sscanf(std.c_str(), "%u", &td) == 1) {
            char buf[64];
            sprintf(buf, "%u/%u", dn, td);
            result["DISC"] = std::string(buf);
        }
    }
    tags->swap(result);
}

void CueSheet::validate()
{
    auto index1_missing =
        [](const CueTrack &track) {
            return !std::count_if(track.begin(), track.end(),
                                  [](const CueSegment &seg) {
                                      return seg.m_index == 1;
                                  });
        };
    auto track = std::find_if(begin(), end(), index1_missing);
    if (track != end()) {
        char msg[128];
        sprintf(msg, "cuesheet: INDEX 01 not found on track %u",
                track->number());
        throw std::runtime_error(msg);
    };
}

void CueSheet::parse_file(const std::string *args)
{
    if (!m_cur_file.empty() && m_cur_file != args[1])
        this->m_has_multiple_files = true;
    m_cur_file = args[1];
}
void CueSheet::parse_track(const std::string *args)
{
    if (args[2] == "AUDIO") {
        unsigned no;
        if (std::sscanf(args[1].c_str(), "%d", &no) != 1)
            die("Invalid TRACK number");
        m_tracks.push_back(CueTrack(this, no));
    }
}
void CueSheet::parse_index(const std::string *args)
{
    if (!m_tracks.size())
        die("INDEX command before TRACK");
    if (m_cur_file.empty())
        die("INDEX command before FILE");
    unsigned no, mm, ss, ff, nframes;
    if (std::sscanf(args[1].c_str(), "%u", &no) != 1)
        die("Invalid INDEX number");
    if (std::sscanf(args[2].c_str(), "%u:%u:%u", &mm, &ss, &ff) != 3)
        die("Invalid INDEX time format");
    if (ss > 59 || ff > 74)
        die("Invalid INDEX time format");
    nframes = msf2frames(mm, ss, ff);
    CueSegment *lastseg = last_segment();
    if (lastseg && lastseg->m_filename == m_cur_file) {
        lastseg->m_end = nframes;
        if (lastseg->m_begin >= nframes)
            die("INDEX time must be in ascending order");
    }
    CueSegment segment(m_cur_file, no);
    segment.m_begin = nframes;
    if (no > 0)
        m_tracks.back().add_segment(segment);
    else {
        if (m_tracks.size() == 1) {
            /* HTOA */
            m_tracks.insert(m_tracks.begin(), CueTrack(this, 0));
            m_tracks[0].set_meta("title", "(HTOA)");
            segment.m_index = 1;
        } else
            segment.m_index = 0x7fffffff;
        m_tracks[m_tracks.size() - 2].add_segment(segment);
    }
}
void CueSheet::parse_postgap(const std::string *args)
{
    if (!m_tracks.size())
        die("POSTGAP command before TRACK");
    unsigned mm, ss, ff;
    if (std::sscanf(args[1].c_str(), "%u:%u:%u", &mm, &ss, &ff) != 3)
        die("Invalid POSTGAP time format");
    CueSegment segment(std::string("__GAP__"), 0x7ffffffe);
    segment.m_end = msf2frames(mm, ss, ff);
    m_tracks.back().add_segment(segment);
}
void CueSheet::parse_pregap(const std::string *args)
{
    if (!m_tracks.size())
        die("PREGAP command before TRACK");
    unsigned mm, ss, ff;
    if (std::sscanf(args[1].c_str(), "%u:%u:%u", &mm, &ss, &ff) != 3)
        die("Invalid PREGAP time format");
    CueSegment segment(std::string("__GAP__"), 0x7fffffff);
    segment.m_end = msf2frames(mm, ss, ff);
    if (m_tracks.size() > 1)
        m_tracks[m_tracks.size() - 2].add_segment(segment);
}
void CueSheet::parse_meta(const std::string *args)
{
    if (m_tracks.size())
        m_tracks.back().set_meta(args[0], args[1]);
    else
        m_meta[args[0]] = args[1];
}

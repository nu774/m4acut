#include <streambuf>
#include <istream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

template <typename CharT>
struct CueTokenizer {
    typedef std::char_traits<CharT> traits_type;
    typedef typename traits_type::int_type int_type;

    explicit CueTokenizer(std::basic_streambuf<CharT> *sb)
        : m_sb(sb), m_lineno(0)
    {}
    bool nextline();

    std::basic_streambuf<CharT> *m_sb;
    std::vector<std::basic_string<CharT> > m_fields;
    unsigned m_lineno;
};

struct CueSegment {
    CueSegment(const std::string &filename, unsigned index)
        : m_filename(filename), m_index(index), m_begin(0), m_end(~0U)
    {}
    std::string m_filename;
    unsigned m_index;
    unsigned m_begin;
    unsigned m_end;
};

class CueSheet;

class CueTrack {
    CueSheet *m_cuesheet;
    unsigned m_number;
    std::vector<CueSegment> m_segments;
    std::map<std::string, std::string> m_meta;
public:
    typedef std::vector<CueSegment>::iterator iterator;
    typedef std::vector<CueSegment>::const_iterator const_iterator;

    CueTrack(CueSheet *cuesheet, unsigned number)
        : m_cuesheet(cuesheet), m_number(number) {}
    std::string name() const
    {
        auto p = m_meta.find("TITLE");
        return p == m_meta.end() ? "" : p->second;
    }
    unsigned number() const { return m_number; }
    void add_segment(const CueSegment &seg);
    void set_meta(const std::string &key, const std::string &value)
    {
        m_meta[key] = value;
    }
    void get_tags(std::map<std::string, std::string> *tags) const;

    iterator begin() { return m_segments.begin(); }
    iterator end() { return m_segments.end(); }
    const_iterator begin() const { return m_segments.begin(); }
    const_iterator end() const { return m_segments.end(); }
    CueSegment *last_segment()
    {
        return m_segments.size() ? &m_segments.back() : 0;
    }
};

class CueSheet {
    bool m_has_multiple_files;
    size_t m_lineno;
    std::string m_cur_file;
    std::vector<CueTrack> m_tracks;
    std::map<std::string, std::string> m_meta;
public:
    typedef std::vector<CueTrack>::iterator iterator;
    typedef std::vector<CueTrack>::const_iterator const_iterator;
    typedef std::pair<double, std::string> chapter_entry_t;

    CueSheet(): m_has_multiple_files(false) {}
    void parse(std::streambuf *src);
    void as_chapters(double duration, /* total duration in sec. */
                     std::vector<chapter_entry_t> *chapters) const;
    void get_tags(std::map<std::string, std::string> *tags) const;

    unsigned count() const { return m_tracks.size(); }
    iterator begin() { return m_tracks.begin(); }
    iterator end() { return m_tracks.end(); }
    const_iterator begin() const { return m_tracks.begin(); }
    const_iterator end() const { return m_tracks.end(); }
private:
    void validate();
    void parse_file(const std::string *args);
    void parse_track(const std::string *args);
    void parse_index(const std::string *args);
    void parse_postgap(const std::string *args);
    void parse_pregap(const std::string *args);
    void parse_meta(const std::string *args);
    void parse_rem(const std::string *args) { parse_meta(args + 1); }
    void die(const std::string &msg)
    {
        std::stringstream ss;
        ss << "cuesheet: " << msg << " at line " << m_lineno;
        throw std::runtime_error(ss.str());
    }
    CueSegment *last_segment()
    {
        CueSegment *seg;
        for (int i = m_tracks.size() - 1; i >= 0; --i)
            if ((seg = m_tracks[i].last_segment()) != 0)
                return seg;
        return 0;
    }
};

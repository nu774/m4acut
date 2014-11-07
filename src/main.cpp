/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#if HAVE_CONFIG_H
# include "config.h"
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <string>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <getopt.h>
#include "M4ATrimmer.h"
#include "compat.h"
#include "cuesheet.h"
#if HAVE_ICONV
# include "StringConverterIConv.h"
#elif defined(_WIN32)
# include "StringConverterWin32.h"
#else
# include "StringConverterUTF8.h"
#endif
#include "version.h"

namespace {

struct params_t {
    const char *ifilename;
    const char *ofilename;
    const char *cuesheet;
    const char *cuesheet_encoding;
    TimeSpec start;
    TimeSpec end;
    bool chapter_mode;
    int  sbr_delay_fix;
};

std::string safe_filename(const std::string &s)
{
    std::string result;
    std::transform(s.begin(), s.end(), std::back_inserter(result),
                   [](char c) -> char {
                       return strchr(":/\\?|<>*\"", c) ? '_' : c;
                   });
    return result.substr(0, 240);
}

bool parse_timespec(const char *spec, TimeSpec *result)
{
    unsigned hh, mm, x;
    char a, _;
    double ss;
    if (!spec || !*spec)
        return false;
    if (std::sscanf(spec, "%u%c%c", &x, &a, &_) == 2 && a == 's')
    {
        result->value.samples = x;
        result->is_samples = true;
        return true;
    }
    if (std::sscanf(spec, "%u:%u:%lf%c", &hh, &mm, &ss, &_) == 3)
        ss = ss + ((hh * 60.0) + mm) * 60.0;
    else if (std::sscanf(spec, "%u:%lf%c", &mm, &ss, &_) == 2)
        ss = ss + mm * 60.0;
    else if (std::sscanf(spec, "%lf%c", &ss, &_) != 1)
        return false;

    result->is_samples = false;
    result->value.seconds = ss;
    return true;
}

void usage()
{
    std::printf(
"Usage: m4acut [OPTIONS] INPUT_FILE\n"
"Options:\n"
" -h, --help             Print this help message\n"
" -v, --version          Show version number\n"
" -o, --output <file>    Specify output filename.\n"
"                        Ignored if -c/-C is specified, otherwise required.\n"
" -s, --start <[[hh:]mm:]ss[.ss..]|ns>\n"
"                        Specify cut start point in either time or number of\n"
"                        samples.\n"
"                        When not given, 0 is assumed.\n"
"                        Example:\n"
"                          588s      : 588 samples\n"
"                          1:23.586  : 1m 23.586s\n"
"                          15        : 15s\n"
" -e, --end <[[hh:]mm:]ss[.ss..]|ns>\n"
"                        Specify cut end point (exclusive).\n"
"                        When not given, end of input is assumed.\n"
" -c, --chapter-mode     Split automatically at chapter points.\n"
"                        Title tag and track tag are created from chapter.\n"
" -C, --cuesheet <file>  Split automatically by cuesheet.\n"
" --cuesheet-encoding <name>\n"
"                        Specify character encoding of cuesheet.\n"
"                        By default, UTF-8 is assumed.\n"
" --fix-sbr-delay <1|-1>\n"
"                        Modify media offset (delay) by the amount of\n"
"                        SBR decoder delay (=481).\n"
"                        1:  increase delay.\n"
"                        -1: decrease delay.\n"
    );
}

bool parse_options(int argc, char **argv, params_t *params)
{
    static option long_options[] = {
        { "help",              no_argument,        0, 'h' },
        { "version",           no_argument,        0, 'v' },
        { "output",            required_argument,  0, 'o' },
        { "start",             required_argument,  0, 's' },
        { "end",               required_argument,  0, 'e' },
        { "chapter-mode",      no_argument,        0, 'c' },
        { "cuesheet",          required_argument,  0, 'C' },
        { "cuesheet-encoding", required_argument,  0, 'E' },
        { "fix-sbr-delay",     required_argument,  0, 'F' },
        {  0,                  0,                  0,  0  },
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "hvo:s:e:cC:",
                             long_options, 0)) != EOF)
    {
        switch (ch) {
        case 'h':
            return usage(), false;
        case 'v':
            std::fprintf(stderr, "m4acut version %s\n", m4acut_version);
            std::exit(0);
        case 'o':
            params->ofilename = optarg;
            break;
        case 's':
            if (!parse_timespec(optarg, &params->start)) {
                std::fputs("ERROR: malformed timespec for -s\n", stderr);
                return false;
            }
            break;
        case 'e':
            if (!parse_timespec(optarg, &params->end)) {
                std::fputs("ERROR: malformed timespec for -e\n", stderr);
                return false;
            }
            break;
        case 'c':
            params->chapter_mode = true;
            break;
        case 'C':
            params->cuesheet = optarg;
            break;
        case 'E':
            params->cuesheet_encoding = optarg;
            break;
        case 'F':
            if (std::sscanf(optarg, "%d", &params->sbr_delay_fix) != 1) {
                std::fputs("ERROR: invalid arg for --fix-sbr-delay\n", stderr);
                return false;
            }
            break;
        default:
            return false;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc != 1)
        return usage(), false;

    params->ifilename = argv[0];
    int ne = params->chapter_mode
           + (params->start.value.samples || params->end.value.samples)
           + (params->cuesheet != nullptr);
    if (ne > 1) {
        std::fputs("ERROR: -c , -C, and -s/-e are mutually exclusive\n",
                   stderr);
        return false;
    }
    if (!params->chapter_mode && !params->cuesheet && !params->ofilename) {
        std::fputs("ERROR: output filename is required\n", stderr);
        return false;
    }
    return true;
}

void process_file(M4ATrimmer &trimmer)
{
    uint64_t au, num_au = trimmer.num_access_units();

    int64_t last = 0;
    for (au = 1; trimmer.copy_next_access_unit(); ++au) {
        int64_t now = aa_timer();
        if (now - last > 1000) {
            int percent = static_cast<int>(au * 100 / num_au);
            std::fprintf(stderr, "\r%d%%", percent);
            last = now;
        }
    }
    trimmer.finish_write(0, 0);
    std::fputs("\r100%...done\n", stderr);
}

void set_tag(M4ATrimmer &trimmer, const std::string &k, const std::string &v)
{
    struct tag_item {
        const char                 *name;
        lsmash_itunes_metadata_item fcc;
    } tag_items[] = {
        { "ALBUM",          ITUNES_METADATA_ITEM_ALBUM_NAME   },
        { "ALBUMARTIST",    ITUNES_METADATA_ITEM_ALBUM_ARTIST },
        { "ARTIST",         ITUNES_METADATA_ITEM_ARTIST       },
        { "DATE",           ITUNES_METADATA_ITEM_RELEASE_DATE },
        { "DISC",           ITUNES_METADATA_ITEM_DISC_NUMBER  },
        { "GENRE",          ITUNES_METADATA_ITEM_USER_GENRE   },
        { "SONGWRITER",     ITUNES_METADATA_ITEM_COMPOSER     },
        { "TITLE",          ITUNES_METADATA_ITEM_TITLE        },
        { "TRACK",          ITUNES_METADATA_ITEM_TRACK_NUMBER },
        { 0,                ITUNES_METADATA_ITEM_CUSTOM       }
    };

    lsmash_itunes_metadata_item fcc = ITUNES_METADATA_ITEM_CUSTOM;
    for (tag_item *p = tag_items; p->name; ++p) {
        if (k == p->name) {
            fcc = p->fcc;
            break;
        }
    }
    switch (fcc) {
    case ITUNES_METADATA_ITEM_ENCODED_BY:
    case ITUNES_METADATA_ITEM_GROUPING:
    case ITUNES_METADATA_ITEM_LYRICS:
    case ITUNES_METADATA_ITEM_TRACK_SUBTITLE:
    case ITUNES_METADATA_ITEM_ENCODING_TOOL:
    case ITUNES_METADATA_ITEM_PODCAST_CATEGORY:
    case ITUNES_METADATA_ITEM_COPYRIGHT:
    case ITUNES_METADATA_ITEM_DESCRIPTION:
    case ITUNES_METADATA_ITEM_GROUPING_DRAFT:
    case ITUNES_METADATA_ITEM_PODCAST_KEYWORD:
    case ITUNES_METADATA_ITEM_LONG_DESCRIPTION:
    case ITUNES_METADATA_ITEM_PURCHASE_DATE:
    case ITUNES_METADATA_ITEM_TV_EPISODE_ID:
    case ITUNES_METADATA_ITEM_TV_NETWORK:
    case ITUNES_METADATA_ITEM_TV_SHOW_NAME:
    case ITUNES_METADATA_ITEM_ITUNES_PURCHASE_ACCOUNT_ID:
    case ITUNES_METADATA_ITEM_ITUNES_SORT_ALBUM:
    case ITUNES_METADATA_ITEM_ITUNES_SORT_ARTIST:
    case ITUNES_METADATA_ITEM_ITUNES_SORT_ALBUM_ARTIST:
    case ITUNES_METADATA_ITEM_ITUNES_SORT_COMPOSER:
    case ITUNES_METADATA_ITEM_ITUNES_SORT_NAME:
    case ITUNES_METADATA_ITEM_ITUNES_SORT_SHOW:
    case ITUNES_METADATA_ITEM_EPISODE_GLOBAL_ID:
    case ITUNES_METADATA_ITEM_PREDEFINED_GENRE:
    case ITUNES_METADATA_ITEM_PODCAST_URL:
    case ITUNES_METADATA_ITEM_CONTENT_RATING:
    case ITUNES_METADATA_ITEM_MEDIA_TYPE:
    case ITUNES_METADATA_ITEM_BEATS_PER_MINUTE:
    case ITUNES_METADATA_ITEM_TV_EPISODE:
    case ITUNES_METADATA_ITEM_TV_SEASON:
    case ITUNES_METADATA_ITEM_ITUNES_ACCOUNT_TYPE:
    case ITUNES_METADATA_ITEM_ITUNES_ARTIST_ID:
    case ITUNES_METADATA_ITEM_ITUNES_COMPOSER_ID:
    case ITUNES_METADATA_ITEM_ITUNES_CATALOG_ID:
    case ITUNES_METADATA_ITEM_ITUNES_TV_GENRE_ID:
    case ITUNES_METADATA_ITEM_ITUNES_PLAYLIST_ID:
    case ITUNES_METADATA_ITEM_ITUNES_COUNTRY_CODE:
    case ITUNES_METADATA_ITEM_DISC_COMPILATION:
    case ITUNES_METADATA_ITEM_HIGH_DEFINITION_VIDEO:
    case ITUNES_METADATA_ITEM_PODCAST:
    case ITUNES_METADATA_ITEM_GAPLESS_PLAYBACK:
    case ITUNES_METADATA_ITEM_COVER_ART:
    case ITUNES_METADATA_ITEM_CUSTOM:
    case ITUNES_METADATA_ITEM_USER_COMMENT:
        break;
    case ITUNES_METADATA_ITEM_ALBUM_NAME:
    case ITUNES_METADATA_ITEM_ALBUM_ARTIST:
    case ITUNES_METADATA_ITEM_ARTIST:
    case ITUNES_METADATA_ITEM_RELEASE_DATE:
    case ITUNES_METADATA_ITEM_USER_GENRE:
    case ITUNES_METADATA_ITEM_COMPOSER:
    case ITUNES_METADATA_ITEM_TITLE:
        trimmer.set_text_tag(fcc, v);
        break;
    case ITUNES_METADATA_ITEM_DISC_NUMBER:
        {
            unsigned n, t = 0;
            if (std::sscanf(v.c_str(), "%u/%u", &n, &t) > 0)
                trimmer.set_disk_tag(n, t);
        }
        break;
    case ITUNES_METADATA_ITEM_TRACK_NUMBER:
        {
            unsigned n, t = 0;
            if (std::sscanf(v.c_str(), "%u/%u", &n, &t) > 0)
                trimmer.set_track_tag(n, t);
        }
        break;
    }
}

void process_cuesheet(M4ATrimmer &trimmer, const params_t &params)
{
    FILE *fp = aa_fopen(params.cuesheet, "r");
    if (!fp)
        throw_file_error(params.cuesheet, std::strerror(errno));
    std::shared_ptr<FILE> __fp__(fp, std::fclose);

    std::string data;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0)
        data.append(buf, n);
    if (data.size() >= 3 && data.substr(0,3) == "\xef\xbb\xbf")
        data = data.substr(3);

    std::shared_ptr<IStringConverter> converter;
    const char *encoding = params.cuesheet_encoding;
    if (!encoding) encoding = "UTF-8";
#if HAVE_ICONV
    converter = std::make_shared<StringConverterIConv>("UTF-8", encoding);
#elif defined(_WIN32)
    converter = std::make_shared<StringConverterWin32>("UTF-8", encoding);
#else
    converter = std::make_shared<StringConverterUTF8();
#endif
    auto res = converter->convert(data, true);
    if (!res.first) {
        std::stringstream msg;
        msg << "cuesheet isn't encoded with " << encoding 
            << ", specify correct character encoding by --cuesheet-encoding";
        throw std::runtime_error(msg.str());
    }
    std::stringstream ss(res.second);
    CueSheet cuesheet;
    cuesheet.parse(ss.rdbuf());
    std::vector<std::pair<double, std::string>> chapters;
    cuesheet.as_chapters(static_cast<double>(trimmer.duration())/
                         trimmer.timescale(), &chapters);
    size_t i = 0;
    double dts = 0.0;
    for (auto track = cuesheet.begin(); track != cuesheet.end(); ++track) {
        double duration = chapters[i++].first;
        std::map<std::string, std::string> tags;
        track->get_tags(&tags);
        for (auto t = tags.begin(); t != tags.end(); ++t)
            set_tag(trimmer, t->first, t->second);
        std::stringstream name;
        name << std::setfill('0') << std::setw(2) << track->number();
        if (!track->name().empty())
            name << ' ' << safe_filename(track->name()) << ".m4a";
        aa_fprintf(stderr, "%s\n", name.str().c_str());
        trimmer.open_output(name.str());
        TimeSpec beg, end;
        beg.is_samples    = end.is_samples = false;
        beg.value.seconds = dts;
        end.value.seconds = dts + duration;
        dts += duration;
        trimmer.select_cut_point(beg, end);
        process_file(trimmer);
    }
}

} // end of empty namespace

int main(int argc, char **argv)
{
    params_t params = { 0 };

    std::setlocale(LC_CTYPE, "");
    std::setbuf(stderr, 0);
    aa_getmainargs(&argc, &argv);
    if (!parse_options(argc, argv, &params))
        return 1;
    try {
        M4ATrimmer trimmer;
        trimmer.open_input(params.ifilename);
        if (params.sbr_delay_fix)
            trimmer.shift_edits(params.sbr_delay_fix * 481);
        if (params.cuesheet)
            process_cuesheet(trimmer, params);
        else if (params.chapter_mode) {
            auto chapters = trimmer.chapters();
            if (!chapters.size())
                throw std::runtime_error("no chapters in the file");
            for (size_t i = 0; i < chapters.size(); ++i) {
                std::stringstream ss;
                ss << std::setfill('0') << std::setw(2) << (i + 1)
                   << ' ' << safe_filename(chapters[i].second) << ".m4a";
                aa_fprintf(stderr, "%s\n", ss.str().c_str());
                trimmer.open_output(ss.str());
                trimmer.select_chapter(i);
                process_file(trimmer);
            }
        } else {
            trimmer.open_output(params.ofilename);
            trimmer.select_cut_point(params.start, params.end);
            process_file(trimmer);
        }
    } catch (std::exception &e) {
        aa_fprintf(stderr, "\r%s\n", e.what());
        return 2;
    }
    return 0;
}

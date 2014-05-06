/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#if HAVE_CONFIG_H
# include "config.h"
#endif
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <string>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <getopt.h>
#include "M4ATrimmer.h"
#include "compat.h"
#include "version.h"

namespace {

struct params_t {
    const char *ifilename;
    const char *ofilename;
    TimeSpec start;
    TimeSpec end;
    bool chapter_mode;
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
"                        Ignored if -c is specified, otherwise required.\n"
" -s, --start <[[hh:]mm:]ss[.ss..]|ns>\n"
"                        Specify cut start point in either time or number of\n"
"                        samples.\n"
"                        When not given, 0 is assumed.\n"
"                        Example:\n"
"                          588s      : 588 samples\n"
"                          1:23.586  : 1m 23.586s\n"
"                          15        : 15s\n"
"                        Note that you have to count in half the sample rate\n"
"                        in case of dual-rate HE-AAC (1 means 22050s).\n"
" -e, --end <[[hh:]mm:]ss[.ss..]|ns>\n"
"                        Specify cut end point (exclusive).\n"
"                        When not given, end of input is assumed.\n"
" -c, --chapter-mode     Split automatically at chapter points.\n"
"                        Title tag and track tag are created from chapter.\n"
"                        You cannot use -c and -s/-e at the same time.\n"
    );
}

bool parse_options(int argc, char **argv, params_t *params)
{
    static option long_options[] = {
        { "help",             no_argument,        0, 'h' },
        { "version",          no_argument,        0, 'v' },
        { "output",           required_argument,  0, 'o' },
        { "start",            required_argument,  0, 's' },
        { "end",              required_argument,  0, 'e' },
        { "chapter-mode",     no_argument,        0, 'c' },
        {  0,                 0,                  0,  0  },
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "hvo:s:e:c",
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
        default:
            return false;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc != 1)
        return usage(), false;

    params->ifilename = argv[0];
    if (params->chapter_mode && (params->start.value.samples ||
                                 params->end.value.samples)) {
        std::fputs("ERROR: -c and -s/-e are mutually exclusive\n", stderr);
        return false;
    }
    if (!params->chapter_mode && !params->ofilename) {
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
        if (!params.chapter_mode) {
            trimmer.open_output(params.ofilename);
            trimmer.select_cut_point(params.start, params.end);
            process_file(trimmer);
        } else {
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
        }
    } catch (std::exception &e) {
        aa_fprintf(stderr, "\r%s\n", e.what());
        return 2;
    }
    return 0;
}

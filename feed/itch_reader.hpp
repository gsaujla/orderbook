#pragma once
// Streams an ITCH 5.0 file (plain or .gz) through itch::parse in fixed-size chunks.

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "itch_parser.hpp"

namespace itch {

struct ReadStats {
    uint64_t bytes = 0;
    bool bad_length = false;
    bool truncated = false;  // file ended in the middle of a message
};

// Calls the handler for every message in `path`. gzopen reads uncompressed files too.
// Return false from `keep_going()` to stop early (checked once per chunk).
template <typename H, typename Stop>
ReadStats readFile(const std::string& path, H& h, Stop keep_going, size_t chunk = 1 << 20) {
    gzFile f = gzopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    gzbuffer(f, 1 << 20);

    std::vector<uint8_t> buf(chunk + 65538);  // room for one max-size leftover message
    size_t have = 0;
    ReadStats st;
    while (keep_going()) {
        int got = gzread(f, buf.data() + have, unsigned(chunk));
        if (got < 0) {
            int err;
            std::string msg = gzerror(f, &err);
            gzclose(f);
            throw std::runtime_error("read error in " + path + ": " + msg);
        }
        if (got == 0) break;
        have += size_t(got);
        st.bytes += size_t(got);
        size_t used = parse(buf.data(), have, h, st.bad_length);
        std::memmove(buf.data(), buf.data() + used, have - used);
        have -= used;
    }
    st.truncated = have != 0;
    gzclose(f);
    return st;
}

template <typename H>
ReadStats readFile(const std::string& path, H& h) {
    return readFile(path, h, [] { return true; });
}

}  // namespace itch

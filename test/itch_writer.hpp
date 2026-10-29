#pragma once
// Test helper: builds length-prefixed, big-endian ITCH 5.0 messages byte by byte.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Builds length-prefixed, big-endian ITCH messages byte by byte, independently of the
// parser's field offsets, so a wrong offset in the parser shows up as a wrong value.
struct Writer {
    std::vector<uint8_t> out;
    std::vector<uint8_t> msg;

    void u8(uint8_t v) { msg.push_back(v); }
    void ch(char c) { msg.push_back(uint8_t(c)); }
    void be(uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; i--) msg.push_back(uint8_t(v >> (8 * i)));
    }
    void text(const char* s, size_t width) {  // space-padded
        size_t n = std::strlen(s);
        for (size_t i = 0; i < width; i++) msg.push_back(uint8_t(i < n ? s[i] : ' '));
    }
    void start(char type, uint16_t locate, uint64_t ts) {
        msg.clear();
        ch(type);
        be(locate, 2);
        be(0, 2);  // tracking
        be(ts, 6);
    }
    void end() {
        out.push_back(uint8_t(msg.size() >> 8));
        out.push_back(uint8_t(msg.size()));
        out.insert(out.end(), msg.begin(), msg.end());
    }

    void add(uint64_t ref, char side, uint32_t shares, const char* stock, uint32_t price,
             const char* mpid = nullptr, uint16_t locate = 1, uint64_t ts = 1000) {
        start(mpid ? 'F' : 'A', locate, ts);
        be(ref, 8);
        ch(side);
        be(shares, 4);
        text(stock, 8);
        be(price, 4);
        if (mpid) text(mpid, 4);
        end();
    }
    void executed(uint64_t ref, uint32_t shares, uint64_t match, uint16_t locate = 1,
                  uint64_t ts = 2000) {
        start('E', locate, ts);
        be(ref, 8);
        be(shares, 4);
        be(match, 8);
        end();
    }
    void executedWithPrice(uint64_t ref, uint32_t shares, uint64_t match, char printable,
                           uint32_t price, uint16_t locate = 1, uint64_t ts = 2000) {
        start('C', locate, ts);
        be(ref, 8);
        be(shares, 4);
        be(match, 8);
        ch(printable);
        be(price, 4);
        end();
    }
    void cancel(uint64_t ref, uint32_t shares, uint16_t locate = 1, uint64_t ts = 3000) {
        start('X', locate, ts);
        be(ref, 8);
        be(shares, 4);
        end();
    }
    void del(uint64_t ref, uint16_t locate = 1, uint64_t ts = 3000) {
        start('D', locate, ts);
        be(ref, 8);
        end();
    }
    void replace(uint64_t old_ref, uint64_t new_ref, uint32_t shares, uint32_t price,
                 uint16_t locate = 1, uint64_t ts = 4000) {
        start('U', locate, ts);
        be(old_ref, 8);
        be(new_ref, 8);
        be(shares, 4);
        be(price, 4);
        end();
    }
    void directory(uint16_t locate, const char* stock, uint64_t ts = 500) {
        start('R', locate, ts);
        text(stock, 8);
        ch('Q');       // market category
        ch('N');       // financial status
        be(100, 4);    // round lot size
        ch('N');       // round lots only
        ch('C');       // issue classification
        text("Z", 2);  // issue subtype
        ch('P');       // authenticity
        ch(' ');       // short sale threshold
        ch(' ');       // IPO flag
        ch('1');       // LULD tier
        ch('N');       // ETP flag
        be(0, 4);      // ETP leverage
        ch('N');       // inverse
        end();
    }
    void system(char code, uint64_t ts = 100) {
        start('S', 0, ts);
        ch(code);
        end();
    }
    void raw(char type, size_t body_len) {  // arbitrary message, for skip / bad-length tests
        start(type, 1, 0);
        while (msg.size() < body_len) u8(0);
        msg.resize(body_len);
        end();
    }
};

// Write `bytes` to a temp file and return its path.
inline std::string writeTemp(const std::vector<uint8_t>& bytes, const std::string& name) {
    std::string path = ::testing::TempDir() + name;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
    return path;
}

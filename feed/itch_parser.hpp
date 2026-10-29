#pragma once
// NASDAQ TotalView-ITCH 5.0 decoder for the messages the book needs.
//
// File framing (as in NASDAQ's sample files): each message is preceded by a 2-byte
// big-endian length. Every message starts with a common header:
//   type(1) stock_locate(2) tracking_number(2) timestamp(6, ns since midnight)
// All integers are big-endian. Prices are 4-byte unsigned with 4 implied decimals,
// which are kept as integer ticks (1 tick = $0.0001).
//
// Decoded: S system event, R stock directory, A add, F add w/ MPID, E executed,
// C executed with price, X cancel (partial), D delete, U replace.
// All other message types are skipped (reported via Handler::onOther).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "order.hpp"

namespace itch {

// ---- Big-endian helpers ----------------------------------------------------------

inline uint16_t be16(const uint8_t* p) {
    return uint16_t(p[0]) << 8 | uint16_t(p[1]);
}
inline uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
inline uint64_t be48(const uint8_t* p) {
    return uint64_t(be16(p)) << 32 | be32(p + 2);
}
inline uint64_t be64(const uint8_t* p) {
    return uint64_t(be32(p)) << 32 | be32(p + 4);
}

// ---- Decoded messages --------------------------------------------------------------

struct Header {
    char type;
    uint16_t locate;     // per-day stock id, assigned by the R messages
    uint16_t tracking;   // NASDAQ internal
    uint64_t timestamp;  // ns since midnight
};

// 8-char space-padded symbol; view() trims the padding.
struct Symbol {
    char raw[8];
    std::string_view view() const {
        size_t n = 8;
        while (n > 0 && raw[n - 1] == ' ') n--;
        return {raw, n};
    }
};

struct SystemEvent {
    Header h;
    char code;  // O start of messages, S start of system hours, Q start of market hours,
                // M end of market hours, E end of system hours, C end of messages
};

struct StockDirectory {
    Header h;
    Symbol stock;
    char market_category;
    char financial_status;
    uint32_t round_lot_size;
};

struct AddOrder {  // A, and F (which also carries an MPID)
    Header h;
    uint64_t ref;  // order reference number, unique for the day
    Side side;
    uint32_t shares;
    Symbol stock;
    int64_t price;  // ticks
    bool has_mpid;
    char mpid[4];
};

struct Executed {  // E: executes at the resting order's own price
    Header h;
    uint64_t ref;
    uint32_t shares;
    uint64_t match;
};

struct ExecutedWithPrice {  // C: executes at a different price (e.g. cross)
    Header h;
    uint64_t ref;
    uint32_t shares;
    uint64_t match;
    bool printable;
    int64_t price;  // ticks
};

struct Cancel {  // X: remove some shares, order keeps its place
    Header h;
    uint64_t ref;
    uint32_t shares;
};

struct Delete {  // D: remove the whole order
    Header h;
    uint64_t ref;
};

struct Replace {  // U: cancel `old_ref`, add `new_ref` (same side/stock, loses priority)
    Header h;
    uint64_t old_ref;
    uint64_t new_ref;
    uint32_t shares;
    int64_t price;  // ticks
};

// Message sizes in bytes (without the 2-byte length prefix), from the ITCH 5.0 spec.
constexpr size_t sizeOf(char type) {
    switch (type) {
        case 'S':
            return 12;
        case 'R':
            return 39;
        case 'A':
            return 36;
        case 'F':
            return 40;
        case 'E':
            return 31;
        case 'C':
            return 36;
        case 'X':
            return 23;
        case 'D':
            return 19;
        case 'U':
            return 35;
        default:
            return 0;  // not decoded
    }
}

// Derive from this and hide the callbacks you care about; the parser is a template
// over the handler type, so calls are resolved at compile time.
struct Handler {
    void onSystemEvent(const SystemEvent&) {}
    void onStockDirectory(const StockDirectory&) {}
    void onAdd(const AddOrder&) {}
    void onExecuted(const Executed&) {}
    void onExecutedWithPrice(const ExecutedWithPrice&) {}
    void onCancel(const Cancel&) {}
    void onDelete(const Delete&) {}
    void onReplace(const Replace&) {}
    void onOther(char /*type*/, const uint8_t* /*msg*/, size_t /*len*/) {}
    // A decoded type arrived with the wrong length: the stream is corrupt or not ITCH 5.0.
    void onBadLength(char /*type*/, size_t /*got*/, size_t /*want*/) {}
};

// ---- Decoding --------------------------------------------------------------------

inline Header header(const uint8_t* m) {
    return Header{char(m[0]), be16(m + 1), be16(m + 3), be48(m + 5)};
}

// Decode one message body (no length prefix). Returns false on a size mismatch.
template <typename H>
bool decode(const uint8_t* m, size_t len, H& h) {
    if (len == 0) return true;
    char type = char(m[0]);
    size_t want = sizeOf(type);
    if (want == 0) {
        h.onOther(type, m, len);
        return true;
    }
    if (len != want) {
        h.onBadLength(type, len, want);
        return false;
    }

    Header hd = header(m);
    const uint8_t* p = m + 11;  // first field after the header
    switch (type) {
        case 'S':
            h.onSystemEvent(SystemEvent{hd, char(p[0])});
            break;
        case 'R': {
            StockDirectory r{};
            r.h = hd;
            std::memcpy(r.stock.raw, p, 8);
            r.market_category = char(p[8]);
            r.financial_status = char(p[9]);
            r.round_lot_size = be32(p + 10);
            h.onStockDirectory(r);
            break;
        }
        case 'A':
        case 'F': {
            AddOrder a{};
            a.h = hd;
            a.ref = be64(p);
            a.side = (p[8] == 'B') ? Side::BUY : Side::SELL;
            a.shares = be32(p + 9);
            std::memcpy(a.stock.raw, p + 13, 8);
            a.price = be32(p + 21);
            a.has_mpid = (type == 'F');
            if (a.has_mpid) std::memcpy(a.mpid, p + 25, 4);
            h.onAdd(a);
            break;
        }
        case 'E':
            h.onExecuted(Executed{hd, be64(p), be32(p + 8), be64(p + 12)});
            break;
        case 'C':
            h.onExecutedWithPrice(ExecutedWithPrice{hd, be64(p), be32(p + 8), be64(p + 12),
                                                    p[20] == 'Y', int64_t(be32(p + 21))});
            break;
        case 'X':
            h.onCancel(Cancel{hd, be64(p), be32(p + 8)});
            break;
        case 'D':
            h.onDelete(Delete{hd, be64(p)});
            break;
        case 'U':
            h.onReplace(Replace{hd, be64(p), be64(p + 8), be32(p + 16), int64_t(be32(p + 20))});
            break;
    }
    return true;
}

// Decode as many complete length-prefixed messages as `buf` holds. Returns the number
// of bytes consumed; any remainder is a partial message to be completed by the next
// chunk. Sets `bad` if a message had the wrong length.
template <typename H>
size_t parse(const uint8_t* buf, size_t len, H& h, bool& bad) {
    size_t pos = 0;
    while (pos + 2 <= len) {
        size_t n = be16(buf + pos);
        if (pos + 2 + n > len) break;  // incomplete
        if (!decode(buf + pos + 2, n, h)) bad = true;
        pos += 2 + n;
    }
    return pos;
}

}  // namespace itch

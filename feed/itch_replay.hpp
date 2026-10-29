#pragma once
// Replays ITCH 5.0 messages into one book per stock. BookT is Book (Replayer) or
// PoolBook (PoolReplayer); with PoolBook every stock's book shares one OrderPool.
//
// NASDAQ has already done the matching, so this calls Book directly (Add / Find /
// Fill / Cancel) and never Engine: every A/F message is a resting limit order, and
// every E/C message is a fill of an order already in the book.
//
//   A / F  add            -> Book::Add
//   E      executed       -> Book::Fill(Find(ref), shares)
//   C      executed @ px  -> same as E; the book is unaffected by the print price
//   X      partial cancel -> Book::Fill(Find(ref), shares)   (reduce qty, keep place)
//   D      delete         -> Book::Cancel(ref)
//   U      replace        -> side from old order; Cancel(old) then Add(new)
//
// Anything that does not fit the book (unknown ref, more shares than remain, duplicate
// ref, zero shares, pool full) is counted in `errors` and skipped. A clean day has all zeros.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "book.hpp"
#include "itch_parser.hpp"
#include "poolbook.hpp"

namespace itch {

struct ReplayErrors {
    uint64_t unknown_ref = 0;    // E/C/X/D/U for a ref not in the book
    uint64_t overfill = 0;       // E/C/X for more shares than the order has left
    uint64_t duplicate_add = 0;  // A/F/U new ref already resting
    uint64_t zero_shares = 0;    // A/F/U with 0 shares, or E/C/X with 0 shares
    uint64_t pool_full = 0;      // A/F/U rejected because the OrderPool has no free slot
    uint64_t total() const {
        return unknown_ref + overfill + duplicate_add + zero_shares + pool_full;
    }
};

struct ReplayCounts {
    uint64_t adds = 0, executes = 0, partial_cancels = 0, deletes = 0, replaces = 0;
    uint64_t executed_shares = 0;
    uint64_t resting = 0;  // orders currently in all books
    uint64_t max_resting = 0;
};

template <typename BookT>
class BasicReplayer : public Handler {
public:
    // pool_capacity is used only by PoolBook. The full 2019-01-30 day peaks at 1.74M
    // resting orders, so the 4M default leaves room.
    explicit BasicReplayer(size_t pool_capacity = size_t(1) << 22) : books(65536) {
        if constexpr (kPooled) pool = std::make_unique<OrderPool>(pool_capacity);
    }

    void onStockDirectory(const StockDirectory& m) {
        std::string s(m.stock.view());
        names[m.h.locate] = s;
        locates[s] = m.h.locate;
    }

    void onAdd(const AddOrder& m) {
        if (m.shares == 0) {
            err.zero_shares++;
            return;
        }
        BookT& b = book(m.h.locate);
        if (!b.Add(Order{m.ref, m.shares, m.price, m.side})) {
            addFailed(b, m.ref);
            return;
        }
        n.adds++;
        rested();
    }

    void onExecuted(const Executed& m) { execute(m.h.locate, m.ref, m.shares); }
    void onExecutedWithPrice(const ExecutedWithPrice& m) { execute(m.h.locate, m.ref, m.shares); }

    void onCancel(const Cancel& m) {
        if (reduce(m.h.locate, m.ref, m.shares)) n.partial_cancels++;
    }

    void onDelete(const Delete& m) {
        if (!book(m.h.locate).Cancel(m.ref)) {
            err.unknown_ref++;
            return;
        }
        n.deletes++;
        n.resting--;
    }

    void onReplace(const Replace& m) {
        BookT& b = book(m.h.locate);
        const Order* old = b.Find(m.old_ref);
        if (!old) {
            err.unknown_ref++;
            return;
        }
        Side side = old->side;  // U does not repeat the side
        b.Cancel(m.old_ref);
        n.resting--;
        if (m.shares == 0) {
            err.zero_shares++;
            return;
        }
        if (!b.Add(Order{m.new_ref, m.shares, m.price, side})) {
            addFailed(b, m.new_ref);
            return;
        }
        n.replaces++;
        rested();
    }

    // ---- Inspection ------------------------------------------------------------------

    const ReplayErrors& errors() const { return err; }
    const ReplayCounts& counts() const { return n; }

    // nullptr if no order for this locate has been seen.
    const BookT* bookFor(uint16_t locate) const { return books[locate].get(); }
    const BookT* bookFor(std::string_view symbol) const {
        auto it = locates.find(std::string(symbol));
        return it == locates.end() ? nullptr : bookFor(it->second);
    }
    std::string_view symbol(uint16_t locate) const {
        auto it = names.find(locate);
        return it == names.end() ? std::string_view() : std::string_view(it->second);
    }

    // Calls f(locate, book) for every book that has been used.
    template <typename F>
    void forEachBook(F f) const {
        for (size_t i = 0; i < books.size(); i++)
            if (books[i]) f(uint16_t(i), *books[i]);
    }

    // nullptr unless BookT is PoolBook.
    const OrderPool* orderPool() const { return pool.get(); }

private:
    static constexpr bool kPooled = std::is_same_v<BookT, PoolBook>;

    std::unique_ptr<OrderPool> pool;  // declared before books: books release into it on destruction
    // Indexed by stock locate (16 bits). Allocated on first use.
    std::vector<std::unique_ptr<BookT>> books;
    std::unordered_map<uint16_t, std::string> names;
    std::unordered_map<std::string, uint16_t> locates;
    ReplayErrors err;
    ReplayCounts n;

    BookT& book(uint16_t locate) {
        auto& b = books[locate];
        if (!b) {
            if constexpr (kPooled)
                b = std::make_unique<BookT>(*pool);
            else
                b = std::make_unique<BookT>();
        }
        return *b;
    }

    // Add returns false for a duplicate ref, or (PoolBook) when the pool is full.
    void addFailed(const BookT& b, uint64_t ref) {
        if (b.Find(ref))
            err.duplicate_add++;
        else
            err.pool_full++;
    }

    void rested() {
        n.resting++;
        if (n.resting > n.max_resting) n.max_resting = n.resting;
    }

    // Shared by E/C (execution) and X (partial cancel): take shares off a resting order.
    // Returns true if applied.
    bool reduce(uint16_t locate, uint64_t ref, uint32_t shares) {
        BookT& b = book(locate);
        Order* o = b.Find(ref);
        if (!o) {
            err.unknown_ref++;
            return false;
        }
        if (shares == 0) {
            err.zero_shares++;
            return false;
        }
        if (shares > o->qty) {
            err.overfill++;
            return false;
        }
        bool gone = shares == o->qty;
        b.Fill(o, shares);  // o is dangling if gone
        if (gone) n.resting--;
        return true;
    }

    void execute(uint16_t locate, uint64_t ref, uint32_t shares) {
        if (reduce(locate, ref, shares)) {
            n.executes++;
            n.executed_shares += shares;
        }
    }
};

using Replayer = BasicReplayer<Book>;
using PoolReplayer = BasicReplayer<PoolBook>;

}  // namespace itch

// hash_open.hpp -- ADAY 2: Acik adreslemeli hash tablosu (linear probing)
//
// Fikir: Zincir yok. Her kelime dogrudan tablodaki bir goze konur. Goz doluysa
// bir sonraki goze bakilir (dogrusal yoklama). Silmede "mezar tasi" yerine
// geriye kaydirma (backward shift) kullanilir, boylece tablo hic kirlenmez.
#pragma once
#include <cstring>
#include "mock.hpp"

namespace findfast {

class OpenHashTable {
    struct Rec {  // [baslik 12 bayt][harfler]
        std::uint32_t hash;
        ListId        list;
        std::uint32_t len;
    };
    static constexpr std::size_t kHeader = 12;
    static constexpr std::size_t kInitial = 8;

    Rec**       slots_ = nullptr;  // nullptr = bos goz
    std::size_t cap_ = 0;          // 2'nin kuvveti
    std::size_t count_ = 0;

    static char* text(Rec* r) { return reinterpret_cast<char*>(r) + kHeader; }

    static std::uint32_t hash_of(std::string_view w) {
        std::uint32_t h = 2166136261u;
        for (unsigned char c : w) {
            h ^= c;
            h *= 16777619u;
        }
        h ^= h >> 15;
        return h;
    }
    static bool same(const Rec* r, std::uint32_t h, std::string_view w) {
        return r->hash == h && r->len == w.size() &&
               std::memcmp(reinterpret_cast<const char*>(r) + kHeader, w.data(), w.size()) == 0;
    }

    // Kelimenin bulundugu gozu, yoksa konulabilecegi ilk bos gozu dondurur.
    std::size_t probe(std::uint32_t h, std::string_view w) const {
        std::size_t i = h & (cap_ - 1);
        while (slots_[i] && !same(slots_[i], h, w)) i = (i + 1) & (cap_ - 1);
        return i;
    }

    void grow(std::size_t new_cap) {
        Rec** old = slots_;
        std::size_t old_cap = cap_;
        slots_ = static_cast<Rec**>(memory::allocate(new_cap * sizeof(Rec*)));
        for (std::size_t i = 0; i < new_cap; ++i) slots_[i] = nullptr;
        cap_ = new_cap;
        for (std::size_t i = 0; i < old_cap; ++i) {
            if (Rec* r = old[i]) {
                std::size_t j = r->hash & (cap_ - 1);
                while (slots_[j]) j = (j + 1) & (cap_ - 1);
                slots_[j] = r;
            }
        }
        memory::deallocate(old);
    }

public:
    OpenHashTable() = default;
    OpenHashTable(const OpenHashTable&) = delete;
    OpenHashTable& operator=(const OpenHashTable&) = delete;
    ~OpenHashTable() { clear(); }

    void insert_or_assign(std::string_view w, ListId list) {
        const std::uint32_t h = hash_of(w);
        if (cap_ != 0) {
            std::size_t i = probe(h, w);
            if (slots_[i]) {
                slots_[i]->list = list;
                return;
            }
        }
        if (cap_ == 0) grow(kInitial);
        else if ((count_ + 1) * 4 > cap_ * 3) grow(cap_ * 2);  // doluluk en cok %75

        Rec* r = static_cast<Rec*>(memory::allocate(kHeader + w.size()));
        r->hash = h;
        r->list = list;
        r->len = static_cast<std::uint32_t>(w.size());
        std::memcpy(text(r), w.data(), w.size());
        std::size_t i = h & (cap_ - 1);
        while (slots_[i]) i = (i + 1) & (cap_ - 1);
        slots_[i] = r;
        ++count_;
    }

    bool lookup(std::string_view w, ListId& out) const {
        if (cap_ == 0) return false;
        const Rec* r = slots_[probe(hash_of(w), w)];
        if (!r) return false;
        out = r->list;
        return true;
    }

    bool remove(std::string_view w) {
        if (cap_ == 0) return false;
        std::size_t i = probe(hash_of(w), w);
        if (!slots_[i]) return false;
        memory::deallocate(slots_[i]);
        --count_;
        // Geriye kaydirma: acilan boslugu, arkadan gelen ve "aslinda daha onde
        // olmasi gereken" kayitlarla doldur ki yoklama zincirleri kopmasin.
        std::size_t j = i;
        for (;;) {
            j = (j + 1) & (cap_ - 1);
            Rec* r = slots_[j];
            if (!r) break;
            std::size_t home = r->hash & (cap_ - 1);
            // r, i gozune tasinabilir mi? home, (i, j] dairesel araliginda DEGILSE evet.
            bool between = (i <= j) ? (i < home && home <= j) : (i < home || home <= j);
            if (!between) {
                slots_[i] = r;
                i = j;
            }
        }
        slots_[i] = nullptr;
        return true;
    }

    std::size_t size() const { return count_; }

    void clear() {
        for (std::size_t i = 0; i < cap_; ++i) memory::deallocate(slots_[i]);
        memory::deallocate(slots_);
        slots_ = nullptr;
        cap_ = 0;
        count_ = 0;
    }
};

class OpenIndex final : public Index {
    OpenHashTable table_;

public:
    void put(std::string_view w, ListId l) override { table_.insert_or_assign(w, l); }
    bool find(std::string_view w, ListId& r) override { return table_.lookup(w, r); }
    bool erase(std::string_view w) override { return table_.remove(w); }
    std::size_t size() const override { return table_.size(); }
    void clear() override { table_.clear(); }
};

}  // namespace findfast

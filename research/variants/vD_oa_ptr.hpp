namespace findfast {
// Open addressing hash table with linear probing.
// table_ is a power-of-two array of pointers; nullptr means "empty slot".
// Each word lives in ONE allocation of its own, laid out as:
//   [ListId list (4 bytes)][length as varint][characters]
// The length uses base-128 (1 byte for words shorter than 128 characters), so a
// normal word costs 5 + L bytes. No hash is stored: it is recomputed when needed.
// Erase uses backward-shift deletion, so the table never contains tombstones.
class StudentIndex final : public Index {
    static constexpr size_t kLengthOffset = sizeof(ListId); // the varint starts after the ListId
    static constexpr unsigned kInitialBits = 3;   // first table has 2^3 = 8 slots
    static constexpr size_t kMaxLoadNum = 3;      // grow when words would exceed
    static constexpr size_t kMaxLoadDen = 4;      // capacity * 3 / 4 (load factor 0.75)
    static_assert(kMaxLoadNum < kMaxLoadDen, "the table must always keep an empty slot");

    char** table_ = nullptr;     // slots; each one is nullptr or points to a word allocation
    size_t cap_ = 0;             // number of slots: 0 or a power of two
    size_t count_ = 0;           // number of distinct words
    unsigned shift_ = 0;         // home slot = hash >> shift_ (uses the top bits of the hash)

    // Reads 8 bytes of the word as one 64-bit number (char_traits copy = a single load).
    static uint64_t load8(const char* p) {
        uint64_t v;
        char_traits<char>::copy(reinterpret_cast<char*>(&v), p, 8);
        return v;
    }
    static uint64_t load4(const char* p) {
        uint32_t v;
        char_traits<char>::copy(reinterpret_cast<char*>(&v), p, 4);
        return v;
    }
    // 64-bit hash: 8 characters per step, then a final avalanche so every bit depends on every character.
    static uint64_t hash_of(string_view w) {
        const char* p = w.data();
        size_t n = w.size();
        uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
        uint64_t tail;
        if (n >= 8) {
            while (n > 8) {
                h = (h ^ load8(p)) * 0xFF51AFD7ED558CCDull;
                h ^= h >> 32;
                p += 8;
                n -= 8;
            }
            tail = load8(p + n - 8);              // last 8 bytes (may overlap the previous block)
        } else if (n >= 4) {
            tail = load4(p) | (load4(p + n - 4) << 32);
        } else {
            tail = 0;
            for (size_t i = 0; i < n; ++i) tail = (tail << 8) | static_cast<unsigned char>(p[i]);
        }
        h = (h ^ tail) * 0xC4CEB9FE1A85EC53ull;
        h ^= h >> 29;
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
        return h;
    }

    // The ListId is the first 4 bytes of an entry; char_traits copy avoids any aliasing issue.
    static ListId list_of(const char* entry) {
        ListId list;
        char_traits<char>::copy(reinterpret_cast<char*>(&list), entry, sizeof list);
        return list;
    }
    static void set_list(char* entry, ListId list) {
        char_traits<char>::copy(entry, reinterpret_cast<const char*>(&list), sizeof list);
    }

    // Bytes needed to store `length` as a varint (7 bits of the number per byte).
    static size_t varint_size(size_t length) {
        size_t bytes = 1;
        while (length >= 128) {
            length >>= 7;
            ++bytes;
        }
        return bytes;
    }
    // Writes `length` low 7 bits first; a set high bit (128) means "another byte follows".
    // Returns the position just after the varint, where the characters go.
    static char* write_varint(char* out, size_t length) {
        while (length >= 128) {
            *out++ = static_cast<char>((length & 127) | 128);
            length >>= 7;
        }
        *out++ = static_cast<char>(length);
        return out;
    }
    // Returns the word stored in an entry by decoding its varint length.
    static string_view word_of(const char* entry) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(entry) + kLengthOffset;
        size_t length = *p++;
        if (length >= 128) {                 // rare case: 128+ characters, more bytes follow
            length &= 127;
            for (unsigned shift = 7;; shift += 7) {
                const unsigned char byte = *p++;
                length |= static_cast<size_t>(byte & 127) << shift;
                if (byte < 128) break;
            }
        }
        return {reinterpret_cast<const char*>(p), length};
    }
    // True when the entry holds exactly `word` (length first, then the characters).
    static bool same(const char* entry, string_view word) {
        const string_view stored = word_of(entry);
        return stored.size() == word.size()
            && char_traits<char>::compare(stored.data(), word.data(), word.size()) == 0;
    }

    // Allocates one entry holding an owned copy of `word` and its ListId.
    static char* make_entry(string_view word, ListId list) {
        const size_t bytes = kLengthOffset + varint_size(word.size()) + word.size();
        char* entry = static_cast<char*>(memory::allocate(bytes));
        set_list(entry, list);
        char* text = write_varint(entry + kLengthOffset, word.size());
        char_traits<char>::copy(text, word.data(), word.size());
        return entry;
    }

    // Linear probing from the home slot of hash h. Returns the slot that holds `word`,
    // or the first empty slot (where `word` would be inserted). Requires table_ != nullptr.
    size_t locate(string_view word, uint64_t h) const {
        const size_t mask = cap_ - 1;
        size_t i = h >> shift_;
        while (table_[i] && !same(table_[i], word)) i = (i + 1) & mask;
        return i;
    }

    // Doubles the table (or creates the first one). The new array is filled before
    // the old one is freed, so a failed allocation leaves the index unchanged.
    void grow() {
        const size_t new_cap = cap_ ? cap_ * 2 : size_t(1) << kInitialBits;
        const unsigned new_shift = cap_ ? shift_ - 1 : 64 - kInitialBits;
        char** fresh = static_cast<char**>(memory::allocate(new_cap * sizeof(char*)));
        for (size_t i = 0; i < new_cap; ++i) fresh[i] = nullptr;
        // Move every pointer to its new home (hash recomputed from the stored word).
        for (size_t i = 0; i < cap_; ++i) {
            if (!table_[i]) continue;
            size_t k = hash_of(word_of(table_[i])) >> new_shift;
            while (fresh[k]) k = (k + 1) & (new_cap - 1);
            fresh[k] = table_[i];
        }
        memory::deallocate(table_);
        table_ = fresh;
        cap_ = new_cap;
        shift_ = new_shift;
    }

public:
    StudentIndex() {}   // empty: no table is allocated until the first put
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;

    void put(string_view word, ListId list) override {
        const uint64_t h = hash_of(word);
        size_t i = 0;
        if (table_) {
            i = locate(word, h);
            if (table_[i]) {                 // existing word: only replace its ListId
                set_list(table_[i], list);
                return;
            }
        }
        // New word: grow first if needed, then find its empty slot in the final table.
        if (count_ + 1 > cap_ * kMaxLoadNum / kMaxLoadDen) {
            grow();
            i = locate(word, h);
        }
        table_[i] = make_entry(word, list);
        ++count_;
    }

    bool find(string_view word, ListId& result) override {
        if (!table_) return false;
        const char* entry = table_[locate(word, hash_of(word))];
        if (!entry) return false;            // miss: result is left untouched
        result = list_of(entry);
        return true;
    }

    bool erase(string_view word) override {
        if (!table_) return false;
        size_t hole = locate(word, hash_of(word));
        if (!table_[hole]) return false;
        memory::deallocate(table_[hole]);
        table_[hole] = nullptr;
        --count_;
        // Backward-shift deletion: walk the rest of the cluster and pull entries back
        // into the hole when the hole lies on their probe path.
        const size_t mask = cap_ - 1;
        for (size_t j = (hole + 1) & mask; table_[j]; j = (j + 1) & mask) {
            const size_t home = hash_of(word_of(table_[j])) >> shift_;
            // (j - home) is how far the entry is from its home; (j - hole) is how far the
            // hole is behind it. If the home is NOT cyclically in (hole, j], it may move.
            if (((j - home) & mask) >= ((j - hole) & mask)) {
                table_[hole] = table_[j];
                table_[j] = nullptr;
                hole = j;
            }
        }
        return true;
    }

    size_t size() const override { return count_; }

    void clear() override {
        for (size_t i = 0; i < cap_; ++i) memory::deallocate(table_[i]);  // nullptr is a no-op
        memory::deallocate(table_);
        table_ = nullptr;
        cap_ = 0;
        count_ = 0;
        shift_ = 0;
    }

    ~StudentIndex() override { clear(); }
};

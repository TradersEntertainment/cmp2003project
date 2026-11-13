namespace findfast {
// Open-addressing hash table with linear probing and a one-byte control array.
// The table is ONE allocation: `capacity_` record pointers (8 bytes each)
// followed by `capacity_` control bytes (1 byte each). Pointers come first so
// they stay 8-byte aligned. A control byte is 0 for an empty slot; for a full
// slot it is a fingerprint: the low 7 bits of the word's hash with bit 7 set
// (so it is never 0). A search scans control bytes and only follows the
// pointer when the fingerprint matches. Each word lives in its own record:
//   [ListId list (4)][length as varint][characters]  = 5 + L bytes when L < 128
// Erase uses backward-shift deletion, so there are no tombstones.
class StudentIndex final : public Index {
    static constexpr size_t kInitialBits = 3;   // first table has 2^3 = 8 slots
    static constexpr size_t kMaxLoadNum = 7;    // grow when words would exceed
    static constexpr size_t kMaxLoadDen = 8;    //   kMaxLoadNum / kMaxLoadDen of the slots
    static_assert(kMaxLoadNum < kMaxLoadDen, "at least one slot must stay empty");
    static constexpr unsigned char kEmpty = 0;                // control byte of an empty slot
    static constexpr size_t kSlotBytes = sizeof(char*) + 1;   // one pointer + one control byte

    char** slots_ = nullptr;          // record pointers; a pointer is valid only when its slot is full
    unsigned char* ctrl_ = nullptr;   // control bytes, stored right after the pointers
    size_t capacity_ = 0;             // number of slots: 0 or a power of two
    size_t count_ = 0;                // number of distinct words
    unsigned shift_ = 0;              // home slot = hash >> shift_ (uses the top bits of the hash)

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

    // Fingerprint of a hash: its low 7 bits plus bit 7 (128), so it never equals kEmpty.
    // The home slot uses the top bits, so the fingerprint adds independent information.
    static unsigned char fingerprint(uint64_t h) {
        return static_cast<unsigned char>((h & 127) | 128);
    }

    // The ListId is at the start of a record; char_traits copy avoids any aliasing issue.
    static ListId list_of(const char* record) {
        ListId list;
        char_traits<char>::copy(reinterpret_cast<char*>(&list), record, sizeof list);
        return list;
    }
    static void set_list(char* record, ListId list) {
        char_traits<char>::copy(record, reinterpret_cast<const char*>(&list), sizeof list);
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
    // Returns the word stored in a record by decoding its varint length.
    static string_view word_of(const char* record) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(record) + sizeof(ListId);
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
    // True when the record holds exactly `word` (same length, same characters).
    static bool holds(const char* record, string_view word) {
        const string_view stored = word_of(record);
        return stored.size() == word.size()
            && char_traits<char>::compare(stored.data(), word.data(), word.size()) == 0;
    }

    // Allocates one record holding an owned copy of `word`.
    static char* make_record(string_view word, ListId list) {
        const size_t bytes = sizeof(ListId) + varint_size(word.size()) + word.size();
        char* record = static_cast<char*>(memory::allocate(bytes));
        set_list(record, list);
        char* text = write_varint(record + sizeof(ListId), word.size());
        char_traits<char>::copy(text, word.data(), word.size());
        return record;
    }

    // The slot where a word with hash h starts its probe.
    size_t home(uint64_t h) const { return static_cast<size_t>(h >> shift_); }

    // Probes from the home slot. Returns true and sets `slot` when `word` is stored there;
    // otherwise returns false and sets `slot` to the empty slot that ended the probe.
    // Only a matching fingerprint makes it read the record. Requires a table.
    bool locate(string_view word, uint64_t h, size_t& slot) const {
        const unsigned char print = fingerprint(h);
        const size_t mask = capacity_ - 1;
        for (size_t i = home(h);; i = (i + 1) & mask) {
            const unsigned char c = ctrl_[i];
            if (c == kEmpty) {               // the word would have been placed here: miss
                slot = i;
                return false;
            }
            if (c == print && holds(slots_[i], word)) {
                slot = i;
                return true;
            }
        }
    }
    // The first empty slot at or after the home slot of hash h.
    size_t first_empty(uint64_t h) const {
        const size_t mask = capacity_ - 1;
        size_t i = home(h);
        while (ctrl_[i] != kEmpty) i = (i + 1) & mask;
        return i;
    }

    // Replaces the table by an empty one of 2^bits slots and reinserts every record.
    // The new table is allocated first, so on failure the old one is left untouched.
    void grow(size_t bits) {
        const size_t capacity = size_t(1) << bits;
        unsigned char* raw = static_cast<unsigned char*>(memory::allocate(capacity * kSlotBytes));
        char** old_slots = slots_;
        unsigned char* old_ctrl = ctrl_;
        const size_t old_capacity = capacity_;
        slots_ = reinterpret_cast<char**>(raw);              // pointers at the start (aligned)
        ctrl_ = raw + capacity * sizeof(char*);              // control bytes after them
        capacity_ = capacity;
        shift_ = static_cast<unsigned>(64 - bits);
        for (size_t i = 0; i < capacity; ++i) ctrl_[i] = kEmpty;
        // Reinsert: the home is recomputed from the stored word; the fingerprint does not
        // depend on the table size, so the old control byte is copied as it is.
        for (size_t i = 0; i < old_capacity; ++i) {
            if (old_ctrl[i] == kEmpty) continue;
            const size_t slot = first_empty(hash_of(word_of(old_slots[i])));
            slots_[slot] = old_slots[i];
            ctrl_[slot] = old_ctrl[i];
        }
        memory::deallocate(old_slots);      // frees the old pointers and control bytes together
    }

public:
    StudentIndex() {}   // empty: no table is allocated until the first put
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;

    void put(string_view word, ListId list) override {
        const uint64_t h = hash_of(word);
        size_t slot = 0;
        if (slots_ && locate(word, h, slot)) {   // existing word: only replace its ListId
            set_list(slots_[slot], list);
            return;
        }
        // New word: grow first if one more word would pass the maximum load,
        // then find its empty slot in the new table.
        if (!slots_) {
            grow(kInitialBits);
            slot = first_empty(h);
        } else if ((count_ + 1) * kMaxLoadDen > capacity_ * kMaxLoadNum) {
            grow(64 - shift_ + 1);              // double the number of slots
            slot = first_empty(h);
        }
        slots_[slot] = make_record(word, list);
        ctrl_[slot] = fingerprint(h);
        ++count_;
    }

    bool find(string_view word, ListId& result) override {
        if (!slots_) return false;
        size_t slot = 0;
        if (!locate(word, hash_of(word), slot)) return false;   // miss: result is left untouched
        result = list_of(slots_[slot]);
        return true;
    }

    bool erase(string_view word) override {
        if (!slots_) return false;
        size_t hole = 0;
        if (!locate(word, hash_of(word), hole)) return false;
        memory::deallocate(slots_[hole]);
        --count_;
        // Backward shift: walk the run after the hole. An entry may move back into the
        // hole only if its home is not after the hole, i.e. its distance from home is at
        // least the distance from the hole. Then the moved entry's old slot is the new hole.
        const size_t mask = capacity_ - 1;
        for (size_t j = (hole + 1) & mask; ctrl_[j] != kEmpty; j = (j + 1) & mask) {
            const size_t from_home = (j - home(hash_of(word_of(slots_[j])))) & mask;
            const size_t from_hole = (j - hole) & mask;
            if (from_home >= from_hole) {
                slots_[hole] = slots_[j];
                ctrl_[hole] = ctrl_[j];
                hole = j;
            }
        }
        ctrl_[hole] = kEmpty;
        return true;
    }

    size_t size() const override { return count_; }

    void clear() override {
        for (size_t i = 0; i < capacity_; ++i)
            if (ctrl_[i] != kEmpty) memory::deallocate(slots_[i]);
        memory::deallocate(slots_);          // one call frees pointers and control bytes
        slots_ = nullptr;
        ctrl_ = nullptr;
        capacity_ = 0;
        count_ = 0;
        shift_ = 0;
    }

    ~StudentIndex() override { clear(); }
};


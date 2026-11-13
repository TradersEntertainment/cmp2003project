namespace findfast {
// Chained hash table with compact nodes and a one-byte fingerprint.
// buckets_ is a power-of-two array of chain heads. Each word lives in ONE node
// allocation laid out as:
//   [Node* next (8)][ListId list (4)][fingerprint (1)][length as varint][characters]
// The fingerprint is the low 8 bits of the word's 64-bit hash, while the bucket
// uses the top bits, so the two are independent. A search skips a node at once
// when its fingerprint differs; only on a match does it decode the length and
// compare characters. The length is base-128 (1 byte for words under 128
// characters), so a normal node costs 14 + L bytes.
class StudentIndex final : public Index {
    // The fixed part of a node; the other fields follow it in the same allocation.
    struct Node { Node* next; };

    static constexpr size_t kListOffset = sizeof(Node);                          // where the ListId starts
    static constexpr size_t kPrintOffset = kListOffset + sizeof(ListId);         // where the fingerprint byte is
    static constexpr size_t kLengthOffset = kPrintOffset + 1;                    // where the varint starts
    static constexpr unsigned kInitialBits = 3;   // first table has 2^3 = 8 buckets
    static constexpr double kMaxLoad = 2.0;       // grow when words > kMaxLoad * buckets

    Node** buckets_ = nullptr;   // chain heads; nullptr while the index is empty
    size_t bucket_count_ = 0;    // always 0 or a power of two
    size_t count_ = 0;           // number of distinct words
    unsigned shift_ = 0;         // bucket = hash >> shift_ (uses the top bits of the hash)

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

    // True when the n characters at a and b are equal; compares 8 characters per step.
    static bool equal_text(const char* a, const char* b, size_t n) {
        if (n >= 8) {
            for (size_t i = 0; i + 8 < n; i += 8)
                if (load8(a + i) != load8(b + i)) return false;
            return load8(a + n - 8) == load8(b + n - 8);   // last 8 (may overlap)
        }
        if (n >= 4) return load4(a) == load4(b) && load4(a + n - 4) == load4(b + n - 4);
        for (size_t i = 0; i < n; ++i)
            if (a[i] != b[i]) return false;
        return true;
    }

    // The fingerprint of a hash: its low 8 bits.
    static unsigned char fingerprint(uint64_t h) { return static_cast<unsigned char>(h); }
    // Reads the fingerprint byte stored in node n (unsigned char access is always allowed).
    static unsigned char fingerprint_of(const Node* n) {
        return reinterpret_cast<const unsigned char*>(n)[kPrintOffset];
    }

    // The ListId sits right after the next pointer; char_traits copy avoids any aliasing issue.
    static ListId list_of(const Node* n) {
        ListId list;
        char_traits<char>::copy(reinterpret_cast<char*>(&list),
                                reinterpret_cast<const char*>(n) + kListOffset, sizeof list);
        return list;
    }
    static void set_list(Node* n, ListId list) {
        char_traits<char>::copy(reinterpret_cast<char*>(n) + kListOffset,
                                reinterpret_cast<const char*>(&list), sizeof list);
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
    // Returns the word stored in node n by decoding its varint length.
    static string_view word_of(const Node* n) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(n) + kLengthOffset;
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
    // True when node n holds exactly `word`. The cheap fingerprint test comes first;
    // only when it matches are the length and the characters compared.
    static bool same(const Node* n, unsigned char print, string_view word) {
        if (fingerprint_of(n) != print) return false;
        const string_view stored = word_of(n);
        return stored.size() == word.size() && equal_text(stored.data(), word.data(), word.size());
    }

    // Allocates one node holding an owned copy of `word`, linked in front of `next`.
    static Node* make_node(string_view word, ListId list, unsigned char print, Node* next) {
        const size_t bytes = kLengthOffset + varint_size(word.size()) + word.size();
        char* raw = static_cast<char*>(memory::allocate(bytes));
        Node* n = ::new (raw) Node{next};
        set_list(n, list);
        raw[kPrintOffset] = static_cast<char>(print);
        char* text = write_varint(raw + kLengthOffset, word.size());
        char_traits<char>::copy(text, word.data(), word.size());
        return n;
    }
    // Frees every node of a chain.
    static void free_chain(Node* n) {
        while (n) {
            Node* next = n->next;
            memory::deallocate(n);
            n = next;
        }
    }

    // Moves all nodes into a new array of 2^bits buckets.
    void rehash(unsigned bits) {
        // 1. Thread every node onto one temporary list using their next pointers.
        Node* all = nullptr;
        for (size_t i = 0; i < bucket_count_; ++i) {
            Node* n = buckets_[i];
            while (n) {
                Node* next = n->next;
                n->next = all;
                all = n;
                n = next;
            }
        }
        // 2. Free the old array BEFORE allocating the new one, so both never coexist.
        memory::deallocate(buckets_);
        buckets_ = nullptr;
        const size_t new_count = size_t(1) << bits;
        try {
            buckets_ = static_cast<Node**>(memory::allocate(new_count * sizeof(Node*)));
        } catch (...) {                      // out of memory: release everything, leave a valid empty index
            free_chain(all);
            bucket_count_ = 0;
            count_ = 0;
            shift_ = 0;
            throw;
        }
        bucket_count_ = new_count;
        shift_ = 64 - bits;
        for (size_t i = 0; i < new_count; ++i) buckets_[i] = nullptr;
        // 3. Put each node into its new bucket; the hash is recomputed from the stored characters.
        while (all) {
            Node* n = all;
            all = n->next;
            Node*& head = buckets_[hash_of(word_of(n)) >> shift_];
            n->next = head;
            head = n;
        }
    }

public:
    StudentIndex() {}   // empty: no buckets are allocated until the first put
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;

    void put(string_view word, ListId list) override {
        const uint64_t h = hash_of(word);
        const unsigned char print = fingerprint(h);
        if (buckets_) {                      // existing word: only replace its ListId
            for (Node* n = buckets_[h >> shift_]; n; n = n->next) {
                if (same(n, print, word)) {
                    set_list(n, list);
                    return;
                }
            }
        }
        // New word: grow first, so the bucket below is computed for the final table.
        if (!buckets_) rehash(kInitialBits);
        else if (count_ + 1 > kMaxLoad * bucket_count_) rehash(64 - shift_ + 1);
        Node*& head = buckets_[h >> shift_];
        head = make_node(word, list, print, head);
        ++count_;
    }

    bool find(string_view word, ListId& result) override {
        if (!buckets_) return false;
        const uint64_t h = hash_of(word);
        const unsigned char print = fingerprint(h);
        for (Node* n = buckets_[h >> shift_]; n; n = n->next) {
            if (same(n, print, word)) {
                result = list_of(n);
                return true;
            }
        }
        return false;                        // miss: result is left untouched
    }

    bool erase(string_view word) override {
        if (!buckets_) return false;
        const uint64_t h = hash_of(word);
        const unsigned char print = fingerprint(h);
        // `link` points at the pointer that leads to the current node, so unlinking is one store.
        for (Node** link = &buckets_[h >> shift_]; *link; link = &(*link)->next) {
            Node* n = *link;
            if (same(n, print, word)) {
                *link = n->next;
                memory::deallocate(n);
                --count_;
                return true;
            }
        }
        return false;
    }

    size_t size() const override { return count_; }

    void clear() override {
        for (size_t i = 0; i < bucket_count_; ++i) free_chain(buckets_[i]);
        memory::deallocate(buckets_);
        buckets_ = nullptr;
        bucket_count_ = 0;
        count_ = 0;
        shift_ = 0;
    }

    ~StudentIndex() override { clear(); }
};


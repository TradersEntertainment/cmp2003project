namespace findfast {
// Chained hash table with compact nodes that remember their hash.
// buckets_ is a power-of-two array of chain heads. Each word lives in ONE node
// allocation laid out as:
//   [Node* next (8)][uint32_t hash (4)][ListId list (4)][length as varint][characters]
// The length uses base-128 (1 byte for words shorter than 128 characters), so a
// normal node costs 17 + L bytes. The stored hash makes most mismatches cheap
// (one integer compare) and lets rehash move nodes without reading their words.
class StudentIndex final : public Index {
    // The fixed part of a node; the varint length and the characters follow it
    // in the same allocation (memory::allocate returns suitably aligned storage).
    struct Node {
        Node* next;      // next node in the same bucket
        uint32_t hash;   // high 32 bits of the word's 64-bit hash
        ListId list;     // the value stored for this word
    };

    static constexpr size_t kLengthOffset = sizeof(Node); // where the varint starts (16)
    static constexpr unsigned kInitialBits = 3;           // first table has 2^3 = 8 buckets
    static constexpr double kMaxLoad = 2.0;               // grow when words reach kMaxLoad * buckets

    Node** buckets_ = nullptr;   // chain heads; nullptr while the index is empty
    size_t bucket_count_ = 0;    // always 0 or a power of two
    size_t count_ = 0;           // number of distinct words
    size_t grow_at_ = 0;         // word count that triggers the next doubling
    unsigned shift_ = 0;         // bucket = hash >> shift_ (shift_ = 32 - log2(bucket_count_))

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
    // The 32-bit hash we store and use: the high half of the 64-bit hash.
    static uint32_t hash32(string_view word) {
        return static_cast<uint32_t>(hash_of(word) >> 32);
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
    // True when node n holds exactly `word`: hash first (cheap), then length, then characters.
    static bool same(const Node* n, uint32_t h, string_view word) {
        if (n->hash != h) return false;
        const string_view stored = word_of(n);
        return stored.size() == word.size()
            && char_traits<char>::compare(stored.data(), word.data(), word.size()) == 0;
    }

    // Allocates one node holding an owned copy of `word`, linked in front of `next`.
    static Node* make_node(string_view word, uint32_t h, ListId list, Node* next) {
        const size_t bytes = kLengthOffset + varint_size(word.size()) + word.size();
        char* raw = static_cast<char*>(memory::allocate(bytes));
        Node* n = ::new (raw) Node{next, h, list};
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

    // Bucket index of a 32-bit hash: its top log2(bucket_count_) bits.
    size_t bucket_of(uint32_t h) const { return h >> shift_; }

    // Moves all nodes into a new array of 2^bits buckets (bits is at most 32).
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
            grow_at_ = 0;
            throw;
        }
        bucket_count_ = new_count;
        shift_ = 32 - bits;
        grow_at_ = static_cast<size_t>(kMaxLoad * static_cast<double>(new_count));
        for (size_t i = 0; i < new_count; ++i) buckets_[i] = nullptr;
        // 3. Put each node into its new bucket using its stored hash (words are not re-read).
        while (all) {
            Node* n = all;
            all = n->next;
            Node*& head = buckets_[bucket_of(n->hash)];
            n->next = head;
            head = n;
        }
    }

public:
    StudentIndex() {}   // empty: no buckets are allocated until the first put
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;

    void put(string_view word, ListId list) override {
        const uint32_t h = hash32(word);
        if (buckets_) {                      // existing word: only replace its ListId
            for (Node* n = buckets_[bucket_of(h)]; n; n = n->next) {
                if (same(n, h, word)) {
                    n->list = list;
                    return;
                }
            }
        }
        // New word: grow first, so the bucket below is computed for the final table.
        // (shift_ == 0 means 2^32 buckets, the most a 32-bit hash can address.)
        if (!buckets_) rehash(kInitialBits);
        else if (count_ >= grow_at_ && shift_ > 0) rehash(32 - shift_ + 1);
        Node*& head = buckets_[bucket_of(h)];
        head = make_node(word, h, list, head);
        ++count_;
    }

    bool find(string_view word, ListId& result) override {
        if (!buckets_) return false;
        const uint32_t h = hash32(word);
        for (const Node* n = buckets_[bucket_of(h)]; n; n = n->next) {
            if (same(n, h, word)) {
                result = n->list;
                return true;
            }
        }
        return false;                        // miss: result is left untouched
    }

    bool erase(string_view word) override {
        if (!buckets_) return false;
        const uint32_t h = hash32(word);
        // `link` points at the pointer that leads to the current node, so unlinking is one store.
        for (Node** link = &buckets_[bucket_of(h)]; *link; link = &(*link)->next) {
            Node* n = *link;
            if (same(n, h, word)) {
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
        grow_at_ = 0;
        shift_ = 0;
    }

    ~StudentIndex() override { clear(); }
};

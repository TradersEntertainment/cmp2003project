namespace findfast {
// Chained hash table with compact nodes and any bucket count (not only powers of two).
// buckets_ is an array of chain heads. Each word lives in ONE node allocation:
//   [Node* next (8 bytes)][ListId list (4 bytes)][length as varint][characters]
// The length uses base-128 (1 byte for words shorter than 128 characters), so a
// normal node costs 13 + L bytes. No hash is stored: it is recomputed when needed.
// A hash is mapped to a bucket with a multiply-and-shift ("fast range") instead of
// a modulo, so the table can grow by a factor of 1.5 instead of doubling.
class StudentIndex final : public Index {
    // The fixed part of a node; the other fields follow it in the same allocation.
    struct Node { Node* next; };

    static constexpr size_t kListOffset = sizeof(Node);                  // where the ListId starts
    static constexpr size_t kLengthOffset = kListOffset + sizeof(ListId); // where the varint starts
    static constexpr size_t kInitialBuckets = 8;  // size of the first bucket array
    static constexpr size_t kLoadNum = 2;         // maximum load = kLoadNum / kLoadDen words per bucket
    static constexpr size_t kLoadDen = 1;
    static constexpr size_t kGrowNum = 3;         // on growth the table gets kGrowNum / kGrowDen
    static constexpr size_t kGrowDen = 2;         //   times the buckets the maximum load needs
    static_assert(kGrowNum > kGrowDen, "growth must leave the load below the maximum");
    // bucket_of multiplies a 32-bit number by bucket_count_; keeping bucket_count_ <= 2^32
    // guarantees the product fits in 64 bits.
    static constexpr size_t kMaxBuckets = size_t(1) << 32;

    Node** buckets_ = nullptr;   // chain heads; nullptr while the index is empty
    size_t bucket_count_ = 0;    // 0 while empty, otherwise between 8 and kMaxBuckets
    size_t count_ = 0;           // number of distinct words

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

    // Maps a hash to a bucket in [0, bucket_count_) without division: the top 32 bits
    // of the hash are a fraction x / 2^32, and x * bucket_count_ / 2^32 scales it to the table.
    size_t bucket_of(uint64_t h) const {
        return static_cast<size_t>(((h >> 32) * bucket_count_) >> 32);
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
    // True when node n holds exactly `word` (length first, then the characters).
    static bool same(const Node* n, string_view word) {
        const string_view stored = word_of(n);
        return stored.size() == word.size()
            && char_traits<char>::compare(stored.data(), word.data(), word.size()) == 0;
    }

    // Allocates one node holding an owned copy of `word`, linked in front of `next`.
    static Node* make_node(string_view word, ListId list, Node* next) {
        const size_t bytes = kLengthOffset + varint_size(word.size()) + word.size();
        char* raw = static_cast<char*>(memory::allocate(bytes));
        Node* n = ::new (raw) Node{next};
        set_list(n, list);
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

    // Bucket count used when the table grows to hold `words` words:
    // ceil(words * (kGrowNum / kGrowDen) / (kLoadNum / kLoadDen)), at most kMaxBuckets.
    static size_t grown_count(size_t words) {
        const size_t top = words * kGrowNum * kLoadDen;
        const size_t bottom = kGrowDen * kLoadNum;
        const size_t wanted = (top + bottom - 1) / bottom;   // integer ceiling of top / bottom
        return wanted < kMaxBuckets ? wanted : kMaxBuckets;
    }

    // Moves all nodes into a new array of new_count buckets.
    void rehash(size_t new_count) {
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
        try {
            buckets_ = static_cast<Node**>(memory::allocate(new_count * sizeof(Node*)));
        } catch (...) {                      // out of memory: release everything, leave a valid empty index
            free_chain(all);
            bucket_count_ = 0;
            count_ = 0;
            throw;
        }
        bucket_count_ = new_count;
        for (size_t i = 0; i < new_count; ++i) buckets_[i] = nullptr;
        // 3. Put each node into its new bucket; the hash is recomputed from the stored characters.
        while (all) {
            Node* n = all;
            all = n->next;
            Node*& head = buckets_[bucket_of(hash_of(word_of(n)))];
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
        if (buckets_) {                      // existing word: only replace its ListId
            for (Node* n = buckets_[bucket_of(h)]; n; n = n->next) {
                if (same(n, word)) {
                    set_list(n, list);
                    return;
                }
            }
        }
        // New word: grow first, so the bucket below is computed for the final table.
        // Growth happens when (count_ + 1) / bucket_count_ would exceed kLoadNum / kLoadDen.
        if (!buckets_) {
            rehash(kInitialBuckets);
        } else if ((count_ + 1) * kLoadDen > kLoadNum * bucket_count_ && bucket_count_ < kMaxBuckets) {
            rehash(grown_count(count_ + 1));
        }
        Node*& head = buckets_[bucket_of(h)];
        head = make_node(word, list, head);
        ++count_;
    }

    bool find(string_view word, ListId& result) override {
        if (!buckets_) return false;
        for (Node* n = buckets_[bucket_of(hash_of(word))]; n; n = n->next) {
            if (same(n, word)) {
                result = list_of(n);
                return true;
            }
        }
        return false;                        // miss: result is left untouched
    }

    bool erase(string_view word) override {
        if (!buckets_) return false;
        // `link` points at the pointer that leads to the current node, so unlinking is one store.
        for (Node** link = &buckets_[bucket_of(hash_of(word))]; *link; link = &(*link)->next) {
            Node* n = *link;
            if (same(n, word)) {
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
    }

    ~StudentIndex() override { clear(); }
};


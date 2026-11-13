namespace findfast {
// Hash table with separate chaining: every bucket is a singly linked list of nodes.
// Each node is ONE allocation: a small header followed directly by the word's characters.
class StudentIndex final : public Index {
    struct Node {
        Node* next;     // next node in the same bucket (nullptr at the end of the chain)
        uint32_t hash;  // high 32 bits of the word's 64-bit hash
        ListId list;    // the value stored for the word
        uint32_t len;   // word length, or kLongLength if the full length is stored before the text
        char start[4];  // first bytes of the text area; the rest follows in the same allocation
    };
    // Bytes before the text area (20 on 64-bit, so the struct's tail padding holds characters).
    static constexpr size_t kHeader = offsetof(Node, start);
    // Marks a length that does not fit in 32 bits; the full size_t length is then
    // copied into the first bytes of the text area, and the characters follow it.
    static constexpr uint32_t kLongLength = 0xFFFFFFFFu;
    static constexpr size_t kInitialBuckets = 8;            // bucket count after the first put
    static constexpr size_t kMaxLoad = 2;                   // at most kMaxLoad words per bucket on average
    static constexpr size_t kMaxBuckets = size_t(1) << 31;  // the bucket index uses at most 31 hash bits

    Node** buckets_ = nullptr;  // array of chain heads (nullptr while the table is empty)
    size_t bucket_count_ = 0;   // 0 or a power of two
    size_t count_ = 0;          // number of distinct words stored
    unsigned shift_ = 0;        // 32 - log2(bucket_count_), so bucket = hash >> shift_

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

    // We keep the high 32 bits of the 64-bit hash; the top bits of those choose the bucket.
    static uint32_t hash32(string_view w) { return static_cast<uint32_t>(hash_of(w) >> 32); }
    size_t bucket_of(uint32_t hash) const { return hash >> shift_; }

    static char* bytes(Node* n) { return reinterpret_cast<char*>(n); }
    static bool is_long(const Node* n) { return n->len == kLongLength; }
    // The real length of the stored word.
    static size_t length_of(Node* n) {
        if (!is_long(n)) return n->len;
        size_t length;
        char_traits<char>::copy(reinterpret_cast<char*>(&length), bytes(n) + kHeader, sizeof length);
        return length;
    }
    // The first character of the stored word (it comes after the full length for long words).
    static char* text(Node* n) { return bytes(n) + kHeader + (is_long(n) ? sizeof(size_t) : 0); }
    // True if node n holds word w; the cheap hash and length tests run before the character compare.
    static bool same(Node* n, uint32_t h, string_view w) {
        return n->hash == h && length_of(n) == w.size()
            && char_traits<char>::compare(text(n), w.data(), w.size()) == 0;
    }

    // Allocates one node that holds an owned copy of w.
    static Node* make_node(string_view w, uint32_t h, ListId list) {
        const bool long_word = w.size() >= kLongLength;
        size_t block = kHeader + (long_word ? sizeof(size_t) : 0) + w.size();
        if (block < sizeof(Node)) block = sizeof(Node);  // the whole struct must fit in its block
        Node* n = ::new (memory::allocate(block)) Node;  // start the node's lifetime in the block
        n->next = nullptr;
        n->hash = h;
        n->list = list;
        if (long_word) {
            n->len = kLongLength;
            const size_t length = w.size();
            char_traits<char>::copy(bytes(n) + kHeader, reinterpret_cast<const char*>(&length), sizeof length);
        } else {
            n->len = static_cast<uint32_t>(w.size());
        }
        char_traits<char>::copy(text(n), w.data(), w.size());
        return n;
    }

    // Frees every node of one chain.
    static void release_chain(Node* n) {
        while (n) {
            Node* next = n->next;
            memory::deallocate(n);
            n = next;
        }
    }

    // Moves every node into a new array of new_count buckets (a power of two).
    // The old array is freed BEFORE the new one is allocated, so the two never coexist.
    void rehash(size_t new_count) {
        Node* all = nullptr;  // 1. thread every node into one temporary list
        for (size_t i = 0; i < bucket_count_; ++i) {
            Node* n = buckets_[i];
            while (n) {
                Node* next = n->next;
                n->next = all;
                all = n;
                n = next;
            }
        }
        memory::deallocate(buckets_);  // 2. free the old array, then allocate the new one
        buckets_ = nullptr;
        bucket_count_ = 0;
        try {
            buckets_ = static_cast<Node**>(memory::allocate(new_count * sizeof(Node*)));
        } catch (...) {  // out of memory: free the words so nothing leaks, leave an empty index
            release_chain(all);
            count_ = 0;
            throw;
        }
        for (size_t i = 0; i < new_count; ++i) buckets_[i] = nullptr;
        bucket_count_ = new_count;
        shift_ = 32;
        for (size_t c = new_count; c > 1; c >>= 1) --shift_;  // shift_ = 32 - log2(new_count)
        while (all) {  // 3. push each node onto the head of its new bucket
            Node* next = all->next;
            Node*& head = buckets_[bucket_of(all->hash)];
            all->next = head;
            head = all;
            all = next;
        }
    }

public:
    StudentIndex() {}  // empty table: no dynamic storage until the first put
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;

    void put(string_view w, ListId list) override {
        const uint32_t h = hash32(w);
        // 1. An existing word only gets its ListId replaced.
        if (bucket_count_) {
            for (Node* n = buckets_[bucket_of(h)]; n; n = n->next)
                if (same(n, h, w)) { n->list = list; return; }
        }
        // 2. A new word: create the table, or double it when it is full.
        if (!bucket_count_) rehash(kInitialBuckets);
        else if (count_ >= bucket_count_ * kMaxLoad && bucket_count_ < kMaxBuckets) rehash(bucket_count_ * 2);
        // 3. Insert the new node at the head of its bucket.
        Node* n = make_node(w, h, list);
        Node*& head = buckets_[bucket_of(h)];
        n->next = head;
        head = n;
        ++count_;
    }

    bool find(string_view w, ListId& result) override {
        if (!bucket_count_) return false;
        const uint32_t h = hash32(w);
        for (Node* n = buckets_[bucket_of(h)]; n; n = n->next)
            if (same(n, h, w)) { result = n->list; return true; }
        return false;  // miss: result is left untouched
    }

    bool erase(string_view w) override {
        if (!bucket_count_) return false;
        const uint32_t h = hash32(w);
        // link points at the pointer that leads to the current node, so unlinking is one assignment.
        for (Node** link = &buckets_[bucket_of(h)]; *link; link = &(*link)->next) {
            Node* n = *link;
            if (same(n, h, w)) {
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
        for (size_t i = 0; i < bucket_count_; ++i) release_chain(buckets_[i]);
        memory::deallocate(buckets_);  // deallocate(nullptr) does nothing, so clearing twice is safe
        buckets_ = nullptr;
        bucket_count_ = 0;
        count_ = 0;
        shift_ = 0;
    }

    ~StudentIndex() override { clear(); }
};


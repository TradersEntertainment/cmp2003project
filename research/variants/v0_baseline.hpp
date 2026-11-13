namespace findfast {
class StudentIndex final : public Index {
    struct Node { Node* next; uint32_t hash; ListId list; uint32_t len; };
    static constexpr size_t kHeader = 20;
    Node** buckets_ = nullptr; size_t bucket_count_ = 0; size_t count_ = 0;
    static char* text(Node* n) { return reinterpret_cast<char*>(n) + kHeader; }
    static uint32_t hash_of(string_view w) {
        uint32_t h = 2166136261u;
        for (unsigned char c : w) { h ^= c; h *= 16777619u; }
        return h ^ (h >> 15);
    }
    static bool same(Node* n, uint32_t h, string_view w) {
        return n->hash == h && n->len == w.size() && char_traits<char>::compare(text(n), w.data(), w.size()) == 0;
    }
    void rehash(size_t nc) {
        Node* all = nullptr;
        for (size_t i = 0; i < bucket_count_; ++i) for (Node* n = buckets_[i]; n;) { Node* x = n->next; n->next = all; all = n; n = x; }
        memory::deallocate(buckets_);
        buckets_ = static_cast<Node**>(memory::allocate(nc * sizeof(Node*)));
        for (size_t i = 0; i < nc; ++i) buckets_[i] = nullptr;
        bucket_count_ = nc;
        while (all) { Node* x = all->next; size_t b = all->hash & (nc - 1); all->next = buckets_[b]; buckets_[b] = all; all = x; }
    }
public:
    StudentIndex() {}
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;
    void put(string_view w, ListId list) override {
        uint32_t h = hash_of(w);
        if (bucket_count_) for (Node* n = buckets_[h & (bucket_count_ - 1)]; n; n = n->next) if (same(n, h, w)) { n->list = list; return; }
        if (!bucket_count_) rehash(8); else if (count_ + 1 > bucket_count_ * 2) rehash(bucket_count_ * 2);
        Node* n = static_cast<Node*>(memory::allocate(kHeader + w.size()));
        n->hash = h; n->list = list; n->len = static_cast<uint32_t>(w.size());
        char_traits<char>::copy(text(n), w.data(), w.size());
        Node*& head = buckets_[h & (bucket_count_ - 1)]; n->next = head; head = n; ++count_;
    }
    bool find(string_view w, ListId& result) override {
        if (!bucket_count_) return false;
        uint32_t h = hash_of(w);
        for (Node* n = buckets_[h & (bucket_count_ - 1)]; n; n = n->next) if (same(n, h, w)) { result = n->list; return true; }
        return false;
    }
    bool erase(string_view w) override {
        if (!bucket_count_) return false;
        uint32_t h = hash_of(w);
        for (Node** link = &buckets_[h & (bucket_count_ - 1)]; *link; link = &(*link)->next) {
            Node* n = *link;
            if (same(n, h, w)) { *link = n->next; memory::deallocate(n); --count_; return true; }
        }
        return false;
    }
    size_t size() const override { return count_; }
    void clear() override {
        for (size_t i = 0; i < bucket_count_; ++i) for (Node* n = buckets_[i]; n;) { Node* x = n->next; memory::deallocate(n); n = x; }
        memory::deallocate(buckets_); buckets_ = nullptr; bucket_count_ = 0; count_ = 0;
    }
    ~StudentIndex() override { clear(); }
};


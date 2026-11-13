namespace findfast {
// Reference only: open addressing over a Record array, tombstones, load 0.5, std::hash.
class StudentIndex final : public Index {
    Record* t_ = nullptr; unsigned char* st_ = nullptr; size_t cap_ = 0, n_ = 0, used_ = 0;  // st: 0 empty 1 full 2 dead
    static size_t h(string_view w) { return std::hash<string_view>{}(w); }
    void rebuild(size_t nc) {
        Record* ot = t_; unsigned char* os = st_; size_t oc = cap_;
        t_ = static_cast<Record*>(memory::allocate(nc * sizeof(Record))); st_ = static_cast<unsigned char*>(memory::allocate(nc));
        for (size_t i = 0; i < nc; ++i) { ::new (&t_[i]) Record(); st_[i] = 0; }
        cap_ = nc; used_ = n_;
        for (size_t i = 0; i < oc; ++i) if (os[i] == 1) { size_t k = h(ot[i].word.view()) & (nc - 1); while (st_[k]) k = (k + 1) & (nc - 1); t_[k].word = static_cast<Key&&>(ot[i].word); t_[k].list = ot[i].list; st_[k] = 1; }
        for (size_t i = 0; i < oc; ++i) ot[i].~Record();
        memory::deallocate(ot); memory::deallocate(os);
    }
    size_t probe(string_view w) const { size_t k = h(w) & (cap_ - 1); while (st_[k]) { if (st_[k] == 1 && t_[k].word.view() == w) return k; k = (k + 1) & (cap_ - 1); } return cap_; }
public:
    StudentIndex() {}
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;
    void put(string_view w, ListId l) override {
        if (cap_) { size_t k = probe(w); if (k < cap_) { t_[k].list = l; return; } }
        if ((used_ + 1) * 2 > cap_) rebuild(cap_ ? (n_ + 1) * 2 > cap_ ? cap_ * 2 : cap_ : 16);
        size_t k = h(w) & (cap_ - 1); while (st_[k] == 1) k = (k + 1) & (cap_ - 1);
        if (st_[k] == 0) ++used_;
        t_[k].word = Key(w); t_[k].list = l; st_[k] = 1; ++n_;
    }
    bool find(string_view w, ListId& r) override { if (!cap_) return false; size_t k = probe(w); if (k == cap_) return false; r = t_[k].list; return true; }
    bool erase(string_view w) override { if (!cap_) return false; size_t k = probe(w); if (k == cap_) return false; t_[k].word = Key(); st_[k] = 2; --n_; return true; }
    size_t size() const override { return n_; }
    void clear() override { for (size_t i = 0; i < cap_; ++i) t_[i].~Record(); memory::deallocate(t_); memory::deallocate(st_); t_ = nullptr; st_ = nullptr; cap_ = n_ = used_ = 0; }
    ~StudentIndex() override { clear(); }
};


namespace findfast {
// Reference only: what a typical team might write (Record nodes + std::hash, load 1).
class StudentIndex final : public Index {
    struct Node { Record rec; Node* next; Node(string_view w, ListId l, Node* n) : rec(w, l), next(n) {} };
    Node** b_ = nullptr; size_t nb_ = 0, n_ = 0;
    static size_t h(string_view w) { return std::hash<string_view>{}(w); }
    void grow() {
        size_t nn = nb_ ? nb_ * 2 : 16;
        Node** nbuf = static_cast<Node**>(memory::allocate(nn * sizeof(Node*)));
        for (size_t i = 0; i < nn; ++i) nbuf[i] = nullptr;
        for (size_t i = 0; i < nb_; ++i) for (Node* x = b_[i]; x;) { Node* nx = x->next; size_t k = h(x->rec.word.view()) % nn; x->next = nbuf[k]; nbuf[k] = x; x = nx; }
        memory::deallocate(b_); b_ = nbuf; nb_ = nn;
    }
public:
    StudentIndex() {}
    StudentIndex(const StudentIndex&) = delete;
    StudentIndex& operator=(const StudentIndex&) = delete;
    void put(string_view w, ListId l) override {
        if (nb_) for (Node* x = b_[h(w) % nb_]; x; x = x->next) if (x->rec.word.view() == w) { x->rec.list = l; return; }
        if (n_ + 1 > nb_) grow();
        size_t k = h(w) % nb_; b_[k] = memory::make<Node>(w, l, b_[k]); ++n_;
    }
    bool find(string_view w, ListId& r) override {
        if (!nb_) return false;
        for (Node* x = b_[h(w) % nb_]; x; x = x->next) if (x->rec.word.view() == w) { r = x->rec.list; return true; }
        return false;
    }
    bool erase(string_view w) override {
        if (!nb_) return false;
        for (Node** p = &b_[h(w) % nb_]; *p; p = &(*p)->next) if ((*p)->rec.word.view() == w) { Node* x = *p; *p = x->next; memory::destroy(x); --n_; return true; }
        return false;
    }
    size_t size() const override { return n_; }
    void clear() override { for (size_t i = 0; i < nb_; ++i) for (Node* x = b_[i]; x;) { Node* nx = x->next; memory::destroy(x); x = nx; } memory::deallocate(b_); b_ = nullptr; nb_ = n_ = 0; }
    ~StudentIndex() override { clear(); }
};


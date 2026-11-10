// avl.hpp -- ADAY 3 (yedek): AVL agaci (kendini dengeleyen ikili arama agaci)
//
// Izin verilen listede hash tablosu OLMAZSA kullanacagimiz yedek. Ayrica
// raporda "neden hash sectik" sorusuna olculmus bir karsilastirma saglar.
// Her islem O(L * log n): log n dugum gezilir, her dugumde kelime kiyaslanir.
#pragma once
#include <cstring>
#include "mock.hpp"

namespace findfast {

class AvlTree {
    struct Node {  // [baslik 24 bayt][harfler]
        Node*         left;
        Node*         right;
        ListId        list;
        std::uint16_t len_lo;  // uzunluk 24 bit'e sigar (4096 karakterlik anahtar icin yeterli)
        std::uint8_t  len_hi;
        std::int8_t   height;  // bu dugumun alt agac yuksekligi
    };
    static constexpr std::size_t kHeader = sizeof(Node);

    Node*       root_ = nullptr;
    std::size_t count_ = 0;

    static std::size_t len(const Node* n) { return n->len_lo | (std::size_t(n->len_hi) << 16); }
    static const char* text(const Node* n) { return reinterpret_cast<const char*>(n) + kHeader; }

    // <0: w kucuk, 0: esit, >0: w buyuk (sozluk sirasi)
    static int compare(std::string_view w, const Node* n) {
        std::size_t nl = len(n), m = w.size() < nl ? w.size() : nl;
        int c = std::memcmp(w.data(), text(n), m);
        if (c != 0) return c;
        return w.size() < nl ? -1 : (w.size() > nl ? 1 : 0);
    }

    static int h(const Node* n) { return n ? n->height : 0; }
    static void fix(Node* n) {
        int a = h(n->left), b = h(n->right);
        n->height = static_cast<std::int8_t>((a > b ? a : b) + 1);
    }
    static Node* rot_right(Node* y) {
        Node* x = y->left;
        y->left = x->right;
        x->right = y;
        fix(y);
        fix(x);
        return x;
    }
    static Node* rot_left(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        y->left = x;
        fix(x);
        fix(y);
        return y;
    }
    // Yukseklik farki 2 olduysa donuslerle dengeyi geri kur (LL, LR, RR, RL).
    static Node* balance(Node* n) {
        fix(n);
        int d = h(n->left) - h(n->right);
        if (d > 1) {
            if (h(n->left->left) < h(n->left->right)) n->left = rot_left(n->left);
            return rot_right(n);
        }
        if (d < -1) {
            if (h(n->right->right) < h(n->right->left)) n->right = rot_right(n->right);
            return rot_left(n);
        }
        return n;
    }

    Node* insert(Node* n, std::string_view w, ListId list) {
        if (!n) {
            Node* x = static_cast<Node*>(memory::allocate(kHeader + w.size()));
            x->left = x->right = nullptr;
            x->list = list;
            x->len_lo = static_cast<std::uint16_t>(w.size());
            x->len_hi = static_cast<std::uint8_t>(w.size() >> 16);
            x->height = 1;
            std::memcpy(reinterpret_cast<char*>(x) + kHeader, w.data(), w.size());
            ++count_;
            return x;
        }
        int c = compare(w, n);
        if (c == 0) {
            n->list = list;
            return n;
        }
        if (c < 0) n->left = insert(n->left, w, list);
        else n->right = insert(n->right, w, list);
        return balance(n);
    }

    // Alt agacin en kucuk dugumunu kopar; koparilan dugum 'min'e yazilir.
    static Node* pop_min(Node* n, Node*& min) {
        if (!n->left) {
            min = n;
            return n->right;
        }
        n->left = pop_min(n->left, min);
        return balance(n);
    }

    Node* remove(Node* n, std::string_view w, bool& removed) {
        if (!n) return nullptr;
        int c = compare(w, n);
        if (c < 0) n->left = remove(n->left, w, removed);
        else if (c > 0) n->right = remove(n->right, w, removed);
        else {
            removed = true;
            --count_;
            Node *l = n->left, *r = n->right;
            memory::deallocate(n);
            if (!r) return l;
            Node* min;  // iki cocuk varsa: sag alt agacin en kucugu yerine gecer
            r = pop_min(r, min);
            min->left = l;
            min->right = r;
            return balance(min);
        }
        return balance(n);
    }

    static void destroy(Node* n) {  // derinlik en cok ~1.44*log2(n): ozyineleme guvenli
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        memory::deallocate(n);
    }

public:
    AvlTree() = default;
    AvlTree(const AvlTree&) = delete;
    AvlTree& operator=(const AvlTree&) = delete;
    ~AvlTree() { clear(); }

    void insert_or_assign(std::string_view w, ListId l) { root_ = insert(root_, w, l); }
    bool lookup(std::string_view w, ListId& out) const {
        const Node* n = root_;
        while (n) {
            int c = compare(w, n);
            if (c == 0) {
                out = n->list;
                return true;
            }
            n = c < 0 ? n->left : n->right;
        }
        return false;
    }
    bool remove(std::string_view w) {
        bool removed = false;
        root_ = remove(root_, w, removed);
        return removed;
    }
    std::size_t size() const { return count_; }
    void clear() {
        destroy(root_);
        root_ = nullptr;
        count_ = 0;
    }
};

class AvlIndex final : public Index {
    AvlTree tree_;

public:
    void put(std::string_view w, ListId l) override { tree_.insert_or_assign(w, l); }
    bool find(std::string_view w, ListId& r) override { return tree_.lookup(w, r); }
    bool erase(std::string_view w) override { return tree_.remove(w); }
    std::size_t size() const override { return tree_.size(); }
    void clear() override { tree_.clear(); }
};

}  // namespace findfast

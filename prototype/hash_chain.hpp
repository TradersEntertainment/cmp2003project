// hash_chain.hpp -- ADAY 1 (favori): Ayrik zincirlemeli hash tablosu (separate chaining)
//
// Fikir: Kelimeyi bir sayiya (hash) ceviririz, sayi bize hangi "kova"ya (bucket)
// bakacagimizi soyler. Ayni kovaya dusen kelimeler tek yonlu bagli liste olur.
// Kova sayisi kayit sayisiyla birlikte buyudugu icin zincirler hep kisa kalir
// ve put / find / erase ortalama O(L) surer (L = kelime uzunlugu), n'den bagimsiz.
#pragma once
#include <cstring>
#include "mock.hpp"

namespace findfast {

class ChainedHashTable {
    // Bir kayit = tek bir bellek blogu: [Node basligi][kelimenin harfleri]
    // Harfleri ayri bir blokta tutmamak hem bellegi hem de onbellek iskalamasini azaltir.
    struct Node {
        Node*         next;  // ayni kovadaki sonraki kayit (yoksa nullptr)
        std::uint32_t hash;  // kelimenin hash'i: buyumede tekrar hesaplamamak ve
                             // karsilastirmada once ucuz sayi kiyasi yapmak icin
        ListId        list;  // kelimenin liste numarasi (tam 32 bit, 0 ve 2^32-1 dahil)
        std::uint32_t len;   // kelime uzunlugu
    };
    // Harfler basligin 20. baytindan baslar (sizeof(Node) 24'tur ama son 4 bayt
    // dolgu; onu da harfler icin kullanip kayit basina 4 bayt kazaniyoruz).
    static constexpr std::size_t kHeader = 20;
    static constexpr std::size_t kInitialBuckets = 8;
    static constexpr std::size_t kMaxLoad = 2;  // kova basina ortalama en cok 2 kayit

    Node**      buckets_ = nullptr;  // kova dizisi: her eleman bir zincirin basi
    std::size_t bucket_count_ = 0;   // her zaman 2'nin kuvveti (ya da 0)
    std::size_t count_ = 0;          // saklanan farkli kelime sayisi

    static char* text(Node* n) { return reinterpret_cast<char*>(n) + kHeader; }

    // FNV-1a: her harfi XOR'la, sonra buyuk bir asal sayiyla carp.
    // Sondaki karistirma, ust bitlerdeki bilgiyi alt bitlere tasir; cunku
    // kova numarasi icin sadece alt bitleri kullaniyoruz (hash & (kova-1)).
    static std::uint32_t hash_of(std::string_view w) {
        std::uint32_t h = 2166136261u;
        for (unsigned char c : w) {
            h ^= c;
            h *= 16777619u;
        }
        h ^= h >> 15;
        return h;
    }

    static bool same(const Node* n, std::uint32_t h, std::string_view w) {
        return n->hash == h && n->len == w.size() &&
               std::memcmp(reinterpret_cast<const char*>(n) + kHeader, w.data(), w.size()) == 0;
    }

    // Kova dizisini yeni boyuta tasir.
    // Bellek hilesi: once butun kayitlari tek bir gecici zincire diz, ESKI diziyi
    // serbest birak, SONRA yeni diziyi ayir. Boylece eski + yeni dizi ayni anda
    // bellekte durmaz ve tepe bellek (peak) dusuk kalir.
    void rehash(std::size_t new_count) {
        Node* all = nullptr;
        for (std::size_t i = 0; i < bucket_count_; ++i) {
            Node* n = buckets_[i];
            while (n) {
                Node* next = n->next;
                n->next = all;
                all = n;
                n = next;
            }
        }
        memory::deallocate(buckets_);
        buckets_ = static_cast<Node**>(memory::allocate(new_count * sizeof(Node*)));
        for (std::size_t i = 0; i < new_count; ++i) buckets_[i] = nullptr;
        bucket_count_ = new_count;
        while (all) {
            Node* next = all->next;
            std::size_t b = all->hash & (bucket_count_ - 1);
            all->next = buckets_[b];
            buckets_[b] = all;
            all = next;
        }
    }

public:
    ChainedHashTable() = default;
    ChainedHashTable(const ChainedHashTable&) = delete;
    ChainedHashTable& operator=(const ChainedHashTable&) = delete;
    ~ChainedHashTable() { clear(); }

    void insert_or_assign(std::string_view w, ListId list) {
        const std::uint32_t h = hash_of(w);
        if (bucket_count_ != 0) {
            for (Node* n = buckets_[h & (bucket_count_ - 1)]; n; n = n->next) {
                if (same(n, h, w)) {  // kelime zaten var: sadece numarayi degistir
                    n->list = list;
                    return;
                }
            }
        }
        // Yeni kelime: gerekirse once tabloyu buyut
        if (bucket_count_ == 0) rehash(kInitialBuckets);
        else if (count_ + 1 > bucket_count_ * kMaxLoad) rehash(bucket_count_ * 2);

        Node* n = static_cast<Node*>(memory::allocate(kHeader + w.size()));
        n->hash = h;
        n->list = list;
        n->len = static_cast<std::uint32_t>(w.size());
        std::memcpy(text(n), w.data(), w.size());  // SAHIPLI KOPYA: harfleri kendimize aliyoruz
        Node*& head = buckets_[h & (bucket_count_ - 1)];
        n->next = head;  // zincirin basina ekle: O(1)
        head = n;
        ++count_;
    }

    bool lookup(std::string_view w, ListId& out) const {
        if (bucket_count_ == 0) return false;
        const std::uint32_t h = hash_of(w);
        for (const Node* n = buckets_[h & (bucket_count_ - 1)]; n; n = n->next) {
            if (same(n, h, w)) {
                out = n->list;
                return true;
            }
        }
        return false;  // bulunamadi: out'a DOKUNMUYORUZ
    }

    bool remove(std::string_view w) {
        if (bucket_count_ == 0) return false;
        const std::uint32_t h = hash_of(w);
        // "link": silinecek dugumu gosteren isaretcinin adresi. Boylece
        // "bastaki dugum mu, ortadaki mi" diye ayri durum yazmaya gerek kalmaz.
        Node** link = &buckets_[h & (bucket_count_ - 1)];
        while (*link) {
            Node* n = *link;
            if (same(n, h, w)) {
                *link = n->next;  // dugumu zincirden cikar
                memory::deallocate(n);
                --count_;
                return true;
            }
            link = &n->next;
        }
        return false;
    }

    std::size_t size() const { return count_; }

    void clear() {
        for (std::size_t i = 0; i < bucket_count_; ++i) {
            Node* n = buckets_[i];
            while (n) {
                Node* next = n->next;  // once sonrakini al, sonra serbest birak
                memory::deallocate(n);
                n = next;
            }
        }
        memory::deallocate(buckets_);
        buckets_ = nullptr;  // cift serbest birakmayi onler; clear() iki kez cagrilabilir
        bucket_count_ = 0;
        count_ = 0;
    }
};

// Adaptor: uygulamanin bekledigi Index arayuzunu yapimizin islemlerine baglar.
class ChainIndex final : public Index {
    ChainedHashTable table_;

public:
    void put(std::string_view w, ListId l) override { table_.insert_or_assign(w, l); }
    bool find(std::string_view w, ListId& r) override { return table_.lookup(w, r); }
    bool erase(std::string_view w) override { return table_.remove(w); }
    std::size_t size() const override { return table_.size(); }
    void clear() override { table_.clear(); }
};

}  // namespace findfast

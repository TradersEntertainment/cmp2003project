// lab.cpp -- Aday yapilari dogrulayan ve yaristiran deney programi.
// Derleme : g++ -std=c++17 -O2 -Wall -Wextra -pedantic lab.cpp -o lab
// Calisma : ./lab            (once dogruluk testleri, sonra 200/1500/5000 yarisi)
//
// Bu dosya TESLIM EDILMEYECEK; burada std::map / std::vector kullanmak serbest,
// cunku onlar sadece "dogru cevap" referansi ve test verisi icin.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "avl.hpp"
#include "hash_chain.hpp"
#include "hash_open.hpp"

using namespace findfast;

static int g_fail = 0;
#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("    FAIL satir %d: %s\n", __LINE__, #cond);        \
            ++g_fail;                                                       \
        }                                                                   \
    } while (0)

static std::string random_word(std::mt19937& rng, int min_len = 3, int max_len = 12) {
    int len = min_len + static_cast<int>(rng() % (max_len - min_len + 1));
    std::string s(len, 'a');
    for (char& c : s) c = "abcdefghijklmnopqrstuvwxyz0123456789"[rng() % 36];
    return s;
}

template <class T>
static void correctness(const char* name) {
    int before = g_fail;
    std::size_t base = memory::g_live;
    {
        T ix;
        ListId r = 99;
        // T01, T05: bos nesne
        CHECK(ix.size() == 0);
        CHECK(!ix.find("missing", r) && r == 99);
        CHECK(!ix.erase("missing"));
        // T02-T04: ekleme ve guncelleme
        ix.put("network", 17);
        ix.put("network", 25);
        CHECK(ix.size() == 1 && ix.find("network", r) && r == 25);
        // T08: cagiranin metni yok olsa da kayit yasamali
        {
            std::string tmp = "temporary";
            ix.put(tmp, 5);
            tmp.assign("XXXXXXXXX");
        }
        CHECK(ix.find("temporary", r) && r == 5);
        // T18: uc degerler
        ix.put("zero", 0);
        ix.put("max", 4294967295u);
        CHECK(ix.find("zero", r) && r == 0);
        CHECK(ix.find("max", r) && r == 4294967295u);
        // T16: ortak onekler, bos kelime, 4096 karakterlik anahtarlar (sadece son harf farkli)
        std::string a(4096, 'q'), b = a;
        b.back() = 'r';
        ix.put(a, 1);
        ix.put(b, 2);
        ix.put("", 3);
        ix.put("net", 4);
        ix.put("networks", 6);
        CHECK(ix.find(a, r) && r == 1);
        CHECK(ix.find(b, r) && r == 2);
        CHECK(ix.find("", r) && r == 3);
        CHECK(ix.find("net", r) && r == 4);
        CHECK(ix.find("network", r) && r == 25);
        CHECK(ix.erase(a) && !ix.erase(a) && ix.find(b, r) && r == 2);
        // T12-T14: clear, tekrar clear, sonra yeniden kullanim
        ix.clear();
        ix.clear();
        CHECK(ix.size() == 0 && memory::g_live == base);
        CHECK(!ix.find("network", r));
        ix.put("again", 1);
        CHECK(ix.size() == 1);

        // T17: rastgele karisik islemler, std::map ile karsilastir
        for (unsigned seed : {1u, 42u, 2026u}) {
            ix.clear();
            std::mt19937 rng(seed);
            std::map<std::string, ListId> ref;
            std::vector<std::string> pool;
            for (int i = 0; i < 3000; ++i) pool.push_back(random_word(rng, 1, 6));
            for (int op = 0; op < 60000; ++op) {
                const std::string& w = pool[rng() % pool.size()];
                switch (rng() % 4) {
                    case 0:
                    case 1: {
                        ListId v = rng();
                        ix.put(w, v);
                        ref[w] = v;
                        break;
                    }
                    case 2: {
                        ListId got = 7;
                        auto it = ref.find(w);
                        bool ok = ix.find(w, got);
                        CHECK(ok == (it != ref.end()));
                        CHECK(got == (ok ? it->second : 7u));
                        break;
                    }
                    default:
                        CHECK(ix.erase(w) == (ref.erase(w) == 1));
                }
                CHECK(ix.size() == ref.size());
            }
            // T11: hepsini sil, sonra yeniden ekle
            for (auto& kv : ref) CHECK(ix.erase(kv.first));
            CHECK(ix.size() == 0);
            ix.put("fresh", 9);
            CHECK(ix.find("fresh", r) && r == 9);
        }
    }  // T15: yikici clear() cagrilmadan calisir
    CHECK(memory::g_live == base);
    std::printf("  %-12s %s\n", name, g_fail == before ? "DOGRU" : "HATALI");
}

struct Result {
    double build, lookup, update;
    std::size_t peak;
};

// Foydeki yaris tarifini taklit eder: n ilk anahtar, 2n sorgu, n guncelleme.
// 7 kez kosulur, ortanca (median) alinir.
template <class T>
static Result race(std::size_t n) {
    using clock = std::chrono::steady_clock;
    std::mt19937 rng(12345);
    std::vector<std::string> words, extra, queries;
    for (std::size_t i = 0; i < n; ++i) words.push_back(random_word(rng));
    for (std::size_t i = 0; i < n; ++i) extra.push_back(random_word(rng));
    for (std::size_t i = 0; i < 2 * n; ++i)  // yarisi var olan, yarisi olmayan kelime
        queries.push_back(i % 2 ? words[rng() % n] : random_word(rng));

    const int reps = 200;  // tek kosu cok kisa; olcum gurultusunu azaltmak icin tekrar
    std::vector<double> tb, tl, tu;
    std::size_t peak = 0;
    for (int run = 0; run < 7; ++run) {
        double b = 0, l = 0, u = 0;
        for (int rep = 0; rep < reps; ++rep) {
            memory::reset_peak();
            std::size_t base = memory::g_live;
            T ix;
            auto t0 = clock::now();
            for (std::size_t i = 0; i < n; ++i) ix.put(words[i], static_cast<ListId>(i));
            auto t1 = clock::now();
            ListId r = 0, sink = 0;
            for (auto& q : queries) sink += ix.find(q, r) ? r : 1;
            auto t2 = clock::now();
            for (std::size_t i = 0; i < n; ++i) {
                switch (i % 3) {
                    case 0: ix.put(words[i], static_cast<ListId>(i + 1)); break;  // guncelle
                    case 1: ix.erase(words[i]); break;                             // sil
                    default: ix.put(extra[i], static_cast<ListId>(i));             // yeni ekle
                }
            }
            auto t3 = clock::now();
            if (sink == 0) std::puts("?");
            b += std::chrono::duration<double, std::micro>(t1 - t0).count();
            l += std::chrono::duration<double, std::micro>(t2 - t1).count();
            u += std::chrono::duration<double, std::micro>(t3 - t2).count();
            peak = memory::g_peak - base;
        }
        tb.push_back(b / reps);
        tl.push_back(l / reps);
        tu.push_back(u / reps);
    }
    auto med = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    return {med(tb), med(tl), med(tu), peak};
}

int main() {
    std::puts("== Dogruluk ==");
    correctness<ChainIndex>("zincirli");
    correctness<OpenIndex>("acik-adres");
    correctness<AvlIndex>("avl");
    if (g_fail) return 1;

    std::puts("\n== Yaris (mikrosaniye, ortanca; bellek = tepe bayt) ==");
    for (std::size_t n : {200u, 1500u, 5000u}) {
        Result r[3] = {race<ChainIndex>(n), race<OpenIndex>(n), race<AvlIndex>(n)};
        const char* names[3] = {"zincirli", "acik-adres", "avl"};
        double bb = 1e18, bl = 1e18, bu = 1e18, bm = 1e18;
        for (auto& x : r) {
            bb = std::min(bb, x.build);
            bl = std::min(bl, x.lookup);
            bu = std::min(bu, x.update);
            bm = std::min(bm, double(x.peak));
        }
        std::printf("n = %zu\n", n);
        for (int i = 0; i < 3; ++i) {
            // Foydeki formul: T = 0.3 build + 0.4 lookup + 0.3 update, E = 10 * 2TM/(T+M)
            double T = 0.30 * bb / r[i].build + 0.40 * bl / r[i].lookup + 0.30 * bu / r[i].update;
            double M = bm / double(r[i].peak);
            std::printf("  %-11s build %8.1f  lookup %8.1f  update %8.1f  bellek %8zu   T %.2f  M %.2f  E %.2f\n",
                        names[i], r[i].build, r[i].lookup, r[i].update, r[i].peak, T, M, 10 * 2 * T * M / (T + M));
        }
    }
    return 0;
}

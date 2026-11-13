#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <cmath>
using namespace std;
static uint64_t load8(const char* p) { uint64_t v; char_traits<char>::copy(reinterpret_cast<char*>(&v), p, 8); return v; }
static uint64_t load4(const char* p) { uint32_t v; char_traits<char>::copy(reinterpret_cast<char*>(&v), p, 4); return v; }
template<bool LIGHT> static uint64_t hash_of(string_view w) {
    const char* p = w.data(); size_t n = w.size(); uint64_t h = 0x9E3779B97F4A7C15ull ^ n; uint64_t tail;
    if (n >= 8) { while (n > 8) { h = (h ^ load8(p)) * 0xFF51AFD7ED558CCDull; h ^= h >> 32; p += 8; n -= 8; } tail = load8(p + n - 8); }
    else if (n >= 4) tail = load4(p) | (load4(p + n - 4) << 32);
    else { tail = 0; for (size_t i = 0; i < n; ++i) tail = (tail << 8) | (unsigned char)p[i]; }
    h = (h ^ tail) * 0xC4CEB9FE1A85EC53ull;
    if (!LIGHT) { h ^= h >> 29; h *= 0xFF51AFD7ED558CCDull; }
    h ^= h >> 32; return h;
}
template<bool LIGHT> void report(const char* name, vector<string>& keys) {
    for (unsigned bits : {7u, 10u, 12u, 16u}) {
        size_t nb = size_t(1) << bits; vector<unsigned> cnt(nb), fp(256);
        size_t n = keys.size(); if (n > nb * 2) n = nb * 2;   // load 2
        for (size_t i = 0; i < n; ++i) { uint64_t h = hash_of<LIGHT>(keys[i]); cnt[h >> (64 - bits)]++; fp[h & 255]++; }
        double ssq = 0; unsigned mx = 0; for (auto c : cnt) { ssq += double(c) * (c + 1) / 2; if (c > mx) mx = c; }
        double expected = double(n) / 2 + double(n) * n / (2.0 * nb);  // E[sum c(c+1)/2] for uniform
        double chi = 0, e = double(n) / 256; for (auto c : fp) chi += (c - e) * (c - e) / e;
        printf("  %-6s %-28s bits %2u n %6zu  probe-cost ratio %.3f  max chain %u  fp chi2/255 %.2f\n", LIGHT ? "light" : "full", name, bits, n, ssq / expected, mx, chi / 255);
    }
}
int main() {
    vector<pair<const char*, vector<string>>> fam(6);
    fam[0].first = "term<i>"; fam[1].first = "sharedprefixarchive%06x"; fam[2].first = "random<i>"; fam[3].first = "a..z^3 short"; fam[4].first = "4096q + i"; fam[5].first = "i decimal";
    char buf[64];
    for (int i = 0; i < 140000; ++i) {
        fam[0].second.push_back("term" + to_string(i));
        snprintf(buf, sizeof buf, "sharedprefixarchive%06x", i); fam[1].second.push_back(buf);
        fam[2].second.push_back("random" + to_string(i * 7919 % 140000));
        fam[5].second.push_back(to_string(i));
        if (i < 2000) fam[4].second.push_back(string(4096, 'q') + to_string(i));
    }
    for (char a = 'a'; a <= 'z'; ++a) for (char b = 'a'; b <= 'z'; ++b) for (char c = 'a'; c <= 'z'; ++c) fam[3].second.push_back(string{a, b, c});
    for (auto& f : fam) { report<true>(f.first, f.second); report<false>(f.first, f.second); }
}

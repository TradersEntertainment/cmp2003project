// mock.hpp -- Gercek sablon (findfast.cpp) gelene kadar kullandigimiz TAKLIT ortam.
// Foydeki sozlesmeyi (Index arayuzu + takipli bellek) elimizden geldigince taklit eder.
// DIKKAT: Gercek sablondaki isimler/imzalar farkli cikabilir; o zaman sadece
// aday yapilardaki memory:: cagrilarini uyarlamamiz yeterli olacak.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace findfast {

using ListId = std::uint32_t;

// ---- Takipli bellek: her ayirmanin boyutunu basina yazar, canli/tepe bayt sayar ----
namespace memory {
inline std::size_t g_live = 0, g_peak = 0, g_blocks = 0;

inline void* allocate(std::size_t bytes) {
    // 16 baytlik gizli baslik: blok boyutunu saklar (hizalama bozulmasin diye 16)
    auto* raw = static_cast<std::size_t*>(std::malloc(bytes + 16));
    raw[0] = bytes;
    g_live += bytes;
    ++g_blocks;
    if (g_live > g_peak) g_peak = g_live;
    return reinterpret_cast<char*>(raw) + 16;
}
inline void deallocate(void* p) {
    if (!p) return;
    auto* raw = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= raw[0];
    --g_blocks;
    std::free(raw);
}
inline void reset_peak() { g_peak = g_live; }
}  // namespace memory

class Index {
public:
    virtual void put(std::string_view word, ListId list) = 0;
    virtual bool find(std::string_view word, ListId& result) = 0;
    virtual bool erase(std::string_view word) = 0;
    virtual std::size_t size() const = 0;
    virtual void clear() = 0;
    virtual ~Index() = default;
};

}  // namespace findfast

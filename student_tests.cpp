// student_tests.cpp -- our own tests for StudentIndex (submitted separately from findfast.cpp).
//
// Build and run (findfast.cpp must be in the same folder):
//   g++ -std=c++17 -O2 -Wall -Wextra -pedantic student_tests.cpp -o student_tests
//   ./student_tests
// Optional memory-error check (Linux/macOS):
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined student_tests.cpp -o student_tests_asan
//   ./student_tests_asan
//
// The template's own main() is renamed so that this file can provide its own.
// std::map / std::string / std::vector are used ONLY here, as the trusted reference
// and for test data; the index itself never uses them.
#define main findfast_template_main
#include "findfast.cpp"
#undef main

namespace {
using namespace findfast;

int g_failed = 0;
int g_passed = 0;

// Runs one named test, reports PASS/FAIL, and checks that tracked memory
// returned to the level it had before the test (catches leaks and double frees).
template <class F>
void run(const char* name, F body) {
    const auto before = memory::live;
    string failure;
    try {
        body();
    } catch (const exception& e) {
        failure = e.what();
    }
    if (memory::live != before) failure += (failure.empty() ? "" : " | ") + string("tracked memory not released");
    if (failure.empty()) {
        ++g_passed;
        cout << "PASS " << name << '\n';
    } else {
        ++g_failed;
        cout << "FAIL " << name << ": " << failure << '\n';
    }
}

void expect(bool ok, const string& what) {
    if (!ok) throw runtime_error(what);
}

void expect_hit(Index& ix, string_view w, ListId want) {
    ListId got = ~want;
    expect(ix.find(w, got), "expected a hit for a word of length " + to_string(w.size()));
    expect(got == want, "wrong ListId for a word of length " + to_string(w.size()));
}

void expect_miss(Index& ix, string_view w) {
    for (ListId sentinel : {ListId{0}, ListId{12345}, numeric_limits<ListId>::max()}) {
        ListId got = sentinel;
        expect(!ix.find(w, got), "unexpected hit for a word of length " + to_string(w.size()));
        expect(got == sentinel, "a miss changed the output argument");
    }
}

// Compares the index with a std::map: size and every stored pair.
void expect_same(Index& ix, const map<string, ListId>& ref) {
    expect(ix.size() == ref.size(), "size() differs from the reference");
    for (const auto& kv : ref) expect_hit(ix, kv.first, kv.second);
}

// 1. The empty word is a valid key.
void empty_word() {
    auto ix = make_index("student");
    expect_miss(*ix, "");
    expect(!ix->erase(""), "erase of an absent empty word returned true");
    ix->put("", 7);
    expect_hit(*ix, "", 7);
    expect(ix->size() == 1, "size after putting the empty word");
    ix->put("a", 8);
    ix->put("", 9);
    expect_hit(*ix, "", 9);
    expect_hit(*ix, "a", 8);
    expect(ix->size() == 2, "update of the empty word added a record");
    expect(ix->erase("") && !ix->erase(""), "erase of the empty word");
    expect_hit(*ix, "a", 8);
}

// 2. Prefix chains: every word is a prefix of the next one. Lengths cross
//    127/128 and 16383/16384, where length encodings often change size.
void prefix_chain() {
    auto ix = make_index("student");
    const size_t lengths[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 63, 64, 126, 127, 128, 129, 255, 256, 257,
                              4095, 4096, 4097, 16383, 16384, 16385, 70000};
    string base(70000, 'p');
    ListId id = 1;
    for (size_t len : lengths) ix->put(string_view(base).substr(0, len), id++);
    id = 1;
    for (size_t len : lengths) expect_hit(*ix, string_view(base).substr(0, len), id++);
    expect_miss(*ix, string_view(base).substr(0, 10));
    expect_miss(*ix, string_view(base).substr(0, 69999));
    expect(ix->size() == sizeof(lengths) / sizeof(lengths[0]), "prefix chain size");
}

// 3. Very long words that differ only in the first, middle or last character.
void long_similar_words() {
    auto ix = make_index("student");
    string a(200000, 'k');
    string first = a, middle = a, last = a;
    first.front() = 'x';
    middle[100000] = 'x';
    last.back() = 'x';
    ix->put(a, 1);
    ix->put(first, 2);
    ix->put(middle, 3);
    ix->put(last, 4);
    expect_hit(*ix, a, 1);
    expect_hit(*ix, first, 2);
    expect_hit(*ix, middle, 3);
    expect_hit(*ix, last, 4);
    expect(ix->erase(middle), "erase long middle variant");
    expect_miss(*ix, middle);
    expect_hit(*ix, last, 4);
    expect(ix->size() == 3, "size with long words");
}

// 4. Any byte value may appear in a word, including '\0' and 0xFF.
void binary_bytes() {
    auto ix = make_index("student");
    const string w1("ab\0cd", 5), w2("ab\0ce", 5), w3("ab", 2), w4("\xff\xfe", 2), w5(1, '\0'), w6(2, '\0');
    ix->put(w1, 1);
    ix->put(w2, 2);
    ix->put(w3, 3);
    ix->put(w4, 4);
    ix->put(w5, 5);
    ix->put(w6, 6);
    expect_hit(*ix, w1, 1);
    expect_hit(*ix, w2, 2);
    expect_hit(*ix, w3, 3);
    expect_hit(*ix, w4, 4);
    expect_hit(*ix, w5, 5);
    expect_hit(*ix, w6, 6);
    expect_miss(*ix, string("ab\0", 3));
    expect_miss(*ix, "");
    expect(ix->size() == 6, "binary words size");
}

// 5. ListId uses all 32 bits; 0 and the maximum are ordinary values.
void list_id_extremes() {
    auto ix = make_index("student");
    constexpr ListId top = numeric_limits<ListId>::max();
    for (int round = 0; round < 3; ++round) {
        ix->put("lo", 0);
        ix->put("hi", top);
        expect_hit(*ix, "lo", 0);
        expect_hit(*ix, "hi", top);
        ix->put("lo", top);
        ix->put("hi", 0);
        expect_hit(*ix, "lo", top);
        expect_hit(*ix, "hi", 0);
        expect(ix->size() == 2, "updates changed the size");
    }
}

// 6. The index copies words: the caller may change or destroy its text after put.
void ownership() {
    auto ix = make_index("student");
    {
        string buffer(300, 'b');
        for (int i = 0; i < 50; ++i) {
            buffer[i] = static_cast<char>('A' + i % 26);
            ix->put(buffer, static_cast<ListId>(i));
        }
        buffer.assign(300, '#');
    }
    string check(300, 'b');
    for (int i = 0; i < 50; ++i) {
        check[i] = static_cast<char>('A' + i % 26);
        expect_hit(*ix, check, static_cast<ListId>(i));
    }
}

// 7. Lifecycle: clear, clear twice, reuse, erase everything, destructor without clear.
void lifecycle() {
    const auto before = memory::live;
    {
        auto ix = make_index("student");
        const auto empty = memory::live;
        ix->clear();
        ix->clear();
        expect(memory::live == empty, "clear on a new object changed memory");
        for (int round = 0; round < 4; ++round) {
            for (int i = 0; i < 1000; ++i) ix->put("w" + to_string(i), static_cast<ListId>(i));
            if (round % 2 == 0) {
                ix->clear();
            } else {
                for (int i = 999; i >= 0; --i) expect(ix->erase("w" + to_string(i)), "erase all");
            }
            expect(ix->size() == 0, "size after emptying");
            // clear() must give back everything; erase may keep the (empty) table for reuse.
            if (round % 2 == 0) expect(memory::live == empty, "clear did not return memory to the empty level");
            expect_miss(*ix, "w0");
            ix->put("again", 3);
            expect_hit(*ix, "again", 3);
            ix->clear();
        }
        for (int i = 0; i < 5000; ++i) ix->put("left" + to_string(i), 1);
    }  // destroyed while full
    expect(memory::live == before, "destructor did not release everything");
}

// 8. Lookups never change the structure: thousands of hits and misses, then a full comparison.
void lookups_are_read_only() {
    auto ix = make_index("student");
    map<string, ListId> ref;
    for (int i = 0; i < 3000; ++i) {
        ref["k" + to_string(i * 7)] = static_cast<ListId>(i);
        ix->put("k" + to_string(i * 7), static_cast<ListId>(i));
    }
    const auto live = memory::live;
    for (int r = 0; r < 5; ++r)
        for (int i = 0; i < 21000; ++i) {
            ListId got = 99;
            const string w = "k" + to_string(i);
            const bool hit = ix->find(w, got);
            expect(hit == (i % 7 == 0), "hit/miss mismatch");
            if (!hit) expect(got == 99, "miss changed result");
        }
    expect(memory::live == live, "find changed tracked memory");
    expect_same(*ix, ref);
}

// 9. Randomised differential test against std::map with several key shapes and seeds.
void random_differential() {
    for (uint32_t seed : {1u, 2u, 42u, 2026u, 77777u, 4294967295u}) {
        for (int shape = 0; shape < 4; ++shape) {
            mt19937 rng(seed);
            auto ix = make_index("student");
            map<string, ListId> ref;
            const int universe = shape == 0 ? 50 : shape == 1 ? 2000 : 20000;
            for (int step = 0; step < 60000; ++step) {
                const unsigned r = rng() % universe;
                string w;
                if (shape == 3) w = string(1 + r % 300, static_cast<char>('a' + r % 26)) + to_string(r);  // long, similar
                else w = (r % 3 ? "sharedprefixarchive" : "w") + to_string(r);
                switch (rng() % 10) {
                case 0: case 1: case 2: case 3: {
                    const ListId v = static_cast<ListId>(rng());
                    ix->put(w, v);
                    ref[w] = v;
                    break;
                }
                case 4: case 5:
                    expect(ix->erase(w) == (ref.erase(w) == 1), "random erase result");
                    break;
                case 6: case 7: case 8: {
                    const auto it = ref.find(w);
                    if (it == ref.end()) expect_miss(*ix, w);
                    else expect_hit(*ix, w, it->second);
                    break;
                }
                default:
                    if (rng() % 4000 == 0) {
                        ix->clear();
                        ref.clear();
                    }
                }
                expect(ix->size() == ref.size(), "random size");
            }
            expect_same(*ix, ref);
        }
    }
}

// 10. Large scale: grow far beyond the public tests, shrink, regrow.
void large_scale() {
    auto ix = make_index("student");
    const int n = 200000;
    for (int i = 0; i < n; ++i) ix->put("term" + to_string(i), static_cast<ListId>(i * 2654435761u));
    expect(ix->size() == static_cast<size_t>(n), "large size");
    for (int i = 0; i < n; i += 2) expect(ix->erase("term" + to_string(i)), "large erase");
    for (int i = 0; i < n; ++i) {
        const string w = "term" + to_string(i);
        if (i % 2) expect_hit(*ix, w, static_cast<ListId>(i * 2654435761u));
        else expect_miss(*ix, w);
    }
    for (int i = 0; i < n; i += 2) ix->put("term" + to_string(i), 5);
    expect(ix->size() == static_cast<size_t>(n), "large regrow size");
    expect_hit(*ix, "term0", 5);
}

// 11. The application on top of the index (search, replace, remove, clear).
void application() {
    Engine e;
    const auto a = e.add("a.txt", "alpha beta gamma");
    const auto b = e.add("b.txt", "beta delta");
    const auto c = e.add("c.txt", "Gamma, GAMMA; epsilon!");
    expect(e.search("beta") == vector<DocId>{a, b}, "beta");
    expect(e.search("gamma", "AND", "epsilon") == vector<DocId>{c}, "gamma AND epsilon");
    expect(e.search("alpha", "OR", "delta") == vector<DocId>{a, b}, "alpha OR delta");
    expect(e.replace(a, "a.txt", "omega"), "replace");
    expect(e.search("alpha").empty() && e.search("omega") == vector<DocId>{a}, "after replace");
    expect(e.search("gamma") == vector<DocId>{c}, "gamma after replace");
    expect(e.remove(c) && !e.remove(c), "remove twice");
    expect(e.search("gamma").empty() && e.search("epsilon").empty(), "words of a removed document");
    expect(e.word_count() == 3, "word count (beta, delta, omega)");
    e.clear();
    expect(e.word_count() == 0, "clear");
    expect(e.add("z.txt", "zeta") == 1 && e.search("zeta") == vector<DocId>{1}, "reuse after clear");
}
}  // namespace

int main() {
    run("U01 empty word", empty_word);
    run("U02 prefix chain and length boundaries", prefix_chain);
    run("U03 long words differing in one character", long_similar_words);
    run("U04 binary bytes in words", binary_bytes);
    run("U05 ListId 0 and maximum", list_id_extremes);
    run("U06 caller text changes after put", ownership);
    run("U07 clear, reuse, erase all, destructor", lifecycle);
    run("U08 find never changes the structure", lookups_are_read_only);
    run("U09 random differential vs std::map", random_differential);
    run("U10 200,000 keys", large_scale);
    run("U11 search engine on top of the index", application);
    cout << "SUMMARY " << g_passed << '/' << (g_passed + g_failed) << '\n';
    return g_failed ? 1 : 0;
}

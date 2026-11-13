# FindFast: How Our Index Works

A complete walkthrough of `findfast.cpp` for every team member. After reading it you should be able to explain any line of our code in the final question-and-answer session.

> Türkçe sürüm: [ANLATIM_TR.md](ANLATIM_TR.md)

**Contents**

1. [The task in one paragraph](#1-the-task-in-one-paragraph)
2. [Why a chained hash table](#2-why-a-chained-hash-table)
3. [The picture to keep in mind](#3-the-picture-to-keep-in-mind)
4. [The code, part by part](#4-the-code-part-by-part)
5. [Complexity](#5-complexity)
6. [Memory, byte by byte](#6-memory-byte-by-byte)
7. [Measurements](#7-measurements)
8. [How each rule is satisfied](#8-how-each-rule-is-satisfied)
9. [Build, test, benchmark](#9-build-test-benchmark)
10. [Questions the examiner may ask](#10-questions-the-examiner-may-ask)

---

## 1. The task in one paragraph

The search engine in the template maps every word to a *list ID* (a `uint32_t`). We had to write only that map, the class `StudentIndex`, with five operations: `put` (insert or replace), `find`, `erase`, `size` and `clear`, plus the constructor and the destructor. We may not use standard containers, library search or sort algorithms, untracked memory, caches, I/O or threads. Every word we keep must be our own copy, and every byte we allocate must go through the supplied `memory::allocate`. All 20 hidden tests must pass to qualify. After that, the rank comes from a score that combines **time** (build 30%, lookup 40%, update 30%) and **peak memory** with a harmonic mean, so a weakness in either one pulls the whole score down.

## 2. Why a chained hash table

A hash table answers `find` in time that does not grow with the number of stored words: we compute one number from the word and jump straight to the right place. A balanced tree would need about log₂ n string comparisons per operation (about 12 at n = 5,000), and a sorted array would need O(n) shifting on every insert.

We did not choose on intuition alone. We wrote **seven different hash-table designs**, made each one pass every test, and raced them on the same machine. We also wrote two "typical" implementations of the kind most teams are likely to submit, to see where we stand. Each workload ran 31 times and we took the median, as the official grading does. The scores below use the official formula, with each cost compared against the best of the designs we tested:

| Design | Time ratio T | Memory ratio M | Score E (out of 10) |
|---|---|---|---|
| **Chained, compact node + 1-byte fingerprint (our choice)** | **0.93** | **0.94** | **9.3** |
| Chained, compact node, no fingerprint | 0.84 | 0.97 | 9.0 |
| Open addressing with a 1-byte control array | 0.87 | 0.88 | 8.7 |
| Chained, non-power-of-two growth | 0.77 | 0.99 | 8.6 |
| Chained, 20-byte node header (our first prototype) | 0.77 | 0.76 | 7.7 |
| Open addressing with plain pointers | 0.74 | 0.79 | 7.4 |
| *Typical: chained nodes holding a `Record`, `std::hash`* | 0.70 | 0.54 | 6.1 |
| *Typical: open addressing over a `Record` array* | 0.82 | 0.25 | 3.8 |

Three ideas make our design win:

1. **One allocation per word, with the characters inside it.** The supplied `Record` would cost 24 bytes plus a second allocation for the characters. We store the link, the list ID, a fingerprint, the length and the characters in a single block of `14 + L` bytes.
2. **A one-byte fingerprint** of the hash in every node. While walking a chain, a node holding a different word is rejected after reading a single byte, without comparing characters.
3. **Growing without a memory spike.** When the table grows, we release the old bucket array *before* asking for the new one (section 4.8). The score uses the *peak* memory, so this matters.

We also swept the maximum load factor (words per bucket) from 1 to 4. The best balance between speed and memory was at **2**, which is the value we use.

## 3. The picture to keep in mind

```
 buckets_ (array of 2^k pointers)
 ┌─────┐
 │  0  │──► [node "data3c4"] ──► [node "catalog88"] ──► nullptr
 ├─────┤
 │  1  │──► nullptr
 ├─────┤
 │  2  │──► [node "sharedprefixarchive0001c5"] ──► nullptr
 ├─────┤
 │ ... │
 └─────┘

 One node = one memory::allocate block (14 + L bytes for words under 128 characters)
 ┌──────────────┬──────────────┬─────────────┬────────────┬──────────────────┐
 │ next (8 B)   │ list (4 B)   │ finger. (1) │ length (1) │ characters (L B) │
 └──────────────┴──────────────┴─────────────┴────────────┴──────────────────┘
   offset 0       offset 8       offset 12     offset 13    offset 14
```

To look up a word:

1. Compute its 64-bit hash `h`.
2. The **top** bits of `h` choose the bucket: `h >> shift_`.
3. The **low** 8 bits of `h` are the fingerprint.
4. Walk the chain. Skip any node whose fingerprint differs. When a fingerprint matches, compare the length and then the characters.

## 4. The code, part by part

Everything below sits between `// BEGIN STUDENT IMPLEMENTATION` and `// END STUDENT IMPLEMENTATION`. Nothing outside the markers was changed, and `make_index` is untouched.

### 4.1 The `Node` struct and the offsets

```cpp
struct Node {
    Node* next;
};
static constexpr size_t kListOffset = sizeof(Node);                    // 8
static constexpr size_t kPrintOffset = kListOffset + sizeof(ListId);   // 12
static constexpr size_t kLengthOffset = kPrintOffset + 1;              // 13
```

A node has a variable size because words have different lengths, and a C++ struct cannot have a variable size. So `Node` declares only the link, and the other fields are placed at fixed byte offsets inside the same allocation.

Why not a struct with all the fields? `{Node* next; ListId list; unsigned char print;}` would be padded to 16 bytes, which wastes 2–3 bytes on every word. Using offsets, the characters start right after the length byte.

### 4.2 The fields of `StudentIndex`

```cpp
Node** buckets_ = nullptr;   // array of chain heads
size_t bucket_count_ = 0;    // 0 or a power of two
size_t count_ = 0;           // number of distinct words
unsigned shift_ = 64;        // bucket index = hash >> shift_
```

- `buckets_ == nullptr` means "nothing is stored". The constructor allocates nothing, so a new object holds no dynamic memory (T01, T13).
- `count_` makes `size()` O(1).
- `shift_` equals `64 - log2(bucket_count_)`. With 8 buckets, `shift_ = 61`, so `h >> 61` is a number from 0 to 7. The initial value 64 is never used in a shift: every operation checks `buckets_` first.

Constants:

- `kInitialBits = 3` gives 8 buckets for the first table.
- `kMaxLoad = 2` means the table grows when the number of words would exceed 2 × the number of buckets.

### 4.3 `load8` / `load4`: reading 8 characters at once

```cpp
static uint64_t load8(const char* p) {
    uint64_t value;
    char_traits<char>::copy(reinterpret_cast<char*>(&value), p, 8);
    return value;
}
```

This copies 8 bytes of the word into a 64-bit integer. The compiler turns it into **one** load instruction. We do not use `*(uint64_t*)p` because the word may start at any address and that cast is undefined behaviour in C++. `char_traits<char>::copy` is the same function the supplied `Key` uses to copy characters; it is a copy, not a search or sort algorithm.

### 4.4 `hash_of`: turning a word into a 64-bit number

```cpp
uint64_t h = 0x9E3779B97F4A7C15ull ^ n;          // start: a constant mixed with the length
while (n > 8) {                                  // 8 characters per step
    h = (h ^ load8(p)) * 0xFF51AFD7ED558CCDull;  // put the block in, multiply to spread it
    h ^= h >> 32;                                // fold the high half back into the low half
    p += 8; n -= 8;
}
tail = ... the last 1–8 characters ...
h = (h ^ tail) * 0xC4CEB9FE1A85EC53ull;          // final mixing ("avalanche")
h ^= h >> 29;
h *= 0xFF51AFD7ED558CCDull;
h ^= h >> 32;
```

- **Why 8 characters per step?** Our first prototype used FNV-1a, one character per step. For a 14-character word that is 14 multiplications in a chain, and it made lookups about 30% slower.
- **Why multiply?** Multiplying by a large odd constant spreads each input bit into many higher bits. The shift-xor (`h ^= h >> 32`) brings the high bits back down. Together they make a small change in the word ("data332" → "data333") change about half of the hash bits.
- **The constants** are well-known 64-bit mixing constants: the golden-ratio constant 0x9E3779B97F4A7C15 and the two MurmurHash3 finaliser multipliers. Any large odd constant with irregular bits works. These are widely used and well tested.
- **Why mix in the length?** So that words that are prefixes of each other ("net", "network") start from different states.
- **The tail.**
  - At least 8 characters: we read the *last* 8 bytes, which may overlap the previous block. This is safe because overlapping only reads characters of the word.
  - 4 to 7 characters: two overlapping 4-byte reads.
  - Fewer than 4: a short loop over the bytes.
  - The empty word: `tail = 0` and nothing is read.
- **Quality check.** We measured the hash on several key families (`term<i>`, `sharedprefixarchive%06x`, decimal numbers, every 3-letter word, 4,096-character keys). Bucket occupancy matched a uniform random distribution, and the fingerprint bytes were evenly spread. We also tried a cheaper finaliser. It was not measurably faster and spread the fingerprint badly for `term<i>` keys, so we kept the full one.

### 4.5 `fingerprint`, `list_of`, `set_list`, `fingerprint_of`

```cpp
static unsigned char fingerprint(uint64_t h) { return static_cast<unsigned char>(h); }   // low 8 bits
```

The bucket comes from the **top** bits and the fingerprint from the **low** bits, so two words in the same bucket still have independent fingerprints. A wrong word passes the fingerprint test only 1 time in 256.

`list_of` and `set_list` read and write the 4-byte list ID at offset 8 with `char_traits<char>::copy`. This is the strictly correct way to access an integer stored inside a raw byte block, and it compiles to a single `mov`. `fingerprint_of` reads byte 12; reading any memory through `unsigned char` is always allowed.

### 4.6 The length as a varint: `varint_size`, `write_varint`, `word_of`

Words can have any length (the public tests use 4,096 characters; the hidden ones may use more). Storing the length as a `size_t` would cost 8 bytes per word, and almost all words are short. So we use a **varint**:

- Each byte stores 7 bits of the length.
- The top bit (value 128) means "another byte follows".

| Length | Bytes | Encoding |
|---|---|---|
| 0–127 | 1 | `[L]` |
| 128–16,383 | 2 | `[L & 127 | 128][L >> 7]` |
| 16,384–2,097,151 | 3 | … |

`word_of(node)` decodes the length. The fast path is a single byte below 128. It returns a `string_view` that points at the stored characters, and every comparison goes through it.

### 4.7 `equal_text` and `holds`: is this the word we want?

```cpp
static bool holds(const Node* node, unsigned char print, string_view word) {
    if (fingerprint_of(node) != print) return false;      // 255 of 256 wrong words stop here
    const string_view stored = word_of(node);
    return stored.size() == word.size() && equal_text(stored.data(), word.data(), word.size());
}
```

`equal_text` compares 8 characters per step with `load8`, using the same overlap trick for the last block. It only answers "equal or not", which is all we need, so it is faster than a general comparison that also has to find which string is smaller.

### 4.8 `make_node` and `free_chain`

```cpp
const size_t size = kLengthOffset + varint_size(word.size()) + word.size();   // 13 + 1 + L
char* raw = static_cast<char*>(memory::allocate(size));                        // tracked memory
Node* node = ::new (raw) Node{next};      // placement new: build the link object in that block
set_list(node, list);
raw[kPrintOffset] = static_cast<char>(print);
char* text = write_varint(raw + kLengthOffset, word.size());
char_traits<char>::copy(text, word.data(), word.size());                       // OUR OWN COPY
```

- **Placement new** (`::new (raw) Node{next}`) allocates nothing. It constructs a `Node` object in memory we already received from `memory::allocate`. The supplied `memory::make` uses the same technique.
- The characters are **copied**. The caller's `string_view` may point to a temporary string that disappears right after `put` returns (T08).
- `free_chain` reads `next` **before** releasing a node, because the node's memory must not be touched after `deallocate`.

### 4.9 `rehash`: growing without a memory spike

```cpp
Node* all = nullptr;                    // 1. thread EVERY node onto one temporary list
for each bucket: move its nodes to the front of `all`
memory::deallocate(buckets_);           // 2. release the old array FIRST
buckets_ = allocate(new size);          // 3. only then ask for the new array
for each node in `all`: push it to the front of buckets_[hash >> shift_]   // 4. redistribute
```

The usual way is: allocate the new array, move the nodes, free the old array. For a moment both arrays exist, and the grader measures the **peak**. Because our nodes carry their own `next` links, we can park them all on one temporary list, release the old array, and only then allocate the new one. **Our peak memory therefore equals the memory of the final table.** We checked this with a byte-exact model: at n = 5,000 the predicted peak (176,022 bytes) matches the measured value exactly.

We do not store the full hash in the node, so `rehash` recomputes it from the stored characters. We tested a version that stores 4 more bytes per word to skip this. It was not faster in practice, so the bytes are not worth it.

Growing doubles the table, so a word is moved O(1) times on average (1 + ½ + ¼ + … < 2). `put` is therefore amortised O(L).

### 4.10 `put`

```cpp
const uint64_t h = hash_of(word);
const unsigned char print = fingerprint(h);
if (buckets_) {
    for (Node* node = buckets_[h >> shift_]; node; node = node->next)
        if (holds(node, print, word)) { set_list(node, list); return; }   // existing word: replace the ID
}
if (!buckets_) rehash(kInitialBits);                                       // first word ever
else if (count_ >= kMaxLoad * bucket_count_) rehash(64 - shift_ + 1);     // too full: double
Node*& head = buckets_[h >> shift_];
head = make_node(word, list, print, head);                                // insert at the FRONT
++count_;
```

- An existing word only gets its new list ID: no second record, and `count_` does not change (T04, T12).
- The table grows **before** we compute the bucket, so the new node lands in the right bucket of the final table.
- Inserting at the front of the chain is O(1).

### 4.11 `find`

The same walk as `put`. On a hit it writes `result` and returns `true`. On a miss it returns `false` **without touching `result`** (T05). `find` never changes the structure: no reordering, no caching (T07, T19).

### 4.12 `erase` and the pointer to a pointer

```cpp
for (Node** link = &buckets_[h >> shift_]; *link; link = &(*link)->next) {
    Node* node = *link;
    if (holds(node, print, word)) {
        *link = node->next;          // the previous pointer now skips the node
        memory::deallocate(node);    // release its storage (word included)
        --count_;
        return true;
    }
}
return false;
```

`link` holds the **address of the pointer** that leads to the current node. At the first node that pointer is the bucket slot; later it is the previous node's `next` field. So `*link = node->next` removes the node wherever it is in the chain, with no special case for the head. A missing word returns `false` and leaves `count_` alone (T10).

### 4.13 `size`, `clear`, destructor

- `size()` returns `count_`. It is `const` and changes nothing.
- `clear()` frees every chain, frees the bucket array, and resets every field to the constructor's state. A second `clear()` is safe: `bucket_count_` is 0 so the loop does nothing, and `deallocate(nullptr)` does nothing. Tracked memory returns exactly to the empty-object level (T13, T14).
- The destructor calls `clear()`, so a populated index releases everything even if nobody called `clear()` (T15).

## 5. Complexity

n = number of stored words, L = length of the word in the operation, b = number of buckets (n/2 ≤ b ≤ n after the first growth).

| Operation | Average | Worst case | Why |
|---|---|---|---|
| `put` (new word) | O(L) amortised | O(n·L) | hash O(L) + chain of ≤ 2 nodes on average + O(L) copy; doubling costs O(n) but happens rarely |
| `put` (existing word) | O(L) | O(n·L) | hash + chain walk + compare |
| `find` | O(L) | O(n·L) | hash + chain walk; the fingerprint skips most compares |
| `erase` | O(L) | O(n·L) | same walk + O(1) unlink |
| `size` | O(1) | O(1) | stored counter |
| `clear` / destructor | O(n + b) | O(n + b) | each node and the array freed once |
| constructor | O(1) | O(1) | no allocation |
| **Memory** | Σ(14 + Lᵢ) + 8·b + 40 bytes | | one block per word + bucket array + the object |

The worst case only happens if almost every word lands in the same bucket, which a good 64-bit hash makes practically impossible for real words.

## 6. Memory, byte by byte

Exact model for the 5,000-key workload (average word length 14.6):

| Part | Bytes |
|---|---|
| 5,000 nodes × (14 + L) | 143,214 |
| bucket array: 4,096 × 8 | 32,768 |
| the `StudentIndex` object (vtable pointer + 4 fields) | 40 |
| **peak (model = measured)** | **176,022** |

That is about **35 bytes per word, of which 14.6 are the word itself**. For comparison, a node that holds a `Record` with `std::hash` measured 299,000 bytes, and a `Record` array with open addressing measured 674,000 bytes.

## 7. Measurements

Median of 61 runs with `./findfast --bench`, Intel Xeon 2.3 GHz, `g++ -std=c++17 -O2`:

| Workload | Build | Lookup | Update | Peak bytes |
|---|---|---|---|---|
| synthetic `--n 200` | 11.9 µs | 7.3 µs | 5.3 µs | 5,389 |
| synthetic `--n 1500` | 88.8 µs | 59.4 µs | 41.6 µs | 42,092 |
| synthetic `--n 5000 --queries 10000 --updates 5000` | 318.9 µs | 185.8 µs | 147.1 µs | 149,281 |
| CSV like `data/small.csv`, 5,000 keys | 378.5 µs | 229.3 µs | 171.3 µs | 176,022 |

The full tables and the scripts that produced them are in [`research/`](../research).

## 8. How each rule is satisfied

| Rule (brief / template) | How we comply |
|---|---|
| Only the marked region is edited; `make_index` unchanged | Checked automatically by `research/tools/check_rules.py`: everything outside the region is byte-identical to the template |
| No standard containers or adapters | Only raw arrays and our own nodes; no `vector`, `map`, `string`, … in the region |
| No library search or sort algorithms | None used. `char_traits<char>::copy` only copies bytes (the supplied `Key` uses it too) |
| No second lookup structure, no cache, no hard-coded answers | One hash table. `find` is read-only. Nothing depends on specific words or workload sizes |
| No I/O, no threads | None |
| All dynamic memory tracked | Every allocation is `memory::allocate`; every release is `memory::deallocate`. Placement new only constructs inside those blocks |
| Owned copies of words | Characters are copied into the node in `make_node` |
| `find` miss leaves `result` unchanged | `result` is written only on a hit |
| ListId 0 and 4,294,967,295 are normal values | Stored as a full `uint32_t`; no value is used as a marker |
| `clear` twice, reuse after `clear`, destructor without `clear` | Covered by T13–T15 and by our own tests U07 |

## 9. Build, test, benchmark

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic findfast.cpp -o findfast
./findfast --test all                        # SUMMARY 20/20
./findfast --test all --scale 5000 --seed 7  # larger hidden-like run
./findfast --demo
./findfast --bench --n 5000 --queries 10000 --updates 5000
./findfast --bench --workload data/small.csv

g++ -std=c++17 -O2 -Wall -Wextra -pedantic student_tests.cpp -o student_tests
./student_tests                              # our 11 extra tests: SUMMARY 11/11
```

We also ran every test under AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) and with clang++. Both are clean.

## 10. Questions the examiner may ask

**Why a hash table and not a tree?** A tree needs about log₂ n string comparisons per operation; a hash table needs one hash and on average fewer than two node visits. We measured an AVL tree in our prototype: 3–5 times slower at n = 5,000.

**What is a collision and how do you handle it?** Two different words in the same bucket. We keep them in a linked list (separate chaining) and compare until we find the right one.

**What is the load factor and why 2?** Words per bucket. A lower value means shorter chains but a bigger bucket array. We measured 1, 1.5, 2, 2.5, 3 and 4; 2 gave the best combined score.

**Why is the bucket count a power of two?** So the bucket is just the top bits of the hash (`h >> shift_`), with no slow division.

**Why the top bits and not `h % size`?** A shift is one instruction and the top bits of our hash are well mixed. It also leaves the low bits free for the fingerprint.

**What does the fingerprint buy you?** When a chain holds other words, we reject each of them after one byte instead of a length check and a character comparison. It made the whole engine about 10% faster for 1 byte per word.

**Two different words with the same hash?** Still correct: the fingerprint and the hash only *reject*. A word is accepted only after the length and every character match.

**Why do you copy the word?** `string_view` does not own its characters. The caller may destroy them after `put` returns (T08). We must keep our own copy.

**What is placement new?** `::new (address) T{...}` constructs an object at an address that is already allocated. It does not allocate. The supplied `memory::make` uses it too.

**Why `char_traits<char>::copy` instead of a pointer cast?** Reading a `uint64_t` through a `char*` that was cast to `uint64_t*` is undefined behaviour when the address is not aligned or the memory holds other types. Copying the bytes is always valid, and the compiler emits a single load.

**What is a varint and why use it?** A variable-length integer: 7 bits per byte, and the top bit says "more bytes follow". Normal words need 1 byte for their length instead of 8, and any length still works.

**Why does `rehash` free the old array before allocating the new one?** The score uses the peak memory. Normally both arrays exist for a moment. We first move every node onto one temporary list, so the old array is no longer needed. Our peak is the final table's size.

**What does `Node** link` do in `erase`?** It points at the pointer that leads to the current node: the bucket slot first, then a `next` field. Writing `*link = node->next` unlinks the node anywhere in the chain without a special case.

**What happens if `clear` is called twice?** The second call sees `bucket_count_ == 0`, so the loop does nothing, and `deallocate(nullptr)` returns immediately. No double free.

**Why does the constructor allocate nothing?** T13 checks that after `clear()` the tracked memory equals the empty-object level. If the constructor allocated, `clear` would have to recreate exactly that allocation. Allocating lazily on the first `put` is simpler and saves memory.

**What is the complexity of `put`?** Average O(L) amortised: O(L) to hash and copy, O(1) expected chain length, and doubling costs O(n) only after n/2 inserts. Worst case O(n·L) if every word collides.

**How much memory per word?** 14 + L bytes for the node plus 4–8 bytes of bucket array, about 35 bytes for the 5,000-key workload.

**What would you improve with more time?** For millions of words, a different growth step (for example 1.5×) would keep the load factor in a narrower band. We tested it: it saved about 2% memory but cost about 8% time, so it was not worth it here.

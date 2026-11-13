# Research: How We Chose the Design

This folder holds everything we used to choose and tune `StudentIndex`. Nothing here is part of the submission. It is the evidence behind the choice.

## Designs we raced

Every file in [`variants/`](variants) is a complete student region. Each one passes all 20 public tests at many scales and seeds, and runs clean under AddressSanitizer and UndefinedBehaviorSanitizer.

| File | Idea | Bytes per word (header + L) |
|---|---|---|
| `v0_baseline.hpp` | First prototype: chained, node = next + 32-bit hash + ListId + 32-bit length, FNV-1a hash (1 character per step) | 20 + L |
| `vA_chain_fasthash.hpp` | Same node, 64-bit hash that reads 8 characters per step | 20 + L |
| `vB_chain_compact.hpp` | Compact node: next + ListId + varint length, no stored hash | 13 + L |
| `vC_chain_fp.hpp` | vB + 1-byte fingerprint (low 8 bits of the hash) | 14 + L |
| `vC2_eq.hpp` | vC + equality check 8 bytes at a time (**the submitted design**) | 14 + L |
| `vC3_lighthash.hpp` | vC with a cheaper hash finaliser (rejected: poor fingerprint spread on `term<i>` keys) | 14 + L |
| `vD_oa_ptr.hpp` | Open addressing, slots of pointers, load ≤ 3/4, backward-shift delete | 5 + L, plus 8 per slot |
| `vE_oa_fp.hpp` | Open addressing + 1-byte control array, load ≤ 7/8 | 5 + L, plus 9 per slot |
| `vF_chain_compact_hash.hpp` | Compact node + stored 32-bit hash (no rehashing on growth) | 17 + L |
| `vG_chain_fastrange.hpp` | Compact node, any bucket count, growth ×1.5 | 13 + L |
| `c1_typical_chain.hpp` | *Reference:* what many teams will write: nodes holding a `Record`, `std::hash`, load 1 | 32 + L (two allocations) |
| `c2_typical_oa.hpp` | *Reference:* open addressing over a `Record` array, tombstones, load ≤ 1/2 | 24 per slot + L |

## Results

[`results/benchmark.txt`](results/benchmark.txt) holds the raw output of the final comparison: 11 designs, 16 workloads, 31 runs each, medians. E is computed with the official formula against the best of the set. Short version:

| Design | T | M | E |
|---|---|---|---|
| **submitted (= vC2_eq + comments)** | **0.89** | **0.93** | **9.1** |
| vC_chain_fp | 0.88 | 0.93 | 9.0 |
| vB_chain_compact | 0.81 | 0.96 | 8.7 |
| vF_chain_compact_hash | 0.88 | 0.85 | 8.6 |
| vE_oa_fp | 0.84 | 0.88 | 8.4 |
| vG_chain_fastrange | 0.74 | 0.98 | 8.4 |
| vA_chain_fasthash | 0.89 | 0.78 | 8.3 |
| vD_oa_ptr | 0.78 | 0.81 | 7.8 |
| v0_baseline | 0.76 | 0.78 | 7.7 |
| c1_typical_chain | 0.68 | 0.54 | 6.0 |
| c2_typical_oa | 0.72 | 0.26 | 3.8 |

We also tried scrambling each 8-byte block before it enters the hash. This hardens the hash against keys crafted so that bit flips cancel. It was about 4% slower (T 0.96 against the submitted code), which is outside the brief's 3% tie band. Ordinary key families do not trigger the weakness, so we kept the faster hash.

Load-factor sweep of vC (max words per bucket before doubling): 1.0 → E 8.66, 1.5 → 8.68, **2.0 → 8.78**, 2.5 → 8.71, 3.0 → 8.62, 4.0 → 8.54. We kept 2.

## Tools

| File | What it does |
|---|---|
| `template.cpp` | The original template, kept for comparison |
| `splice.py TEMPLATE REGION OUT` | Puts a region file between the markers of the template |
| `check_rules.py TEMPLATE CANDIDATE` | Checks that everything outside the region is byte-identical, and flags banned constructs inside it |
| `verify.sh CANDIDATE.cpp` | Rules check, strict compile, all tests at 16 scales × 6 seeds, sanitizer runs, demo output, benchmark correctness |
| `gen_wl.py N SEED OUT` / `make_workloads.sh` | CSV workloads shaped like `data/small.csv` |
| `bench.py REPS grade|wide|all BIN...` | Runs every workload round-robin, reports medians and E against the best of the set |
| `hashq.cpp` | Hash quality: bucket occupancy and fingerprint spread for several key families |

Example:

```bash
cd research
./make_workloads.sh
python3 splice.py template.cpp variants/vC2_eq.hpp variants/vC2_eq.cpp
./verify.sh variants/vC2_eq.cpp
g++ -std=c++17 -O2 variants/vC2_eq.cpp -o bin/vC2_eq
python3 bench.py 31 grade bin/vC2_eq
```

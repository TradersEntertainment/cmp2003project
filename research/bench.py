# Usage: python3 bench.py REPS SET bin1 bin2 ...   (run from anywhere; workloads are read from research/wl)
# SET = grade (csv200/1500/5000 + syn200/1500/5000) or wide (csv at many n) or all
import sys, subprocess, statistics, os, itertools
W = os.path.dirname(os.path.abspath(__file__))
reps = int(sys.argv[1]); which = sys.argv[2]; bins = sys.argv[3:]
grade = [(f'csv{n}', ['--workload', f'{W}/wl/csv{n}.csv']) for n in (200, 1500, 5000)] + [(f'syn{n}', ['--n', str(n)]) for n in (200, 1500, 5000)]
wide = [(f'csv{n}', ['--workload', f'{W}/wl/csv{n}.csv']) for n in (100, 300, 500, 700, 1000, 2000, 3000, 4000, 6500, 8000)]
loads = {'grade': grade, 'wide': wide, 'all': grade + wide}[which]
res = {(b, l): [] for b in bins for l, _ in loads}
for r in range(reps):
    order = bins if r % 2 == 0 else bins[::-1]
    for (l, args), b in itertools.product(loads, order):
        out = subprocess.run(['taskset', '-c', '2', b, '--bench'] + args, capture_output=True, text=True, check=True).stdout
        d = dict(line.split() for line in out.strip().splitlines())
        res[(b, l)].append((float(d['build_seconds']), float(d['lookup_seconds']), float(d['update_seconds']), int(d['peak_bytes'])))
names = [os.path.basename(b) for b in bins]
tot = {b: [] for b in bins}; Ts = {b: [] for b in bins}; Ms = {b: [] for b in bins}
verbose = os.environ.get('V', '1') == '1'
for l, _ in loads:
    med = {b: tuple(statistics.median(x[i] for x in res[(b, l)]) for i in range(4)) for b in bins}
    best = [min(med[b][i] for b in bins) for i in range(4)]
    if verbose: print(f'== {l}')
    for b, nm in zip(bins, names):
        m = med[b]
        rat = [1.0 if m[i] <= best[i] * 1.03 else best[i] / m[i] for i in range(4)]
        T = 0.3 * rat[0] + 0.4 * rat[1] + 0.3 * rat[2]; M = rat[3]; E = 10 * 2 * T * M / (T + M)
        tot[b].append(E); Ts[b].append(T); Ms[b].append(M)
        if verbose: print(f'  {nm:26s} b {m[0]*1e6:7.1f} l {m[1]*1e6:7.1f} u {m[2]*1e6:7.1f} us  peak {m[3]:7d}  T {T:.3f} M {M:.3f} E {E:.2f}')
print('== mean over workloads: T  M  E')
for b, nm in sorted(zip(bins, names), key=lambda x: -sum(tot[x[0]])):
    k = len(tot[b]); print(f'  {nm:26s} T {sum(Ts[b])/k:.3f}  M {sum(Ms[b])/k:.3f}  E {sum(tot[b])/k:.2f}')

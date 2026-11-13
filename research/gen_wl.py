# Generates a CSV workload shaped like the supplied data/small.csv.
import sys, random
n, seed, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
rng = random.Random(seed)
others = ['network', 'catalog', 'data', 'report', 'security']
keys = []
for i in range(n):
    if rng.random() < 1/3: keys.append('sharedprefixarchive%06x' % i)
    else: keys.append(rng.choice(others) + '%x' % i)
rng.shuffle(keys)
ids = [rng.getrandbits(32) for _ in keys]
if n >= 2: ids[0], ids[1] = 0, 4294967295
with open(out, 'w') as f:
    f.write('phase,op,word,list_id\n')
    for k, v in zip(keys, ids): f.write(f'build,P,{k},{v}\n')
    for i in range(2 * n):
        if i % 4 == 0: f.write(f'lookup,F,missing{i},-\n')
        else: f.write(f'lookup,F,{rng.choice(keys)},-\n')
    for g in range(0, n, 4):
        k = keys[g // 4]
        f.write(f'update,P,{k},{rng.getrandbits(32)}\n')
        f.write(f'update,E,{k},-\n')
        f.write(f'update,P,fresh{g + 2},{rng.getrandbits(32)}\n')
        f.write(f'update,E,{k},-\n')

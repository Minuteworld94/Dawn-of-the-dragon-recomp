"""Names the call stacks the sampling profiler logs (env SPYRO_PROFILE, src/spyro_profiler.cpp,
WORK_LOG 124) with the linker map spyrodotd.map.

Usage: python -I profile_names.py <spyrodotd.map> <log> [thread name filter] [max stacks]
Prints, for the last report of each matching thread: the busiest functions (self time: where
the thread was, waits in ntdll / win32u included) and the top call stacks with names.
"""
import bisect
import collections
import re
import sys

map_path, log_path = sys.argv[1], sys.argv[2]
want = sys.argv[3] if len(sys.argv) > 3 else 'Main thread'
max_stacks = int(sys.argv[4]) if len(sys.argv) > 4 else 25

base = 0x140000000
syms = []
for line in open(map_path, errors='replace'):
    m = re.match(r'\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{16})\s', line)
    if m:
        syms.append((int(m.group(2), 16) - base, m.group(1)))
syms.sort()
addrs = [a for a, _ in syms]


def name(tok):
    if not re.fullmatch(r'[0-9A-F]+', tok):
        return tok
    rva = int(tok, 16)
    i = bisect.bisect_right(addrs, rva) - 1
    if i < 0:
        return tok
    n = syms[i][1]
    n = re.sub(r'^\?+', '', n)
    return n[:60]


# The last report block per thread: header line then "stack N | ..." lines.
blocks = collections.OrderedDict()
cur = None
for line in open(log_path, errors='replace'):
    if '[spyro/prof]' not in line:
        continue
    body = line.split('[spyro/prof]', 1)[1].strip()
    if body.startswith('stack '):
        if cur is not None:
            n, frames = body[6:].split('|', 1)
            blocks[cur].append((int(n), frames.split()))
    elif 'busy' in body:
        cur = body.split(' (')[0] if want in body else None
        if cur is not None:
            blocks[cur] = []
            header = body

for thread, stacks in blocks.items():
    total = 0
    for line in open(log_path, errors='replace'):
        pass
    print('==', header[:160])
    print('-- top stacks (samples, leaf first):')
    for n, frames in stacks[:max_stacks]:
        names = []
        for f in frames:
            nm = name(f)
            if not names or names[-1] != nm:
                names.append(nm)
        print('%5d  %s' % (n, '  <  '.join(names[:8])))
    # inclusive: samples per function anywhere in the stacks shown
    incl = collections.Counter()
    for n, frames in stacks:
        for nm in set(name(f) for f in frames):
            incl[nm] += n
    print('-- functions on the most sampled stacks (inclusive, of %d listed samples):' % sum(n for n, _ in stacks))
    for nm, n in incl.most_common(30):
        print('%5d  %s' % (n, nm))

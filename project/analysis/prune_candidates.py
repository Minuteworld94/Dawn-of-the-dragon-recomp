"""Ban auto-discovered function candidates that caused 'Unresolved ... branch' reports.

Usage: py -3 -I prune_candidates.py <build_log.txt> <functions_auto.toml> <banned_candidates.txt>

A candidate is banned when an unresolved branch's TARGET is the candidate, falls inside it, or its
SOURCE is inside it (i.e. the candidate cut a function in two / is really an internal label).
Appends to the ban list; find_func_pointers.py skips banned addresses on the next run.
"""
import re
import sys

log, toml, banfile = sys.argv[1:4]

cands = []  # (start, end)
cur = None
for line in open(toml):
    m = re.match(r"\[functions\.([0-9A-Fa-f]+)\]", line.strip())
    if m:
        cur = int(m.group(1), 16)
        cands.append([cur, None])
        continue
    m = re.match(r"end\s*=\s*0x([0-9A-Fa-f]+)", line.strip())
    if m and cands:
        cands[-1][1] = int(m.group(1), 16)

pairs = set()
for line in open(log, errors="replace"):
    m = re.search(r"Unresolved.*?0x([0-9A-F]+) from 0x([0-9A-F]+)", line)
    if m:
        pairs.add((int(m.group(1), 16), int(m.group(2), 16)))

banned_now = set()
for t, s in pairs:
    for start, end in cands:
        end = end or start + 4
        if start == t or start <= t < end or start <= s < end:
            banned_now.add(start)

old = set()
try:
    old = {int(x, 16) for x in open(banfile).read().split()}
except FileNotFoundError:
    pass
new = banned_now - old
with open(banfile, "w") as f:
    for a in sorted(old | banned_now):
        f.write(f"{a:08X}\n")
print(f"unresolved branch reports: {len(pairs)}   candidates implicated: {len(banned_now)}   newly banned: {len(new)}   total banned: {len(old | banned_now)}")

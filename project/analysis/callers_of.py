"""List which recompiled functions call the given functions / kernel imports.

Usage: py -3 -I callers_of.py <generated_dir> NAME [NAME ...]
  NAME = sub_XXXXXXXX for a game function, or a kernel import name such as VdSwap
  (calls appear as __imp__VdSwap(ctx, base) in the generated code).
Prints "NAME <- caller caller ..." per name (each caller listed once).
"""
import pathlib
import re
import sys

gen = pathlib.Path(sys.argv[1])
names = sys.argv[2:]
patterns = {n: re.compile(r"\b(?:__imp__)?" + re.escape(n) + r"\(ctx, base\)") for n in names}
callers = {n: [] for n in names}
for f in sorted(gen.glob("spyrodotd_recomp.*.cpp")):
    cur = None
    for line in f.open(encoding="utf-8", errors="replace"):
        m = re.match(r"(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?(sub_[0-9A-F]+)\)", line)
        if m:
            cur = m.group(1)
            continue
        if cur is None:
            continue
        for n, p in patterns.items():
            if p.search(line) and cur != n and cur not in callers[n]:
                callers[n].append(cur)
for n in names:
    print(f"{n} <- {' '.join(callers[n]) or '(none)'}")

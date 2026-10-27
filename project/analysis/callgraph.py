"""Build (once) and query the call graph of the recompiled game.

Build:  py -3 -I callgraph.py build <generated_dir> <out.json>
        Records, for every function, its direct callees (sub_ calls and __imp__ kernel calls)
        and how many bctrl (indirect) calls it makes.
Region: py -3 -I callgraph.py region <graph.json> <lo_hex> <hi_hex>
        For every function in [lo, hi): callers from inside / outside the range, so the entry
        points of a statically linked library show up as "called from outside".
"""
import json
import pathlib
import re
import sys


def build(gen, out):
    graph = {}
    cur = None
    call = re.compile(r"\t(?:__imp__)?(sub_[0-9A-F]{8}|[A-Z][A-Za-z0-9_]+)\(ctx, base\);")
    for f in sorted(pathlib.Path(gen).glob("spyrodotd_recomp.*.cpp")):
        for line in f.open(encoding="utf-8", errors="replace"):
            m = re.match(r"(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?(sub_[0-9A-F]+)\)", line)
            if m:
                cur = m.group(1)
                graph.setdefault(cur, {"calls": [], "bctrl": 0})
                continue
            if cur is None:
                continue
            if "// bctrl" in line:
                graph[cur]["bctrl"] += 1
            m = call.search(line)
            if m and m.group(1) != cur and m.group(1) not in graph[cur]["calls"]:
                graph[cur]["calls"].append(m.group(1))
    json.dump(graph, open(out, "w"), indent=0)
    print(f"{len(graph)} functions -> {out}")


def addr(name):
    return int(name[4:], 16) if name.startswith("sub_") else None


def region(path, lo, hi):
    graph = json.load(open(path))
    callers = {}
    for f, info in graph.items():
        for c in info["calls"]:
            callers.setdefault(c, []).append(f)
    for f in sorted(graph, key=lambda n: addr(n)):
        a = addr(f)
        if not (lo <= a < hi):
            continue
        cs = callers.get(f, [])
        inside = [c for c in cs if lo <= addr(c) < hi]
        outside = [c for c in cs if not (lo <= addr(c) < hi)]
        kern = [c for c in graph[f]["calls"] if not c.startswith("sub_")]
        print(f"{f} in {len(inside):3d} out {len(outside):3d} callees {len(graph[f]['calls']):3d} "
              f"bctrl {graph[f]['bctrl']:2d} {' '.join(kern)}"
              + (f"  <- {' '.join(outside[:6])}" if outside else ""))


if sys.argv[1] == "build":
    build(sys.argv[2], sys.argv[3])
else:
    region(sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16))

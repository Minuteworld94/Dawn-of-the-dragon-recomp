"""Print the outgoing calls / indirect calls of recompiled functions, compactly.

Usage: py -3 -I calls_of.py <generated_dir> sub_822E0A28 [sub_... ...]

Reads the generated .cpp files once, then for each function prints the PPC comment lines that
matter: calls (bl), indirect calls through the vtable (bctrl), and loads of the vtable slot,
so the call graph is visible without wading through every instruction.
"""
import pathlib
import re
import sys

gen = pathlib.Path(sys.argv[1])
wanted = sys.argv[2:]

bodies = {}
for path in sorted(gen.glob("spyrodotd_recomp.*.cpp")):
    text = path.read_text(errors="replace")
    for m in re.finditer(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]+)\) \{$", text, re.M):
        name = m.group(1)
        if name not in wanted:
            continue
        start = m.end()
        end = text.find("\n}\n", start)
        bodies[name] = (path.name, text[start:end])

keep = re.compile(r"//\s*(bl |bctrl|bctr\b|lwz r\d+,\d+\(r\d+\)|li r3,|lis |addi r\d+,r\d+,-?\d+|stw r\d+,\d+\(r3\)|cmp\w*|b\w*\s+0x)")
for name in wanted:
    if name not in bodies:
        print(f"\n==== {name}: not found")
        continue
    fname, body = bodies[name]
    lines = [l.strip()[2:].strip() for l in body.splitlines() if l.strip().startswith("//")]
    print(f"\n==== {name}   ({fname}, {len(lines)} instructions)")
    for l in lines:
        if re.match(r"(bl |bctrl|bctr$)", l):
            print(f"    CALL  {l}")
        elif l.startswith(("lwz r", "lis ", "addi ", "li ")) and ("r3" in l or "r11" in l or "r12" in l):
            print(f"          {l}")

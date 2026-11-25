# Packs a folder into a .tar.gz with Linux file modes (WORK_LOG 158): zips made on Windows lose the
# "executable" flag, so the Linux release is a tarball with executables (and .so files) at 0755.
# Usage: python -I package_tar.py <folder> <out.tar.gz> <executable relative path>...
import os, sys, tarfile

src, out = os.path.abspath(sys.argv[1]), sys.argv[2]
executables = {p.replace("\\", "/") for p in sys.argv[3:]}
top = os.path.basename(src)


def add(tar, path, arc):
    info = tar.gettarinfo(path, arc)
    rel = arc.split("/", 1)[1] if "/" in arc else ""
    if info.isdir():
        info.mode = 0o755
    elif rel in executables or arc.endswith(".so") or ".so." in os.path.basename(arc):
        info.mode = 0o755
    else:
        info.mode = 0o644
    info.uid = info.gid = 1000
    info.uname = info.gname = "user"
    if info.isfile():
        with open(path, "rb") as f:
            tar.addfile(info, f)
    else:
        tar.addfile(info)


with tarfile.open(out, "w:gz", compresslevel=9) as tar:
    add(tar, src, top)
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for d in dirs:
            p = os.path.join(root, d)
            add(tar, p, top + "/" + os.path.relpath(p, src).replace("\\", "/"))
        for f in sorted(files):
            p = os.path.join(root, f)
            add(tar, p, top + "/" + os.path.relpath(p, src).replace("\\", "/"))
print(f"wrote {out} ({os.path.getsize(out) / 1e6:.1f} MB)")

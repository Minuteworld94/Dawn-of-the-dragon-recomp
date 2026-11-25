# Unpacks the Steam Runtime "sniper" SDK sysroot (a Linux root file system) on Windows for
# cross-compiling the Linux build (WORK_LOG 158). Windows cannot make Linux symlinks without
# special rights, so: file links become copies of their target, folder links become junctions
# (resolved inside the sysroot, absolute targets included). Only what a build needs is kept:
# headers, 64-bit libraries, GCC's runtime, pkg-config files, wayland protocol XML.
#
# Usage: python -I extract_sysroot.py <sysroot.tar.gz> <out folder>
import os, posixpath, shutil, sys, tarfile

try:
    import _winapi  # CreateJunction (no admin rights needed)
except ImportError:
    _winapi = None

KEEP = ("usr/include/", "usr/lib/x86_64-linux-gnu/", "usr/lib/gcc/x86_64-linux-gnu/", "usr/lib64/",
        "usr/lib/pkgconfig/", "usr/share/pkgconfig/", "usr/share/wayland/", "usr/share/wayland-protocols/",
        "lib/x86_64-linux-gnu/", "lib64/", "etc/ld.so.conf",
        # Valve's GCC 14 (C++23 library; static libstdc++ / libgcc) - the toolchain file points clang at it.
        "usr/lib/gcc-14/lib/gcc/x86_64-linux-gnu/", "usr/lib/gcc-14/x86_64-linux-gnu/")
SKIP_PARTS = ("/share/doc/", "/share/man/", "/share/locale/", "/share/info/")


def wanted(name):
    n = name.lstrip("./")
    if any(p in "/" + n for p in SKIP_PARTS):
        return False
    return any(n.startswith(k) or k.startswith(n + "/") or n + "/" == k for k in KEEP) or n in ("lib", "lib64", "bin", "sbin")


def main():
    src, out = sys.argv[1], os.path.abspath(sys.argv[2])
    os.makedirs(out, exist_ok=True)
    links = []  # (link path inside sysroot, target as written, is hard link)
    files = dirs = 0
    with tarfile.open(src, "r:gz") as tar:
        for m in tar:
            name = m.name.lstrip("./")
            if not name or not wanted(name):
                continue
            dest = os.path.join(out, *name.split("/"))
            if m.isdir():
                os.makedirs(dest, exist_ok=True)
                dirs += 1
            elif m.issym() or m.islnk():
                links.append((name, m.linkname, m.islnk()))
            elif m.isfile():
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with tar.extractfile(m) as f, open(dest, "wb") as o:
                    shutil.copyfileobj(f, o, 1 << 20)
                files += 1
    print(f"extracted {files} files, {dirs} folders; resolving {len(links)} links")

    def resolve(link, target, hard):
        # Path inside the sysroot the link points to (posix, no leading slash).
        if hard or target.startswith("/"):
            p = target.lstrip("/") if not hard else target.lstrip("./")
        else:
            p = posixpath.join(posixpath.dirname(link), target)
        return posixpath.normpath(p)

    pending = links
    for _ in range(12):  # links to links: repeat until nothing changes
        left = []
        for link, target, hard in pending:
            dest = os.path.join(out, *link.split("/"))
            if os.path.lexists(dest):
                continue
            t = resolve(link, target, hard)
            tpath = os.path.join(out, *t.split("/"))
            if os.path.isdir(tpath):
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                if _winapi:
                    _winapi.CreateJunction(tpath, dest)
                else:
                    shutil.copytree(tpath, dest)
            elif os.path.isfile(tpath):
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copy2(tpath, dest)
            else:
                left.append((link, target, hard))
        if len(left) == len(pending):
            break
        pending = left
    print(f"unresolved links: {len(pending)}")
    for link, target, _ in pending[:25]:
        print(f"  {link} -> {target}")
    # GCC 14 has no libgcc_eh.a: the build links libgcc_s dynamically, through GCC 10's linker script.
    gcc10 = os.path.join(out, "usr", "lib", "gcc", "x86_64-linux-gnu", "10", "libgcc_s.so")
    gcc14 = os.path.join(out, "usr", "lib", "gcc-14", "lib", "gcc", "x86_64-linux-gnu", "14")
    if os.path.isfile(gcc10) and os.path.isdir(gcc14):
        shutil.copy2(gcc10, os.path.join(gcc14, "libgcc_s.so"))
        print("copied libgcc_s.so (linker script) into the GCC 14 folder")


if __name__ == "__main__":
    main()

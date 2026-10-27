"""Which interpolator / vertex-input semantics do Spyro's shaders use?

Usage: py -3 -I shader_semantics.py <shaders dir>
Reads every captured Xbox 360 shader container (layout as in XenosRecomp's shader.h) and counts
  - vertex shader inputs (vertex elements) and outputs (interpolators): usage + index
  - pixel shader inputs (interpolators): usage + index
"""
import collections
import pathlib
import struct
import sys

USAGES = ["Position", "BlendWeight", "BlendIndices", "Normal", "PointSize", "TexCoord", "Tangent",
          "Binormal", "TessFactor", "PositionT", "Color", "Fog", "Depth", "Sample"]

def be32(d, o):
    return struct.unpack(">I", d[o:o + 4])[0]

vs_in, vs_out, ps_in = collections.Counter(), collections.Counter(), collections.Counter()
max_vs_out = max_ps_in = 0
for f in sorted(pathlib.Path(sys.argv[1]).glob("*.bin")):
    d = f.read_bytes()
    flags = be32(d, 0)
    shader = be32(d, 0x18)            # ShaderContainer.shaderOffset
    info = be32(d, shader + 0x14)      # Shader.interpolatorInfo
    count = (info >> 5) & 0x1F
    if flags & 1:                      # vertex shader
        field18 = be32(d, shader + 0x18)
        n_elem = be32(d, shader + 0x1C)
        arr = shader + 0x24
        for i in range(n_elem):
            v = be32(d, arr + 4 * (field18 + i))
            usage, index = (v >> 12) & 0xF, (v >> 16) & 0xF
            vs_in[(USAGES[usage], index)] += 1
        for i in range(count):
            v = be32(d, arr + 4 * (field18 + n_elem + i))
            index, usage = v & 0xF, (v >> 4) & 0xF
            vs_out[(USAGES[usage], index)] += 1
        max_vs_out = max(max_vs_out, count)
    else:                              # pixel shader
        arr = shader + 0x20
        for i in range(count):
            v = be32(d, arr + 4 * i)
            index, usage = v & 0xF, (v >> 4) & 0xF
            ps_in[(USAGES[usage], index)] += 1
        max_ps_in = max(max_ps_in, count)

for title, c in (("VS inputs", vs_in), ("VS outputs", vs_out), ("PS inputs", ps_in)):
    print(title + ":", ", ".join(f"{u}{i} x{n}" for (u, i), n in sorted(c.items())))
print("max interpolators: VS out", max_vs_out, " PS in", max_ps_in)

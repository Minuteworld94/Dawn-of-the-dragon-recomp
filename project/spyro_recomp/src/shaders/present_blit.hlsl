// plume renderer: draws the game's front buffer into the window (WORK_LOG 106). Uses the same
// root signature as the translated game shaders (plume_renderer.cpp CreateLayout): textures in
// t0 space0, samplers in s0 space3, the root constant b3 space4 = descriptor index of the image.
// Compiled at build time by dxc (CMakeLists.txt) into present_blit_vs.h / present_blit_ps.h.

Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Samplers[] : register(s0, space3);

cbuffer Blit : register(b3, space4)
{
    uint g_Texture;
};

struct VsOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

// One triangle covering the viewport.
VsOut VSMain(uint id : SV_VertexID)
{
    VsOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VsOut i) : SV_Target
{
    // Sampler 1 = linear, clamp.
    return float4(g_Textures[g_Texture].Sample(g_Samplers[1], i.uv).rgb, 1.0);
}

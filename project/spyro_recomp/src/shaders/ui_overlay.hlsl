// plume overlay UI (spyro_ui.h, WORK_LOG 144): textured, vertex-coloured triangles in window
// pixels. Same root signature as the game shaders (plume_renderer.cpp CreateLayout): textures in
// t0 space0, samplers in s0 space3, push constants in b3 space4.
// Compiled at build time by dxc (CMakeLists.txt) into ui_overlay_vs.h / ui_overlay_ps.h.

Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Samplers[] : register(s0, space3);

cbuffer Ui : register(b3, space4)
{
    uint g_Texture;        // descriptor index of the command's texture
    float2 g_InvHalfSize;  // 2 / window width, 2 / window height
    float g_Unused;
};

struct VsIn
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VsOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VsOut VSMain(VsIn i)
{
    VsOut o;
    o.pos = float4(i.pos.x * g_InvHalfSize.x - 1.0, 1.0 - i.pos.y * g_InvHalfSize.y, 0.0, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    return o;
}

float4 PSMain(VsOut i) : SV_Target
{
    // Sampler 1 = linear, clamp.
    return g_Textures[g_Texture].Sample(g_Samplers[1], i.uv) * i.color;
}

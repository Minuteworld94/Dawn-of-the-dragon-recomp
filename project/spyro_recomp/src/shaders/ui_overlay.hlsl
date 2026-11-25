// plume overlay UI (spyro_ui.h, WORK_LOG 144): textured, vertex-coloured triangles in window
// pixels. Same root signature as the game shaders (plume_renderer.cpp CreateLayout): textures in
// t0 space0, samplers in s0 space3, push constants in b3 space4.
// Compiled at build time by dxc (CMakeLists.txt) into ui_overlay_vs.h / ui_overlay_ps.h.

// Vulkan (SPIR-V, WORK_LOG 156): the same 16 bytes as push constants - scalars only, so the
// offsets are the D3D ones whatever layout rules apply.

Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Samplers[] : register(s0, space3);

#ifdef __spirv__
struct UiConstants
{
    uint Texture;
    float InvHalfX, InvHalfY;
    float Unused;
};
[[vk::push_constant]] UiConstants g_Ui;
#define g_Texture g_Ui.Texture
#define g_InvHalfSize float2(g_Ui.InvHalfX, g_Ui.InvHalfY)
#else
cbuffer Ui : register(b3, space4)
{
    uint g_Texture;        // descriptor index of the command's texture
    float2 g_InvHalfSize;  // 2 / window width, 2 / window height
    float g_Unused;
};
#endif

// Vulkan matches vertex inputs by location, not by semantic: the locations of the pipeline's input
// elements (plume_renderer.cpp CreateLayout: 0 / 4 / 8).
struct VsIn
{
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(4)]] float2 uv : TEXCOORD0;
    [[vk::location(8)]] float4 color : COLOR0;
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

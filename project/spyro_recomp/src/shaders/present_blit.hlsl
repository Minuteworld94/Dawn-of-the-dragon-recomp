// plume renderer: draws the game's front buffer into the window (WORK_LOG 106). Uses the same
// root signature as the translated game shaders (plume_renderer.cpp CreateLayout): textures in
// t0 space0, samplers in s0 space3, the root constant b3 space4 = descriptor index of the image.
// Compiled at build time by dxc (CMakeLists.txt) into present_blit_vs.h / present_blit_ps.h.

Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Samplers[] : register(s0, space3);

cbuffer Blit : register(b3, space4)
{
    uint g_Texture;
    float2 g_Dot;       // lock-on target dot centre, 0..1 across the picture (spyro_lockon.cpp)
    float g_DotRadius;  // in picture heights; 0 = no dot
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
    float3 c = g_Textures[g_Texture].Sample(g_Samplers[1], i.uv).rgb;
    if (g_DotRadius > 0.0)
    {
        // White dot with a soft dark rim (16:9: x distances in picture heights).
        float2 d = (i.uv - g_Dot) * float2(16.0 / 9.0, 1.0);
        float r = length(d) / g_DotRadius;
        float px = fwidth(r);
        float rim = 1.0 - smoothstep(1.45 - px, 1.45 + px, r);
        float disc = 1.0 - smoothstep(1.0 - px, 1.0 + px, r);
        c = lerp(c, float3(0.0, 0.0, 0.0), rim * 0.45);
        c = lerp(c, float3(1.0, 1.0, 1.0), disc * 0.95);
    }
    return float4(c, 1.0);
}

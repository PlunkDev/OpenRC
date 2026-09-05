struct SourceVertexInput
{
    float4 clip_position : POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

struct SourcePixelInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

SourcePixelInput vs_source(SourceVertexInput input)
{
    SourcePixelInput output;
    output.position = input.clip_position;
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}

Texture2D diffuse_texture : register(t0);
SamplerState diffuse_sampler : register(s0);

float4 ps_textured(SourcePixelInput input) : SV_Target
{
    const float4 texel = diffuse_texture.Sample(diffuse_sampler, input.texcoord);
    clip(texel.a - (0.5F / 255.0F));
    return texel;
}

// RenderSceneV1 always binds a texture. Untextured materials use a private
// linear 1x1 white texture so this shader has one deterministic path.
cbuffer RenderSceneMaterialConstants : register(b0)
{
    float4 material_base_color;
    float use_vertex_color;
    float alpha_cutoff;
    float2 material_padding;
};

float3 linear_to_srgb(float3 value)
{
    const float3 non_negative = max(value, float3(0.0F, 0.0F, 0.0F));
    const float3 linear_segment = 12.92F * non_negative;
    const float3 power_segment =
        1.055F * pow(non_negative, 1.0F / 2.4F) - 0.055F;
    return lerp(
        linear_segment,
        power_segment,
        step(float3(0.0031308F, 0.0031308F, 0.0031308F), non_negative));
}

float4 ps_render_scene(SourcePixelInput input) : SV_Target
{
    const float4 vertex_factor =
        lerp(float4(1.0F, 1.0F, 1.0F, 1.0F),
             input.color,
             use_vertex_color);
    const float4 texel = diffuse_texture.Sample(diffuse_sampler, input.texcoord);
    const float4 final_color =
        material_base_color * vertex_factor * texel;

    // Opaque materials upload -1.0. Masked materials upload their exact
    // unsigned-byte cutoff normalized to [0,1]; clip discards only values
    // below zero, so equality passes as required by RenderSceneV1.
    clip(final_color.a - alpha_cutoff);
    // The swap-chain RTV is UNORM. Texture SRVs decode authored sRGB bytes to
    // linear light, so encode RGB exactly once before presenting.
    return float4(linear_to_srgb(final_color.rgb), final_color.a);
}

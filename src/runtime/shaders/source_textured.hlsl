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

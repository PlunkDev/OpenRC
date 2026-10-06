struct PixelInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> destination_image : register(t0);
Texture2D<uint4> coverage_image : register(t1);
cbuffer OverlayConstants : register(b0)
{
    uint coverage_denominator;
    uint image_width;
    uint image_height;
    uint reserved;
};

float4 ps_screen_overlay(PixelInput input) : SV_Target
{
    const uint2 source_pixel = min(uint2(input.texcoord * uint2(image_width, image_height)),
                                  uint2(image_width - 1U, image_height - 1U));
    const uint4 source = coverage_image.Load(int3(source_pixel, 0));
    const uint3 destination = uint3(round(destination_image.Load(int3(uint2(input.position.xy), 0)).rgb * 255.0F));
    // Positive integer division is floor, including darkening. This is
    // algebraically dst + floor((src-dst)*coverage/denominator), avoiding
    // signed division's truncation and floating UNORM alpha rounding.
    const uint3 result = (source.rgb * source.a +
        destination * (coverage_denominator - source.a)) / coverage_denominator;
    return float4(float3(result) / 255.0F, 1.0F);
}

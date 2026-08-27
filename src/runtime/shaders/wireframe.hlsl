cbuffer FitConstants : register(b0)
{
    float2 fit_scale;
    float2 fit_offset;
};

struct VertexInput
{
    float2 position : POSITION;
    float4 color : COLOR0;
};

struct PixelInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

PixelInput vs_main(VertexInput input)
{
    PixelInput output;
    output.position = float4(
        input.position * fit_scale + fit_offset,
        0.0F,
        1.0F);
    output.color = input.color;
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    return float4(input.color.rgb, 1.0F);
}

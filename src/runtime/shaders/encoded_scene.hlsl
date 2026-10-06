// Neutral display-encoded integer material arithmetic. Texture coordinates
// choose a four-bit bilinear reference with horizontal then vertical floor.
// Geometry interpolation/raster coverage follows D3D11; this is not a claim
// about physical hardware subpixel DDA rounding on the original platform.
struct VertexInput {
    float4 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};
struct PixelInput {
    float4 position : SV_Position;
    noperspective float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};
struct AffineInput {
    float4 position : SV_Position;
    noperspective float4 color : COLOR0;
    noperspective float2 uv : TEXCOORD0;
};
PixelInput vs_encoded(VertexInput input) {
    PixelInput output;
    output.position=input.position;output.color=round(input.color*255.0);output.uv=input.uv;
    return output;
}
AffineInput vs_encoded_affine(VertexInput input) {
    AffineInput output;
    output.position=input.position;output.color=round(input.color*255.0);output.uv=input.uv;
    return output;
}
Texture2D<float4> color_texture : register(t0);
Texture2D<float4> destination : register(t1);
cbuffer EncodedMaterial : register(b0) {
    uint modulation_denominator;
    uint blend_denominator;
    uint policy_flags; // texture,source-over,bilinear,repeatU,repeatV,mask,invert-mask
    uint alpha_cutoff_byte;
    uint2 texture_dimensions;
    uint2 padding;
};
int address(int value,int size,bool repeat_address) {
    if(!repeat_address) return clamp(value,0,size-1);
    uint magnitude=value<0?(uint)(-value):(uint)value;
    uint wrapped=magnitude%(uint)size;
    return value<0&&wrapped!=0?size-(int)wrapped:(int)wrapped;
}
uint4 texel(int2 coordinate) {
    coordinate.x=address(coordinate.x,(int)texture_dimensions.x,(policy_flags&8)!=0);
    coordinate.y=address(coordinate.y,(int)texture_dimensions.y,(policy_flags&16)!=0);
    return (uint4)round(color_texture.Load(int3(coordinate,0))*255.0);
}
uint4 sample_color(float2 uv) {
    uint4 sampled=uint4(0,0,0,0);
    if((policy_flags&4)==0) {
        sampled=texel((int2)floor(uv*texture_dimensions));
    } else {
    // Values remain in bounded signed integer coordinates admitted by the
    // neutral vertex validator. Correct floor/repeat also applies below zero.
    int2 fixed_coordinate=(int2)floor(uv*texture_dimensions*16.0-8.0);
    int2 base=fixed_coordinate>>4;
    uint2 fraction=(uint2)(fixed_coordinate-base*16);
    uint4 upper=(texel(base)*(16u-fraction.x)+texel(base+int2(1,0))*fraction.x)>>4;
    uint4 lower=(texel(base+int2(0,1))*(16u-fraction.x)+texel(base+int2(1,1))*fraction.x)>>4;
        sampled=(upper*(16u-fraction.y)+lower*fraction.y)>>4;
    }
    return sampled;
}
int floor_divide(int value,uint divisor) {
    return value>=0?(int)((uint)value/divisor):-(int)(((uint)(-value)+divisor-1u)/divisor);
}
float4 shade(float4 position,float4 color,float2 uv) {
    // Decode packed vertex bytes before interpolation. Normalized 128/255
    // is not exact; converting it after interpolation can turn constant
    // alpha128 into127 and incorrectly retain the preceding framebuffer.
    int4 source=(int4)floor(clamp(color,0.0,255.0));
    if((policy_flags&1)!=0) source=(int4)((uint4)source*sample_color(uv)/modulation_denominator);
    source=clamp(source,0,255);
    if((policy_flags&32)!=0) {
        if((policy_flags&64)!=0) clip((float)alpha_cutoff_byte-1.0-(float)source.a);
        else clip((float)source.a-(float)alpha_cutoff_byte);
    }
    if((policy_flags&2)!=0) {
        int3 old=(int3)round(destination.Load(int3((int2)position.xy,0)).rgb*255.0);
        int3 delta=(source.rgb-old)*source.a;
        source.r=old.r+floor_divide(delta.r,blend_denominator);
        source.g=old.g+floor_divide(delta.g,blend_denominator);
        source.b=old.b+floor_divide(delta.b,blend_denominator);
    }
    // The destination alpha is outside this RGB presentation contract.
    return float4((float3)clamp(source.rgb,0,255)/255.0,1.0);
}
float4 ps_encoded(PixelInput input) : SV_Target {
    return shade(input.position,input.color,input.uv);
}
float4 ps_encoded_affine(AffineInput input) : SV_Target {
    return shade(input.position,input.color,input.uv);
}

struct PSInput {
    float4 position : SV_POSITION;
    float4 uv : TEXCOORD0;
};
cbuffer AAConst : register(b1) {
    float4 aa_inv_size;
};
Texture2D rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 main(PSInput input) : SV_TARGET {
    float2 uv = input.uv.xy;
    float2 px = aa_inv_size.xy;
    float3 nw = rsx_tex[0].Sample(rsx_samp[0], uv + float2(-px.x, -px.y)).rgb;
    float3 ne = rsx_tex[0].Sample(rsx_samp[0], uv + float2( px.x, -px.y)).rgb;
    float3 sw = rsx_tex[0].Sample(rsx_samp[0], uv + float2(-px.x,  px.y)).rgb;
    float3 se = rsx_tex[0].Sample(rsx_samp[0], uv + float2( px.x,  px.y)).rgb;
    float3 m  = rsx_tex[0].Sample(rsx_samp[0], uv).rgb;
    float l_nw = luma(nw), l_ne = luma(ne), l_sw = luma(sw), l_se = luma(se);
    float l_m = luma(m);
    float l_min = min(l_m, min(min(l_nw, l_ne), min(l_sw, l_se)));
    float l_max = max(l_m, max(max(l_nw, l_ne), max(l_sw, l_se)));
    float2 dir = float2(-((l_nw + l_ne) - (l_sw + l_se)),
                         (l_nw + l_sw) - (l_ne + l_se));
    float reduce = max((l_nw + l_ne + l_sw + l_se) * 0.03125, 0.0078125);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, -8.0, 8.0) * px;
    float3 a = 0.5 * (rsx_tex[0].Sample(rsx_samp[0], uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                      rsx_tex[0].Sample(rsx_samp[0], uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 b = a * 0.5 + 0.25 * (rsx_tex[0].Sample(rsx_samp[0], uv + dir * -0.5).rgb +
                                 rsx_tex[0].Sample(rsx_samp[0], uv + dir * 0.5).rgb);
    float l_b = luma(b);
    float3 result = (l_b < l_min || l_b > l_max) ? a : b;
    return float4(result, 1.0);
}

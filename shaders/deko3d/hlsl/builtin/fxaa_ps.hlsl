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
float3 tap(float2 uv) { return rsx_tex[0].SampleLevel(rsx_samp[0], uv, 0.0).rgb; }
float4 main(PSInput input) : SV_TARGET {
    float2 uv = input.uv.xy;
    float2 px = aa_inv_size.xy;
    float3 m  = tap(uv);
    float3 nw = tap(uv + float2(-px.x, -px.y));
    float3 ne = tap(uv + float2( px.x, -px.y));
    float3 sw = tap(uv + float2(-px.x,  px.y));
    float3 se = tap(uv + float2( px.x,  px.y));
    float l_nw = luma(nw), l_ne = luma(ne), l_sw = luma(sw), l_se = luma(se);
    float l_m = luma(m);
    float l_min = min(l_m, min(min(l_nw, l_ne), min(l_sw, l_se)));
    float l_max = max(l_m, max(max(l_nw, l_ne), max(l_sw, l_se)));
    /* No edge here (most of the image): keep the pixel and skip the four
     * taps along the edge, as FXAA's own early exit does. Handheld GPU time
     * of this pass was ~3 ms per frame at 1280x720 (run175). */
    if (l_max - l_min < max(0.0312, l_max * 0.125))
        return float4(m, 1.0);
    float2 dir = float2(-((l_nw + l_ne) - (l_sw + l_se)),
                         (l_nw + l_sw) - (l_ne + l_se));
    float reduce = max((l_nw + l_ne + l_sw + l_se) * 0.03125, 0.0078125);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, -8.0, 8.0) * px;
    float3 a = 0.5 * (tap(uv + dir * (1.0 / 3.0 - 0.5)) + tap(uv + dir * (2.0 / 3.0 - 0.5)));
    float3 b = a * 0.5 + 0.25 * (tap(uv + dir * -0.5) + tap(uv + dir * 0.5));
    float l_b = luma(b);
    float3 result = (l_b < l_min || l_b > l_max) ? a : b;
    return float4(result, 1.0);
}

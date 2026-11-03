struct PSInput {
    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;
    float4 fog : FOG;
    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;
    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;
};
Texture2D    rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
cbuffer PSConstants : register(b1) {
    float4 fp_constants[11];
    float4 fp_alpha;
};
struct PSOutput {
    float4 t0 : SV_TARGET0;
    float4 t1 : SV_TARGET1;
};
/* Movie YUV -> RGB (OPENING.SFD planes: t0 Y, t1 U, t2 V). The game always
 * uploads the BT.601 video-range constants below (fp_constants[0..8],
 * run141-146), so they are written in the code; only the fade (colour
 * fp_constants[9], weight fp_constants[10].w) is read. Read from the
 * constant buffer inside the formula, the result came out wrong on the
 * Switch (blue, flashing white), although each input read alone was right. */
PSOutput main(PSInput input) {
    float2 uv = input.tc0.xy;
    float y = (rsx_tex[0].Sample(rsx_samp[0], uv).x - 0.0627451) * 1.16438;
    float u = rsx_tex[1].Sample(rsx_samp[1], uv).x - 0.501961;
    float v = rsx_tex[2].Sample(rsx_samp[2], uv).x - 0.501961;
    float g_part = y - 0.391607 * u;
    float3 rgb = float3(y + 1.59603 * v, g_part - 0.812813 * v, y + 2.01723 * u);
    float4 c = input.col0 * float4(rgb, 1.0);
    float4 o0 = float4((fp_constants[9].xyz - c.xyz) * fp_constants[10].w + c.xyz, c.w);
    float4 o1 = float4(0.0, 0.0, 0.0, g_part);
    PSOutput _out;
    _out.t0 = (o0 == o0) ? o0 : (float4)0;
    _out.t1 = (o1 == o1) ? o1 : (float4)0;
    return _out;
}

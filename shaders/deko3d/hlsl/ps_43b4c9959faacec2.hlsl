struct PSInput {
    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;
    float4 fog : FOG;
    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;
    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;
};
Texture2D    rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
cbuffer PSConstants : register(b1) {
    float4 fp_constants[4];
    float4 fp_alpha;
};
struct PSOutput {
    float4 t0 : SV_TARGET0;
    float4 t1 : SV_TARGET1;
};
PSOutput main(PSInput input) {
    float4 r[48]; float4 h[48];
    [unroll] for (int _i = 0; _i < 48; _i++) { r[_i] = (float4)0; h[_i] = (float4)0; }
    float4 cc0 = (float4)0; float4 cc1 = (float4)0;
    { float4 _v = (float4)(rsx_tex[1].Sample(rsx_samp[1], ((input.tc0).xyzw).xy)); r[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[0]).xxxx) * ((fp_constants[0]).xxxx)); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[0]).xyzw) + ((fp_constants[1]).yyyy)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((r[0]).zzzz)); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((r[0]).wwww)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(dot(((r[0]).xyzw).xy, ((r[0]).xyzw).xy)); r[1].x = _v.x; }
    { float4 _v = (float4)((input.tc0).xxxy); r[0].zw = _v.zw; }
    { float4 _v = (float4)(((r[0]).xyzw) + ((r[0]).xxxy)); r[1].zw = _v.zw; }
    { float4 _v = (float4)((abs((r[0]).xyzw) > 0.0 ? ((r[0]).xyzw) / sqrt(abs(((r[1]).xyzw).x)) : (float4)0.0)); r[1].xy = _v.xy; }
    { float4 _v = (float4)(((r[1]).xyzw) * ((fp_constants[2]).yzzw)); r[1].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[1].Sample(rsx_samp[1], ((r[1]).zwzz).xy)); r[2].z = _v.z; }
    { float4 _v = (float4)(((r[1]).xyzw) + ((r[1]).xxxy)); r[1].zw = _v.zw; }
    { float4 _v = (float4)(((r[0]).zwzz) + ((r[0]).xyzw)) / 2.0; r[2].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[1].Sample(rsx_samp[1], ((r[1]).zwzz).xy)); r[1].z = _v.z; }
    { float4 _v = (float4)(min(((r[2]).zzzz), ((r[1]).zzzz))); r[1].w = _v.w; }
    { float4 _v = (float4)(((r[1]).xyzw) + ((r[2]).xyzw)) * 2.0; r[1].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[1].Sample(rsx_samp[1], ((r[1]).xyzw).xy)); r[1].z = _v.z; }
    { float4 _v = (float4)(min(((r[1]).wwww), ((r[1]).zzzz))); r[1].x = _v.x; }
    { float4 _v = (float4)((float4)(((r[1]).xyzw) >= ((fp_constants[3]).wwww))); h[2].x = _v.x; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((h[2]).xxxx) + ((r[0]).zwzz)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).xyzw).xy)); r[0].xyzw = _v.xyzw; }
    PSOutput _out;
    float4 _o0 = r[0];
    _out.t0 = (_o0 == _o0) ? _o0 : (float4)0;
    float4 _o1 = r[2];
    _out.t1 = (_o1 == _o1) ? _o1 : (float4)0;
    return _out;
}

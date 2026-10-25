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
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy)); h[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((input.col0).xyzw) * ((h[0]).xyzw)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((fp_constants[0]).xxxx) + (-((input.fog).yyyy))); r[1].x = _v.x; }
    { float4 _v = (float4)((r[0]).xyzw); r[0].w = _v.w; }
    { float4 _v = (float4)(min(((r[1]).xxxx), ((fp_constants[1]).yyyy))); r[1].w = _v.w; }
    { float4 _v = (float4)((-((r[0]).xyzw)) + ((fp_constants[2]).xyzw)); r[1].xyz = _v.xyz; }
    { float4 _v = (float4)(max(((r[1]).wwww), ((fp_constants[3]).xxxx))); r[2].x = _v.x; }
    { float4 _v = (float4)(((r[2]).xxxx) * ((r[1]).xyzw) + ((r[0]).xyzw)); r[0].xyz = _v.xyz; }
    PSOutput _out;
    float4 _o0 = r[0];
    _out.t0 = (_o0 == _o0) ? _o0 : (float4)0;
    float4 _o1 = r[2];
    _out.t1 = (_o1 == _o1) ? _o1 : (float4)0;
    return _out;
}

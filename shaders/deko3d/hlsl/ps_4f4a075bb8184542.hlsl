struct PSInput {
    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;
    float4 fog : FOG;
    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;
    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;
};
Texture2D    rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
cbuffer PSConstants : register(b1) {
    float4 fp_constants[24];
    float4 fp_alpha;
};
struct PSOutput {
    float4 t0 : SV_TARGET0;
    float4 t1 : SV_TARGET1;
    float4 t2 : SV_TARGET2;
};
PSOutput main(PSInput input) {
    float4 r[48]; float4 h[48];
    [unroll] for (int _i = 0; _i < 48; _i++) { r[_i] = (float4)0; h[_i] = (float4)0; }
    float4 cc0 = (float4)0; float4 cc1 = (float4)0;
    { float4 _v = (float4)((input.tc0).xyzw); r[1].xy = _v.xy; }
    { float4 _v = (float4)((-((r[1]).xyzw)) + ((fp_constants[0]).xyxx)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(dot(((r[0]).xyzw).xy, ((r[0]).xyzw).xy)); r[0].z = _v.z; }
    { float4 _v = (float4)((abs(abs((r[0]).zzzz)) > 0.0 ? (abs((r[0]).zzzz)) / sqrt(abs(((r[0]).zzzz).x)) : (float4)0.0)); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[0]).xyzw) + (-((fp_constants[1]).zzzz))); r[0].w = _v.w; }
    { float4 _v = (float4)(max(((r[0]).wwww), ((fp_constants[2]).xxxx))); r[3].x = _v.x; }
    { float4 _v = (float4)(rsqrt(abs(((r[0]).zzzz).x))); r[0].z = _v.z; }
    { float4 _v = (float4)(((r[0]).zzzz) * ((r[0]).xyzw)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(((r[3]).xxxx) * ((fp_constants[3]).xyzw)); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[4]).xxxx)); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((r[3]).xxxx)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(((r[0]).xxxy) * ((r[0]).wwww)); r[1].zw = _v.zw; }
    { float4 _v = (float4)(((r[1]).xyzw) + ((r[1]).zwzz)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).xyzw).xy)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[5]).xxxx)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[2]).xyzw) * ((fp_constants[6]).xxxx) + ((r[0]).xyzw)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)((r[1]).zwzz) * 2.0; r[0].xy = _v.xy; }
    { float4 _v = (float4)(((r[1]).xyzw) + ((r[0]).xyzw)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).xyzw).xy)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[7]).xxxx) + ((r[2]).xyzw)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[1]).zwzz) * ((fp_constants[8]).xxxx) + ((r[1]).xyzw)); r[2].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).xyzw).xy)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[2]).xyzw) * ((fp_constants[9]).xxxx) + ((r[0]).xyzw)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)((r[1]).xyzw) * 4.0; r[3].zw = _v.zw; }
    { float4 _v = (float4)(((r[1]).xyzw) + ((r[3]).zwzz)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(((r[1]).xyzw) * ((fp_constants[10]).xxxx) + ((r[1]).xxxy)); r[3].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).xyzw).xy)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[11]).xxxx) + ((r[2]).xyzw)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[1]).zwzz) * ((fp_constants[12]).xxxx) + ((r[1]).xyzw)); r[2].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).xyzw).xy)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[2]).xyzw) * ((fp_constants[13]).xxxx) + ((r[0]).xyzw)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).zwzz).xy)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[2]).xyzw) * ((fp_constants[14]).xxxx) + ((r[0]).xyzw)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)((r[1]).zwzz) * 8.0; r[2].xy = _v.xy; }
    { float4 _v = (float4)(((r[1]).xyzw) * ((fp_constants[15]).xxxx) + ((r[1]).xxxy)); r[3].zw = _v.zw; }
    { float4 _v = (float4)(((r[1]).xyzw) + ((r[2]).xyzw)); r[3].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).zwzz).xy)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[2]).xyzw) * ((fp_constants[16]).xxxx) + ((r[0]).xyzw)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).xyzw).xy)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[2]).xyzw) * ((fp_constants[17]).xxxx) + ((r[0]).xyzw)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[1]).xyzw) * ((fp_constants[18]).xxxx) + ((r[1]).xxxy)); r[0].zw = _v.zw; }
    { float4 _v = (float4)(((r[1]).zwzz) * ((fp_constants[19]).yyyy) + ((r[1]).xyzw)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(((r[1]).zwzz) * ((fp_constants[20]).xxxx) + ((r[1]).xyzw)); r[1].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[1]).xyzw).xy)); r[1].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[1]).xyzw) * ((fp_constants[21]).xxxx) + ((r[2]).xyzw)); r[1].xyzw = _v.xyzw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).xyzw).xy)); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[2]).xyzw) * ((fp_constants[22]).xxxx) + ((r[1]).xyzw)); r[1].xyzw = _v.xyzw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).zwzz).xy)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[23]).xxxx) + ((r[1]).xyzw)); r[0].xyzw = _v.xyzw; }
    PSOutput _out;
    float4 _o0 = r[0];
    _out.t0 = (_o0 == _o0) ? _o0 : (float4)0;
    float4 _o1 = r[2];
    _out.t1 = (_o1 == _o1) ? _o1 : (float4)0;
    float4 _o2 = r[3];
    _out.t2 = (_o2 == _o2) ? _o2 : (float4)0;
    return _out;
}

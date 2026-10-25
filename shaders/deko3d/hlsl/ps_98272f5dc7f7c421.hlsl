struct PSInput {
    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;
    float4 fog : FOG;
    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;
    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;
};
Texture2D    rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
cbuffer PSConstants : register(b1) {
    float4 fp_constants[27];
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
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[0]).xxxy)); r[0].zw = _v.zw; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[1]).xxxy)); r[1].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).zwzz).xy)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xyzw) + ((fp_constants[2]).xyxx)); r[0].xy = _v.xy; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[3]).xxxy)); r[2].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).xyzw).xy)); h[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[1]).xyzw) * ((fp_constants[4]).zzzz)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)((fp_constants[5]).xxxx); h[0].w = _v.w; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[6]).xxxy)); r[3].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[1]).zwzz).xy)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[3]).xyzw) * ((fp_constants[7]).zzzz) + ((h[1]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).zwzz).xy)); h[6].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[6]).xyzw) * ((fp_constants[8]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[9]).xxxy)); r[2].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).zwzz).xy)); h[4].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[4]).xyzw) * ((fp_constants[10]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[11]).xxxy)); r[3].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).zwzz).xy)); h[6].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[6]).xyzw) * ((fp_constants[12]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[13]).xxxy)); r[2].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).zwzz).xy)); h[4].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[4]).xyzw) * ((fp_constants[14]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[15]).xxxy)); r[3].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).zwzz).xy)); h[6].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[6]).xyzw) * ((fp_constants[16]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[17]).xxxy)); r[2].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).zwzz).xy)); h[4].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[4]).xyzw) * ((fp_constants[18]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[19]).xxxy)); r[3].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).zwzz).xy)); h[6].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[6]).xyzw) * ((fp_constants[20]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[21]).xxxy)); r[2].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).zwzz).xy)); h[4].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[4]).xyzw) * ((fp_constants[22]).zzzz) + ((h[3]).xyzw)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[1]).xxxy) + ((fp_constants[23]).xxxy)); r[3].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).zwzz).xy)); h[2].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[2]).xyzw) * ((fp_constants[24]).zzzz) + ((h[3]).xyzw)); h[2].xyz = _v.xyz; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[3]).zwzz).xy)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[3]).xyzw) * ((fp_constants[25]).zzzz) + ((h[2]).xyzw)); h[2].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[0]).xyzw) * ((fp_constants[26]).zzzz) + ((h[2]).xyzw)); h[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[1]).xyzw) + ((h[0]).xyzw)); h[0].xyz = _v.xyz; }
    PSOutput _out;
    float4 _o0 = h[0];
    _out.t0 = (_o0 == _o0) ? _o0 : (float4)0;
    float4 _o1 = h[4];
    _out.t1 = (_o1 == _o1) ? _o1 : (float4)0;
    float4 _o2 = h[6];
    _out.t2 = (_o2 == _o2) ? _o2 : (float4)0;
    return _out;
}

struct PSInput {
    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;
    float4 fog : FOG;
    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;
    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;
};
Texture2D    rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
cbuffer PSConstants : register(b1) {
    float4 fp_constants[22];
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
    { float4 _v = (float4)(rsx_tex[1].Sample(rsx_samp[1], ((input.tc0).xyzw).xy)); r[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[0]).zzzz) + ((fp_constants[0]).wwww)); r[1].xyz = _v.xyz; }
    { float4 _v = (float4)(((fp_constants[1]).yyyy) + (-((fp_constants[1]).xxxx))); r[1].w = _v.w; }
    { float4 _v = (float4)(((fp_constants[2]).yyyy) + ((fp_constants[2]).xxxx)); r[0].w = _v.w; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy)); h[4].xyz = _v.xyz; }
    { float4 _v = (float4)(floor((r[1]).xyzw)); r[0].xyz = _v.xyz; }
    { float4 _v = (float4)(dot(((r[0]).xyzw).xyz, ((fp_constants[3]).xyzw).xyz)); r[0].x = _v.x; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((r[1]).wwww) + (-((r[0]).wwww))); r[0].x = _v.x; }
    { float4 _v = (float4)(((fp_constants[4]).yyyy) * ((fp_constants[4]).xxxx)); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[0]).xyzw) / ((r[0]).xyzw).x) * 2.0; r[0].w = _v.w; }
    { float4 _v = (float4)((fp_constants[5]).xxxx); h[0].x = _v.x; }
    { float4 _v = (float4)((fp_constants[6]).xxxx); h[0].yw = _v.yw; }
    { float4 _v = (float4)((-((r[0]).wwww)) + (-((fp_constants[7]).zzzz))); r[3].x = _v.x; }
    { float4 _v = (float4)((input.tc0).xxxy); r[3].zw = _v.zw; }
    { float4 _v = (float4)(max(((r[3]).xxxx), ((fp_constants[8]).yyyy))); r[0].w = _v.w; }
    { float4 _v = (float4)((fp_constants[9]).xxxx); h[6].y = _v.y; }
    { float4 _v = (float4)(((r[0]).xyzw) / ((fp_constants[10]).wwww).x); _v = saturate(_v); h[6].w = _v.w; }
    { float4 _v = (float4)((fp_constants[11]).xxxx); h[6].xz = _v.xz; }
    { float4 _v = (float4)(((h[6]).wwww) * ((h[6]).xyzw) + ((r[3]).zwzz)); r[1].xy = _v.xy; }
    { float4 _v = (float4)(-((fp_constants[12]).xxxx)); h[0].z = _v.z; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[1]).xyzw).xy)); h[3].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[0]).xxxy) * ((h[6]).wwww) + ((r[3]).xyzw)); r[0].zw = _v.zw; }
    { float4 _v = (float4)(((h[0]).xyzw) * ((h[6]).wwww) + ((r[3]).xyzw)); r[2].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).zwzz).xy)); h[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[4]).xyzw) + ((h[0]).xyzw)); h[4].xyz = _v.xyz; }
    { float4 _v = (float4)(-((fp_constants[13]).xxxx)); h[0].w = _v.w; }
    { float4 _v = (float4)((h[6]).xyzw); h[0].z = _v.z; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).zwzz).xy)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[4]).xyzw) + ((h[1]).xyzw)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)((fp_constants[14]).xxxx); h[2].xz = _v.xz; }
    { float4 _v = (float4)((fp_constants[15]).xxxx); h[4].yw = _v.yw; }
    { float4 _v = (float4)(((h[1]).xyzw) + ((h[3]).xyzw)); h[5].xyz = _v.xyz; }
    { float4 _v = (float4)((fp_constants[16]).xxxx); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[0]).wwww) * ((fp_constants[17]).xxxx)); h[4].x = _v.x; }
    { float4 _v = (float4)(((h[6]).wwww) * ((h[0]).xyzw) + ((r[3]).xyzw)); r[0].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[0]).zwzz).xy)); h[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[5]).xyzw) + ((h[0]).xyzw)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)((fp_constants[18]).xxxx); r[2].w = _v.w; }
    { float4 _v = (float4)(((r[2]).wwww) * ((fp_constants[19]).xxxx)); h[2].y = _v.y; }
    { float4 _v = (float4)(((h[6]).wwww) * ((h[4]).xxxy) + ((r[3]).xyzw)); r[2].zw = _v.zw; }
    { float4 _v = (float4)(-((h[4]).xxxx)); h[4].z = _v.z; }
    { float4 _v = (float4)(((h[6]).wwww) * ((h[4]).xyzw) + ((r[3]).xyzw)); r[1].zw = _v.zw; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).zwzz).xy)); h[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[1]).xyzw) + ((h[0]).xyzw)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)(-((h[2]).yyyy)); h[2].w = _v.w; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[1]).zwzz).xy)); h[5].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[6]).wwww) * ((h[2]).xyzw) + ((r[3]).xyzw)); r[1].zw = _v.zw; }
    { float4 _v = (float4)(((h[6]).wwww) * ((h[2]).xyzw) + ((r[3]).zwzz)); r[2].xy = _v.xy; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[1]).zwzz).xy)); h[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[1]).xyzw) + ((h[5]).xyzw)); h[2].xyz = _v.xyz; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((r[2]).xyzw).xy)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[2]).xyzw) + ((h[1]).xyzw)); h[1].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[1]).xyzw) + ((h[0]).xyzw)); h[0].xyz = _v.xyz; }
    { float4 _v = (float4)((fp_constants[20]).xxxx); r[0].w = _v.w; }
    { float4 _v = (float4)(((h[0]).xyzw) * ((fp_constants[21]).xxxx)); r[0].xyz = _v.xyz; }
    PSOutput _out;
    float4 _o0 = r[0];
    _out.t0 = (_o0 == _o0) ? _o0 : (float4)0;
    float4 _o1 = r[2];
    _out.t1 = (_o1 == _o1) ? _o1 : (float4)0;
    float4 _o2 = r[3];
    _out.t2 = (_o2 == _o2) ? _o2 : (float4)0;
    return _out;
}

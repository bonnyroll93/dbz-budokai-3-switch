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
PSOutput main(PSInput input) {
    float4 r[48]; float4 h[48];
    [unroll] for (int _i = 0; _i < 48; _i++) { r[_i] = (float4)0; h[_i] = (float4)0; }
    float4 cc0 = (float4)0; float4 cc1 = (float4)0;
    { float4 _v = (float4)(rsx_tex[1].Sample(rsx_samp[1], ((input.tc0).xyzw).xy)); r[0].x = _v.x; }
    { float4 _v = (float4)((input.col0).xyzw); h[1].xyzw = _v.xyzw; }
    { float4 _v = (float4)(((r[0]).xxxx) + ((fp_constants[0]).xxxx)); r[0].y = _v.y; }
    { float4 _v = (float4)((fp_constants[1]).yyyy); r[1].w = _v.w; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy)); r[0].x = _v.x; }
    { float4 _v = (float4)(((r[0]).xyzw) + ((fp_constants[2]).xxxx)); r[1].x = _v.x; }
    { float4 _v = (float4)(((r[1]).xxxx) * ((fp_constants[3]).xxxx)); r[1].z = _v.z; }
    { float4 _v = (float4)(rsx_tex[2].Sample(rsx_samp[2], ((input.tc0).xyzw).xy)); r[0].x = _v.x; }
    { float4 _v = (float4)(((r[0]).xyzw) + ((fp_constants[4]).xxxx)); r[0].x = _v.x; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[5]).xxxx) + ((r[1]).zzzz)); r[1].x = _v.x; }
    { float4 _v = (float4)((-((r[0]).yyyy)) * ((fp_constants[6]).yyyy) + ((r[1]).zzzz)); r[2].w = _v.w; }
    { float4 _v = (float4)((-((r[0]).xxxx)) * ((fp_constants[7]).xxxx) + ((r[2]).wwww)); r[1].y = _v.y; }
    { float4 _v = (float4)(((r[0]).yyyy) * ((fp_constants[8]).yyyy) + ((r[1]).xyzw)); r[1].z = _v.z; }
    { float4 _v = (float4)(((h[1]).xyzw) * ((r[1]).xyzw)); r[0].xyzw = _v.xyzw; }
    { float4 _v = (float4)((-((r[0]).xyzw)) + ((fp_constants[9]).xyzw)); r[1].xyz = _v.xyz; }
    { float4 _v = (float4)((r[0]).xyzw); r[0].w = _v.w; }
    { float4 _v = (float4)(((r[1]).xyzw) * ((fp_constants[10]).wwww) + ((r[0]).xyzw)); r[0].xyz = _v.xyz; }
    PSOutput _out;
    float4 _o0 = r[0];
    _out.t0 = (_o0 == _o0) ? _o0 : (float4)0;
    float4 _o1 = r[2];
    _out.t1 = (_o1 == _o1) ? _o1 : (float4)0;
    return _out;
}

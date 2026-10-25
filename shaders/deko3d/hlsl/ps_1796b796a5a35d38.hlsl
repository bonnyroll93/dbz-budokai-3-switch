struct PSInput {
    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;
    float4 fog : FOG;
    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;
    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;
};
Texture2D    rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
cbuffer PSConstants : register(b1) {
    float4 fp_constants[3];
    float4 fp_alpha;
};
float4 main(PSInput input) : SV_TARGET {
    float4 r[48]; float4 h[48];
    [unroll] for (int _i = 0; _i < 48; _i++) { r[_i] = (float4)0; h[_i] = (float4)0; }
    float4 cc0 = (float4)0; float4 cc1 = (float4)0;
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy)); h[1].xyzw = _v.xyzw; }
    { float4 _v = (float4)(log2(((h[1]).xyzw).x)); r[0].x = _v.x; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[0]).xxxx)); r[0].x = _v.x; }
    { float4 _v = (float4)(log2(((h[1]).yyyy).x)); r[0].y = _v.y; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[1]).yyyy)); r[0].y = _v.y; }
    { float4 _v = (float4)(log2(((h[1]).zzzz).x)); r[0].z = _v.z; }
    { float4 _v = (float4)(((r[0]).xyzw) * ((fp_constants[2]).xyzw)); r[0].z = _v.z; }
    { float4 _v = (float4)(exp2(((r[0]).zzzz).x)); r[0].z = _v.z; }
    { float4 _v = (float4)((h[1]).xyzw); r[0].w = _v.w; }
    { float4 _v = (float4)(exp2(((r[0]).yyyy).x)); r[0].y = _v.y; }
    { float4 _v = (float4)(exp2(((r[0]).xyzw).x)); r[0].x = _v.x; }
    float4 _o = r[0];
    return (_o == _o) ? _o : (float4)0;
}

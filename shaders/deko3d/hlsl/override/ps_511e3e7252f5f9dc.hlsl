struct PSInput {
    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;
    float4 fog : FOG;
    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;
    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;
};
Texture2D    rsx_tex[16] : register(t0);
SamplerState rsx_samp[16] : register(s0);
cbuffer PSConstants : register(b1) {
    float4 fp_constants[10];
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
    { float4 _v = (float4)(((input.tc1).xyzw) + (-((fp_constants[0]).xyzw))); r[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)(dot(((r[2]).xyzw), ((r[2]).xyzw))); r[0].w = _v.w; }
    { float4 _v = (float4)((input.tc2).xyzw); r[1].xyzw = _v.xyzw; }
    { float4 _v = (float4)(dot(((r[1]).xyzw).xyz, ((r[1]).xyzw).xyz)); r[0].x = _v.x; }
    { float4 _v = (float4)((abs((r[1]).xyzw) > 0.0 ? ((r[1]).xyzw) / sqrt(abs(((r[0]).xyzw).x)) : (float4)0.0)); r[0].xyz = _v.xyz; }
    { float4 _v = (float4)(dot(((r[0]).xyzw).xyz, ((fp_constants[1]).xyzw).xyz)) / 2.0; r[1].x = _v.x; }
    { float4 _v = (float4)((abs((r[2]).xyzw) > 0.0 ? ((r[2]).xyzw) / sqrt(abs(((r[0]).wwww).x)) : (float4)0.0)); r[2].xyz = _v.xyz; }
    { float4 _v = (float4)((fp_constants[2]).xxxx); r[0].w = _v.w; }
    { float4 _v = (float4)(dot(((r[0]).xyzw).xyz, ((r[2]).xyzw).xyz)); r[0].x = _v.x; }
    float t28_ndv = r[0].x;
    { float4 _v = (float4)((fp_constants[3]).xxxx); r[2].w = _v.w; }
    { float4 _v = (float4)((-(abs((r[0]).xyzw))) + ((fp_constants[4]).xxxx)); r[0].x = _v.x; }
    { float4 _v = (float4)(((r[1]).xxxx) + ((fp_constants[5]).yyyy)); r[2].z = _v.z; }
    { float4 _v = (float4)(((r[0]).xxxx) * ((r[0]).xxxx)); r[2].y = _v.y; }
    { float4 _v = (float4)(log2(((r[0]).xyzw).x)); r[2].x = _v.x; }
    { float4 _v = (float4)(((r[1]).wwww) * ((r[2]).yyyy)) / 2.0; r[1].z = _v.z; }
    { float4 _v = (float4)(rsx_tex[0].Sample(rsx_samp[0], ((input.tc0).xyzw).xy)); h[2].xyzw = _v.xyzw; }
    { float4 _v = (float4)((float4)(((fp_constants[6]).xxxx) <  ((h[2]).xyzw)));
      cc0.w = _v.w;
    }
    { float4 _v = (float4)((h[2]).xyzw); r[0].xyz = _v.xyz; }
    { float4 _v = (float4)(((r[2]).xxxx) * ((fp_constants[7]).xxxx)); r[3].w = _v.w; }
    { float4 _v = (float4)(rsx_tex[1].Sample(rsx_samp[1], ((r[2]).zwzz).xy)); h[4].xyz = _v.xyz; }
    { float4 _v = (float4)(((h[2]).xyzw) + (-((h[4]).xyzw))); r[2].xyz = _v.xyz; }
    { float4 _v = (float4)(exp2(((r[3]).wwww).x)); r[2].w = _v.w; }
    { float4 _v = (float4)((float4)(((r[2]).wwww) >= ((fp_constants[8]).xxxx))); h[2].x = _v.x; }
    { float4 _v = (float4)((fp_constants[9]).yyyy); r[2].w = _v.w; }
    { float4 _v = (float4)((r[0]).xyzw);
      float4 _p = (float4)((cc0).wwww != (float4)0.0);
      r[2].xyzw = lerp(r[2].xyzw, _v.xyzw, _p.xyzw);
    }
    { float4 _v = (float4)(((r[1]).wwww) * ((h[2]).xyzw) + ((r[1]).zzzz)); r[0].x = _v.x; }
    /* T28 (V1): the band-1 variant of the T28 band test, now on the whole
     * screen. The band limits sit off screen on purpose: the original text
     * renders white skin on the Switch, this structure renders it right
     * (suspected shader-compiler issue, under investigation). */
    if (input.position.x >= 1.0e9) r[0].x = 0.0;
    { float4 _v = (float4)(((r[2]).xyzw) + ((r[0]).xxxx)); r[0].xyzw = _v.xyzw; }
    if (input.position.x >= 2.0e9 && input.position.x < 3.0e9)
        r[0] = float4(abs(t28_ndv).xxx, 1.0);
    else if (input.position.x >= 4.0e9 && input.position.x < 5.0e9)
        r[0] = float4(saturate(abs(input.tc1.w - 1.0) * 4.0), saturate(input.tc2.w), 0.0, 1.0);
    PSOutput _out;
    float4 _o0 = r[0];
    _out.t0 = (_o0 == _o0) ? _o0 : (float4)0;
    float4 _o1 = r[2];
    _out.t1 = (_o1 == _o1) ? _o1 : (float4)0;
    float4 _o2 = r[3];
    _out.t2 = (_o2 == _o2) ? _o2 : (float4)0;
    return _out;
}

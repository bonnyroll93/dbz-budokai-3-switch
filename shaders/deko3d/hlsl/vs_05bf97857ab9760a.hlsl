struct VSInput {
    float4 a0:ATTR0;
    float4 a1:ATTR1;
    float4 a2:ATTR2;
    float4 a7:ATTR7;
};
struct VSOutput {
    float4 pos:SV_Position; float4 col0:COLOR0; float4 col1:COLOR1;
    float4 fog:FOG;
    float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;
    float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7;
};
cbuffer VPConst : register(b0) {
    float4 vp_c[512];
    float4 vp_posscale;
    float4 vp_posoffset;
};
float vp_mul1(float a, float b) { return (a == 0.0 || b == 0.0) ? 0.0 : a * b; }
float4 vp_mul(float4 a, float4 b) { return float4(vp_mul1(a.x, b.x), vp_mul1(a.y, b.y), vp_mul1(a.z, b.z), vp_mul1(a.w, b.w)); }
float vp_dot3(float4 a, float4 b) { float4 m = vp_mul(a, b); return m.x + m.y + m.z; }
float vp_dot4(float4 a, float4 b) { float4 m = vp_mul(a, b); return m.x + m.y + m.z + m.w; }
VSOutput main(VSInput input) {
    float4 v[16];
    [unroll] for (int _k=0;_k<16;_k++) v[_k]=float4(0,0,0,1);
    v[0]=input.a0;
    v[1]=input.a1;
    v[2]=input.a2;
    v[7]=input.a7;
    float4 r[32]; float4 o[16];
    [unroll] for (int _i=0;_i<32;_i++) r[_i]=(float4)0;
    [unroll] for (int _j=0;_j<16;_j++) o[_j]=float4(0,0,0,1);
    int4 a0 = (int4)0; int4 a1 = (int4)0;
    float4 cc[2]; cc[0] = (float4)0; cc[1] = (float4)0;
    int _pc = 0; int _cs[8]; int _csp = 0;
    [loop] for (int _it = 0; _it < 16384 && _pc >= 0; _it++) {
    if (_pc == 0) {
    { float4 _v = (float4)((vp_c[303]).xxxx);
        cc[0].x = _v.x;
    }
    /* FLOW BRI @1 -> 83 cond=2 test=0 swz=xxxx */
        _pc = (cc[0].x == 0.0) ? 83 : 2;
    }
    if (_pc == 2) {
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[13].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).zzzz);
        r[11].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).yyyy);
        r[12].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).yyyy);
        r[10].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).zzzz);
        r[9].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).yyyy);
        r[8].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).yyyy);
        r[7].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).zzzz);
        r[6].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).zzzz);
        r[5].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).wwww);
        r[4].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[3].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[2].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).wwww);
        r[14].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[466]).wwww);
        r[1].y = _v.y;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        r[0].y = _v.y;
    }
    { float4 _v = (float4)(vp_mul(((v[7]).xxxx),((vp_c[467]).xxxx)));
        r[0].x = _v.x;
    }
    { float4 _v = (float4)(((vp_c[467]).wwwy)+((r[0]).xxxx));
        r[0].zw = _v.zw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).yyyy),((vp_c[303]).xxxx)));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)((vp_c[466]).wwww);
        r[0].y = _v.y;
    }
    { float4 _v = (float4)((1.0/((r[1]).xxxx).x));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)(((vp_c[466]).xxxx)+((r[0]).xxxx));
        r[1].z = _v.z;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).wwwz),((r[1]).xxxx)));
        r[0].zw = _v.zw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xxxx),((r[1]).zzzz)));
        r[14].x = _v.x;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xxxx),((r[1]).xxxx)));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[7].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[5].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[4].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[2].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).wwww);
        r[8].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).wwww);
        r[6].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).wwww);
        r[3].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).wwww);
        r[0].x = _v.x;
    }
    { float4 _v = (float4)((float4)0);
        r[0].z = _v.z;
    }
    { float4 _v = (float4)((r[1]).xxxx);
        r[13].x = _v.x;
    }
    { float4 _v = (float4)((r[1]).xxxx);
        r[11].x = _v.x;
    }
    { float4 _v = (float4)((r[1]).xxxx);
        r[12].x = _v.x;
    }
    { float4 _v = (float4)((r[14]).xxxx);
        r[10].x = _v.x;
    }
    { float4 _v = (float4)((r[14]).xxxx);
        r[9].x = _v.x;
    }
    { float4 _v = (float4)((float4)0);
        r[14].z = _v.z;
    }
    { float4 _v = (float4)((float4)0);
        r[1].z = _v.z;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[0].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[15].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[8].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[7].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[6].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[5].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[4].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[3].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[2].z = _v.z;
    }
    { float4 _v = (float4)((r[14]).zzzz);
        r[10].z = _v.z;
    }
    { float4 _v = (float4)((r[14]).zzzz);
        r[9].z = _v.z;
    }
    { float4 _v = (float4)((r[1]).zzzz);
        r[11].z = _v.z;
    }
    { float4 _v = (float4)((r[1]).zzzz);
        r[12].z = _v.z;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[2].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[3].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[4].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[5].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[6].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[7].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[8].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[9].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[10].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[14].x = _v.x;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[12].x = _v.x;
    }
    { float4 _v = (float4)((r[1]).zzzz);
        r[13].z = _v.z;
    }
    { float4 _v = (float4)(float4(0,0,0,0));
        r[11].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).xxxx);
        r[15].y = _v.y;
    }
    { float4 _v = (float4)((r[1]).xxxx);
        r[15].w = _v.w;
    }
    { float4 _v = (float4)((r[2]).xxxx);
        r[11].z = _v.z;
    }
    { float4 _v = (float4)((r[3]).xxxx);
        r[11].y = _v.y;
    }
    { float4 _v = (float4)((r[4]).xxxx);
        r[15].z = _v.z;
    }
    { float4 _v = (float4)((r[5]).xxxx);
        r[12].z = _v.z;
    }
    { float4 _v = (float4)((r[6]).xxxx);
        r[12].y = _v.y;
    }
    { float4 _v = (float4)((r[7]).xxxx);
        r[14].z = _v.z;
    }
    { float4 _v = (float4)((r[8]).xxxx);
        r[14].y = _v.y;
    }
    { float4 _v = (float4)((r[9]).xxxx);
        r[12].w = _v.w;
    }
    { float4 _v = (float4)((r[10]).xxxx);
        r[14].w = _v.w;
    }
    { float4 _v = (float4)((r[14]).xyzw);
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((r[12]).xyzw);
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((r[15]).xyzw);
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((r[11]).xyzx);
        r[5].xyz = _v.xyz;
    }
    /* FLOW BRI @82 -> 88 cond=7 test=0 swz=xyzw */
        _pc = true ? 88 : 83;
    }
    if (_pc == 83) {
    { int4 _a = (int4)floor((v[7]).xxxx); a0.x = _a.x; }
    { float4 _v = (float4)((vp_c[(88u + a0.x) & 511u]).xyzw);
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[(156u + a0.x) & 511u]).xyzw);
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[(224u + a0.x) & 511u]).xyzw);
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[(20u + a0.x) & 511u]).xyzx);
        r[5].xyz = _v.xyz;
    }
        _pc = 88;
    }
    if (_pc == 88) {
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((r[1]).xyzw)));
        r[7].z = _v.z;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        o[8].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        o[9].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        o[10].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        o[11].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_dot4(((r[2]).xyzw),((r[2]).xyzw)));
        r[7].x = _v.x;
    }
    { float4 _v = (float4)((r[1]).xyzw);
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((r[1]).xyzw);
        r[4].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[293]).xyzx);
        o[1].xyz = _v.xyz;
    }
    { float4 _v = (float4)((float4)(((v[2]).xyzx)!=((vp_c[467]).zzzz)));
        r[6].xyz = _v.xyz;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        o[7].xy = _v.xy;
    }
    { float4 _v = (float4)((vp_c[467]).zwzz);
        r[8].xy = _v.xy;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[5].w = _v.w;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[10].w = _v.w;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[9].w = _v.w;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[7].w = _v.w;
    }
    { float4 _v = (float4)(((vp_c[467]).wwww)+(-(v[1]).xxxx));
        r[8].w = _v.w;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[6].w = _v.w;
    }
    { float4 _v = (float4)((float4)(((v[1]).xxxx)!=((vp_c[467]).wwww)));
        r[7].y = _v.y;
    }
    { float4 _v = (float4)(((r[6]).xxxx)+((r[6]).yyyy)); _v = saturate(_v);
        r[6].x = _v.x;
    }
    { float4 _v = (float4)((r[7]).yyyy);
        cc[0].z = _v.z;
    }
    { float4 _v = (float4)((float4)(((r[7]).zzzz)<((vp_c[467]).zzzz)));
        r[7].z = _v.z;
    }
    { float4 _v = (float4)((float4)(((vp_c[467]).zzzz)<((r[7]).xxxx)));
        cc[0].y = _v.y;
    }
    { float4 _v = (float4)((1.0/((r[7]).xxxx).x));
        r[6].y = _v.y;
    }
    { float4 _v = (float4)(vp_mul(((r[7]).yyyy),((r[7]).zzzz)));
        cc[0].x = _v.x;
    }
    { float4 _v = (float4)(((r[6]).zzzz)+((r[6]).xxxx)); _v = saturate(_v);
        r[8].z = _v.z;
    }
    { float4 _v = (float4)(vp_mul(((r[6]).yyyy),((vp_c[467]).yyyy)));
        if (cc[0].y != 0.0) r[6].w = _v.w;
    }
    { float4 _v = (float4)(-(r[1]).xyzw);
        if (cc[0].x != 0.0) r[4].x = _v.x;
        if (cc[0].x != 0.0) r[4].y = _v.y;
        if (cc[0].x != 0.0) r[4].z = _v.z;
        if (cc[0].x != 0.0) r[4].w = _v.w;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzx),((r[6]).wwww)));
        r[7].xyz = _v.xyz;
    }
    { float4 _v = (float4)(vp_mul(((r[7]).xyzz),((r[2]).xyzx)));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[7]).xyzz),((r[2]).wwwy)));
        r[6].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[7]).yzyy),((r[2]).xyxx)));
        r[7].xy = _v.xy;
    }
    { float4 _v = (float4)(vp_mul(((v[1]).xxxx),((r[4]).xyzw)));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[8]).wwww),((r[0]).xyzw))+((r[2]).xyzw));
        if (cc[0].z != 0.0) r[3].x = _v.x;
        if (cc[0].z != 0.0) r[3].y = _v.y;
        if (cc[0].z != 0.0) r[3].z = _v.z;
        if (cc[0].z != 0.0) r[3].w = _v.w;
    }
    { float4 _v = (float4)(((r[1]).wwww)+((r[6]).yyyy));
        r[4].w = _v.w;
    }
    { float4 _v = (float4)(((r[1]).wwww)+(-(r[6]).yyyy));
        r[8].w = _v.w;
    }
    { float4 _v = (float4)((-(r[6]).zxzz)+((r[7]).xyxx));
        r[11].xy = _v.xy;
    }
    { float4 _v = (float4)(((r[6]).zzzz)+((r[7]).xxxx));
        r[6].y = _v.y;
    }
    { float4 _v = (float4)(((r[6]).wwww)+((r[6]).xxxx));
        r[2].w = _v.w;
    }
    { float4 _v = (float4)(vp_dot4(((r[3]).xyzw),((r[3]).xyzw)));
        r[1].w = _v.w;
    }
    { float4 _v = (float4)(((r[1]).xyxx)+((r[1]).yzzy));
        r[0].xyz = _v.xyz;
    }
    { float4 _v = (float4)((float4)(((vp_c[467]).zzzz)<((r[1]).wwww)));
        cc[0].x = _v.x;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        o[5].x = _v.x;
    }
    { float4 _v = (float4)((vp_c[467]).zzzz);
        r[0].w = _v.w;
    }
    { float4 _v = (float4)((1.0/((r[1]).wwww).x));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)(((vp_c[467]).wwww)+(-(r[0]).xyzx));
        r[4].xyz = _v.xyz;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xxxx),((vp_c[467]).yyyy)));
        if (cc[0].x != 0.0) r[0].w = _v.w;
    }
    { float4 _v = (float4)(vp_mul(((r[3]).xyzx),((r[0]).wwww)));
        r[2].xyz = _v.xyz;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzz),((r[3]).xyzy)));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzz),((r[3]).wwwx)));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).yzyy),((r[3]).xyxx)));
        r[2].xy = _v.xy;
    }
    { float4 _v = (float4)(((r[1]).wwww)+(-(r[1]).yyyy));
        r[10].x = _v.x;
    }
    { float4 _v = (float4)(((r[1]).wwww)+((r[1]).yyyy));
        r[9].z = _v.z;
    }
    { float4 _v = (float4)(((r[0]).wwww)+((r[1]).xxxx));
        r[10].y = _v.y;
    }
    { float4 _v = (float4)((-(r[1]).xzxx)+((r[2]).yxyy));
        r[1].xy = _v.xy;
    }
    { float4 _v = (float4)(((r[1]).zzzz)+((r[2]).xxxx));
        r[7].x = _v.x;
    }
    { float4 _v = (float4)(((r[0]).xyxx)+((r[0]).yzzy));
        r[0].xyz = _v.xyz;
    }
    { float4 _v = (float4)(((vp_c[467]).wwww)+(-(r[0]).yzxy));
        r[0].xyz = _v.xyz;
    }
    { float4 _v = (float4)((r[1]).yyyy);
        r[9].y = _v.y;
    }
    { float4 _v = (float4)((r[1]).xxxx);
        r[7].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).zzzz);
        r[10].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).xxxx);
        r[9].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).yyyy);
        r[7].y = _v.y;
    }
    { float4 _v = (float4)(vp_mul(((r[7]).xyzw),((r[11]).xxxx)));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[4]).zzzz),((r[7]).xyzw)));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).wwww),((r[7]).xyzw)));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[8]).wwww),((r[9]).xyzw))+((r[2]).xyzw));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[9]).xyzw),((r[6]).yyyy))+((r[1]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[4]).yyyy),((r[9]).xyzw))+((r[0]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[10]).xyzw),((r[4]).wwww))+((r[0]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[10]).xyzw),((r[11]).yyyy))+((r[1]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[4]).xxxx),((r[10]).xyzw))+((r[2]).xyzw));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[5]).zzzz),((vp_c[467]).zzzw))+((r[2]).xyzw));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[5]).yyyy),((vp_c[467]).zzzw))+((r[1]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[5]).xxxx),((vp_c[467]).zzzw))+((r[0]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[9]).yyyy)));
        r[4].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[10]).yyyy)));
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[8]).yyyy)));
        r[6].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[11]).yyyy)));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[11]).xxxx))+((r[1]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[8]).xxxx))+((r[6]).xyzw));
        r[6].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[10]).xxxx))+((r[3]).xyzw));
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[9]).xxxx))+((r[4]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[9]).zzzz))+((r[0]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[10]).zzzz))+((r[3]).xyzw));
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[8]).zzzz))+((r[6]).xyzw));
        r[4].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[11]).zzzz))+((r[1]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[8]).xxxy),((vp_c[11]).wwww))+((r[1]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[8]).xxxy),((vp_c[8]).wwww))+((r[4]).xyzw));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[8]).xxxy),((vp_c[10]).wwww))+((r[3]).xyzw));
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[8]).xxxy),((vp_c[9]).wwww))+((r[0]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_dot3(((r[3]).xyzx),((v[2]).xyzx)));
        r[4].z = _v.z;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((r[0]).xyzx)));
        r[4].y = _v.y;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((r[2]).xyzx)));
        r[4].x = _v.x;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[1]).xyzw))+((r[1]).xyzw).w));
        r[1].w = _v.w;
    }
    { float4 _v = (float4)(vp_dot3(((r[4]).xyzx),((r[4]).xyzx)));
        r[4].w = _v.w;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[3]).xyzw))+((r[3]).xyzw).w));
        r[1].z = _v.z;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[0]).xyzw))+((r[0]).xyzw).w));
        r[1].y = _v.y;
    }
    { float4 _v = (float4)(rsqrt(max(abs(((r[4]).wwww).x), 1e-10)));
        r[3].x = _v.x;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[2]).xyzw))+((r[2]).xyzw).w));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)(vp_mul(((r[3]).xxxx),((r[4]).xyzx)));
        r[5].xyz = _v.xyz;
    }
    { float4 _v = (float4)(vp_mul(((r[5]).xyzw),((vp_c[294]).xxxx))+((r[1]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[8]).zzzz),((vp_c[293]).wwww)));
        o[1].w = _v.w;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[3]).xyzw)));
        o[0].w = _v.w;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[2]).xyzw)));
        o[0].z = _v.z;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[1]).xyzw)));
        o[0].y = _v.y;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[0]).xyzw)));
        o[0].x = _v.x;
    }
        _pc = -1;
    }
    }
    VSOutput Out;
    float4 _p = o[0];
    Out.pos = float4(_p.xyz * vp_posscale.xyz + _p.w * vp_posoffset.xyz, _p.w);
    Out.col0 = o[1]; Out.col1 = o[2]; Out.fog = o[5].xxxx;
    Out.t0=o[7];  Out.t1=o[8];  Out.t2=o[9];  Out.t3=o[10];
    Out.t4=o[11]; Out.t5=o[12]; Out.t6=o[13]; Out.t7=o[14];
    return Out;
}

struct VSInput {
    float4 a0:ATTR0;
    float4 a2:ATTR2;
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
    v[2]=input.a2;
    float4 r[32]; float4 o[16];
    [unroll] for (int _i=0;_i<32;_i++) r[_i]=(float4)0;
    [unroll] for (int _j=0;_j<16;_j++) o[_j]=float4(0,0,0,1);
    int4 a0 = (int4)0; int4 a1 = (int4)0;
    float4 cc[2]; cc[0] = (float4)0; cc[1] = (float4)0;
    { float4 _v = (float4)((vp_c[467]).xxxx);
        o[8].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        o[9].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        o[10].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        o[11].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[293]).xyzx);
        o[1].xyz = _v.xyz;
    }
    { float4 _v = (float4)((float4)(((v[2]).xyzx)!=((vp_c[467]).xxxx)));
        r[2].xyz = _v.xyz;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        o[7].xy = _v.xy;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        r[1].w = _v.w;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[11]).xyzw))+((vp_c[11]).xyzw).w));
        r[0].w = _v.w;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[10]).xyzw))+((vp_c[10]).xyzw).w));
        r[0].z = _v.z;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((vp_c[10]).xyzx)));
        r[1].z = _v.z;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((vp_c[9]).xyzx)));
        r[1].y = _v.y;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((vp_c[8]).xyzx)));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[9]).xyzw))+((vp_c[9]).xyzw).w));
        r[0].y = _v.y;
    }
    { float4 _v = (float4)(vp_dot3(((r[1]).xyzx),((r[1]).xyzx)));
        r[2].w = _v.w;
    }
    { float4 _v = (float4)(((r[2]).xxxx)+((r[2]).yyyy)); _v = saturate(_v);
        r[2].x = _v.x;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[8]).xyzw))+((vp_c[8]).xyzw).w));
        r[0].x = _v.x;
    }
    { float4 _v = (float4)(rsqrt(max(abs(((r[2]).wwww).x), 1e-10)));
        r[2].y = _v.y;
    }
    { float4 _v = (float4)(((r[2]).zzzz)+((r[2]).xxxx)); _v = saturate(_v);
        r[2].x = _v.x;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).yyyy),((r[1]).xyzx)));
        r[1].xyz = _v.xyz;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[294]).xxxx))+((r[0]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xxxx),((vp_c[293]).wwww)));
        o[1].w = _v.w;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[3]).xyzw)));
        o[0].w = _v.w;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[1]).xyzw)));
        o[0].y = _v.y;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[2]).xyzw)));
        r[1].x = _v.x;
    }
    { float4 _v = (float4)(vp_dot4(((r[0]).xyzw),((vp_c[0]).xyzw)));
        o[0].x = _v.x;
    }
    { float4 _v = (float4)((r[1]).xxxx);
        o[0].z = _v.z;
    }
    { float4 _v = (float4)((r[1]).xxxx);
        o[5].x = _v.x;
    }
    VSOutput Out;
    float4 _p = o[0];
    Out.pos = float4(_p.xyz * vp_posscale.xyz + _p.w * vp_posoffset.xyz, _p.w);
    Out.col0 = o[1]; Out.col1 = o[2]; Out.fog = o[5].xxxx;
    Out.t0=o[7];  Out.t1=o[8];  Out.t2=o[9];  Out.t3=o[10];
    Out.t4=o[11]; Out.t5=o[12]; Out.t6=o[13]; Out.t7=o[14];
    return Out;
}

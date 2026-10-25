struct VSInput {
    float4 a0:ATTR0;
    float4 a2:ATTR2;
    float4 a3:ATTR3;
    float4 a8:ATTR8;
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
    v[3]=input.a3;
    v[8]=input.a8;
    float4 r[32]; float4 o[16];
    [unroll] for (int _i=0;_i<32;_i++) r[_i]=(float4)0;
    [unroll] for (int _j=0;_j<16;_j++) o[_j]=float4(0,0,0,1);
    int4 a0 = (int4)0; int4 a1 = (int4)0;
    float4 cc[2]; cc[0] = (float4)0; cc[1] = (float4)0;
    { float4 _v = (float4)(vp_mul(((v[3]).xyzw),((vp_c[293]).xyzw)));
        o[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        o[10].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[467]).xxxx);
        o[11].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[11]).xyzw);
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[10]).xyzw);
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[8]).xyzw);
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((vp_c[9]).xyzw);
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)((float4)(((v[2]).xyzx)!=((vp_c[467]).xxxx)));
        r[7].xyz = _v.xyz;
    }
    { float4 _v = (float4)(((vp_c[292]).xyxx)+((v[8]).xyxx));
        o[7].xy = _v.xy;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[11]).xyzw))+((vp_c[11]).xyzw).w));
        o[8].w = _v.w;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[10]).xyzw))+((vp_c[10]).xyzw).w));
        o[8].z = _v.z;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[9]).xyzw))+((vp_c[9]).xyzw).w));
        o[8].y = _v.y;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((vp_c[8]).xyzw))+((vp_c[8]).xyzw).w));
        o[8].x = _v.x;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((vp_c[10]).xyzx)));
        o[9].z = _v.z;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((vp_c[9]).xyzx)));
        o[9].y = _v.y;
    }
    { float4 _v = (float4)(vp_dot3(((v[2]).xyzx),((vp_c[8]).xyzx)));
        o[9].x = _v.x;
    }
    { float4 _v = (float4)(((r[7]).xxxx)+((r[7]).yyyy)); _v = saturate(_v);
        r[7].x = _v.x;
    }
    { float4 _v = (float4)(vp_mul(((r[3]).xyzw),((vp_c[3]).yyyy)));
        r[4].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[3]).xyzw),((vp_c[1]).yyyy)));
        r[6].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[3]).xyzw),((vp_c[2]).yyyy)));
        r[5].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[3]).xyzw),((vp_c[0]).yyyy)));
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[0]).xxxx))+((r[3]).xyzw));
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[2]).xxxx))+((r[5]).xyzw));
        r[5].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[1]).xxxx))+((r[6]).xyzw));
        r[6].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[2]).xyzw),((vp_c[3]).xxxx))+((r[4]).xyzw));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(((r[7]).zzzz)+((r[7]).xxxx)); _v = saturate(_v);
        r[7].x = _v.x;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[3]).zzzz))+((r[2]).xyzw));
        r[2].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[1]).zzzz))+((r[6]).xyzw));
        r[4].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[2]).zzzz))+((r[5]).xyzw));
        r[5].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[1]).xyzw),((vp_c[0]).zzzz))+((r[3]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[0]).wwww))+((r[1]).xyzw));
        r[1].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[2]).wwww))+((r[5]).xyzw));
        r[3].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[1]).wwww))+((r[4]).xyzw));
        r[4].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[0]).xyzw),((vp_c[3]).wwww))+((r[2]).xyzw));
        r[0].xyzw = _v.xyzw;
    }
    { float4 _v = (float4)(vp_mul(((r[7]).xxxx),((vp_c[304]).xxxx)));
        o[9].w = _v.w;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[0]).xyzw))+((r[0]).xyzw).w));
        o[0].w = _v.w;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[4]).xyzw))+((r[4]).xyzw).w));
        o[0].y = _v.y;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[3]).xyzw))+((r[3]).xyzw).w));
        r[0].x = _v.x;
    }
    { float4 _v = (float4)((vp_dot3(((v[0]).xyzx),((r[1]).xyzw))+((r[1]).xyzw).w));
        o[0].x = _v.x;
    }
    { float4 _v = (float4)((r[0]).xxxx);
        o[0].z = _v.z;
    }
    { float4 _v = (float4)((r[0]).xxxx);
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

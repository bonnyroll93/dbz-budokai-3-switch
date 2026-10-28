struct VSInput {
    uint vertex_id : SV_VERTEXID;
};
struct VSOutput {
    float4 pos : SV_POSITION;
    float4 uv : TEXCOORD0;
};
VSOutput main(VSInput input) {
    VSOutput o;
    if (input.vertex_id == 0) {
        o.pos = float4(-1.0, -1.0, 0.0, 1.0);
        o.uv = float4(0.0, 1.0, 0.0, 1.0);
    } else if (input.vertex_id == 1) {
        o.pos = float4(3.0, -1.0, 0.0, 1.0);
        o.uv = float4(2.0, 1.0, 0.0, 1.0);
    } else {
        o.pos = float4(-1.0, 3.0, 0.0, 1.0);
        o.uv = float4(0.0, -1.0, 0.0, 1.0);
    }
    return o;
}

cbuffer View : register(b0, space1) { float2 size; float2 pad; };
struct Input { float4 position : TEXCOORD0; float4 color : TEXCOORD1; float2 uv : TEXCOORD2; float fog : TEXCOORD3; };
struct Output { float4 position : SV_Position; noperspective float4 color : TEXCOORD0; float2 uv : TEXCOORD1; noperspective float fog : TEXCOORD2; float eye_depth : TEXCOORD3; };
Output main(Input v) {
    Output o;
    float w = abs(v.position.w) > 0.0000001 ? 1.0 / v.position.w : 1.0;
    o.position = float4((v.position.x * 2.0 / size.x - 1.0) * w,
                       (1.0 - v.position.y * 2.0 / size.y) * w,
                       clamp(v.position.z, 0.0, 1.0) * w, w);
    o.color = v.color; o.uv = v.uv; o.fog = v.fog; o.eye_depth = max(w, 0.0);
    return o;
}

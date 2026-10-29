Texture2D image : register(t0, space2);
SamplerState sample_state : register(s0, space2);
cbuffer Effects : register(b0, space3) { float4 fog_color; float4 effects; float4 fog_params; };
struct Input { float4 position : SV_Position; noperspective float4 color : TEXCOORD0; float2 uv : TEXCOORD1; noperspective float fog : TEXCOORD2; float eye_depth : TEXCOORD3; };
float4 main(Input v) : SV_Target0 {
    float4 texel = image.Sample(sample_state, v.uv);
    float4 c = float4(texel.rgb * v.color.rgb,
                      texel.a * (effects.w > 0.5 ? v.color.a : 1.0));
    if (effects.x > 0.5 && c.a < effects.y) discard;
    if (effects.z > 0.5) {
        float visibility = saturate(v.fog);
        float d = max(v.eye_depth, 0.0), amount = max(fog_params.y, 0.0) * d;
        if (fog_params.x > 2.5) visibility = exp(-amount * amount);
        else if (fog_params.x > 1.5) visibility = exp(-amount);
        else if (fog_params.x > 0.5) visibility = saturate((fog_params.w - d) / max(fog_params.w - fog_params.z, 0.0000001));
        c.rgb = lerp(fog_color.rgb, c.rgb, saturate(visibility));
    }
    return c;
}

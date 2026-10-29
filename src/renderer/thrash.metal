#include <metal_stdlib>
using namespace metal;
struct In { float4 position [[attribute(0)]]; float4 color [[attribute(1)]]; float2 uv [[attribute(2)]]; float fog [[attribute(3)]]; };
struct Out { float4 position [[position]]; float4 color [[center_no_perspective]]; float2 uv; float fog [[center_no_perspective]]; float eye_depth; };
struct View { float2 size; float2 pad; };
struct Effects { float4 fog_color; float4 effects; float4 fog_params; };
vertex Out vs_main(In v [[stage_in]], constant View &view [[buffer(0)]]) {
    Out o;
    float w=abs(v.position.w)>0.0000001 ? 1.0/v.position.w : 1.0;
    o.position=float4((v.position.x*2.0/view.size.x-1.0)*w,(1.0-v.position.y*2.0/view.size.y)*w,clamp(v.position.z,0.0,1.0)*w,w);
    o.color=v.color; o.uv=v.uv; o.fog=v.fog; o.eye_depth=max(w,0.0); return o;
}
fragment float4 ps_main(Out v [[stage_in]], texture2d<float> image [[texture(0)]], sampler sample_state [[sampler(0)]], constant Effects &fx [[buffer(0)]]) {
    float4 texel=image.sample(sample_state,v.uv);
    float4 c=float4(texel.rgb*v.color.rgb,texel.a*(fx.effects.w>0.5 ? v.color.a:1.0));
    if(fx.effects.x>0.5 && c.a<fx.effects.y) discard_fragment();
    if(fx.effects.z>0.5) {
        float visibility=clamp(v.fog,0.0,1.0);
        float d=max(v.eye_depth,0.0),amount=max(fx.fog_params.y,0.0)*d;
        if(fx.fog_params.x>2.5)visibility=exp(-amount*amount);
        else if(fx.fog_params.x>1.5)visibility=exp(-amount);
        else if(fx.fog_params.x>0.5)visibility=clamp((fx.fog_params.w-d)/max(fx.fog_params.w-fx.fog_params.z,0.0000001),0.0,1.0);
        c.rgb=mix(fx.fog_color.rgb,c.rgb,clamp(visibility,0.0,1.0));
    }
    return c;
}

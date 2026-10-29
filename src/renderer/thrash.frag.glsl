#version 450
layout(set=2,binding=0) uniform sampler2D image;
layout(set=3,binding=0) uniform Effects { vec4 fog_color; vec4 effects; vec4 fog_params; } fx;
layout(location=0) noperspective in vec4 color;
layout(location=1) in vec2 uv;
layout(location=2) noperspective in float fog;
layout(location=3) in float eye_depth;
layout(location=0) out vec4 result;
void main() {
    vec4 texel=texture(image,uv);
    vec4 c=vec4(texel.rgb*color.rgb,texel.a*(fx.effects.w>0.5 ? color.a:1.0));
    if(fx.effects.x>0.5 && c.a<fx.effects.y) discard;
    if(fx.effects.z>0.5) {
        float visibility=clamp(fog,0.0,1.0);
        float d=max(eye_depth,0.0),amount=max(fx.fog_params.y,0.0)*d;
        if(fx.fog_params.x>2.5)visibility=exp(-amount*amount);
        else if(fx.fog_params.x>1.5)visibility=exp(-amount);
        else if(fx.fog_params.x>0.5)visibility=clamp((fx.fog_params.w-d)/max(fx.fog_params.w-fx.fog_params.z,0.0000001),0.0,1.0);
        c.rgb=mix(fx.fog_color.rgb,c.rgb,clamp(visibility,0.0,1.0));
    }
    result=c;
}

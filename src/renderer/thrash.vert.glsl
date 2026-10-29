#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec4 color;
layout(location=2) in vec2 uv;
layout(location=3) in float fog;
layout(set=1,binding=0) uniform View { vec2 size; vec2 pad; } view;
layout(location=0) noperspective out vec4 out_color;
layout(location=1) out vec2 out_uv;
layout(location=2) noperspective out float out_fog;
layout(location=3) out float out_eye_depth;
void main() {
    float w = abs(position.w) > 0.0000001 ? 1.0 / position.w : 1.0;
    gl_Position=vec4((position.x*2.0/view.size.x-1.0)*w,
                     (1.0-position.y*2.0/view.size.y)*w,
                     clamp(position.z,0.0,1.0)*w,w);
    out_color=color; out_uv=uv; out_fog=fog; out_eye_depth=max(w,0.0);
}

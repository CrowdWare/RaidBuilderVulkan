#version 450

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) flat in float v_tex_index;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_ground;
layout(set = 0, binding = 1) uniform sampler2D u_block;

void main() {
    vec3 n = normalize(v_normal);
    vec3 light_dir = normalize(vec3(0.2, 1.0, 0.1));
    float ndotl = max(dot(n, light_dir), 0.0);
    float ambient = 0.65;
    vec3 sunlight = vec3(1.0, 0.95, 0.85) * ndotl * 0.75;
    vec3 base = v_color;
    if (v_tex_index > 0.5 && v_tex_index < 1.5)
        base *= texture(u_ground, v_uv).rgb;
    else if (v_tex_index >= 1.5)
        base *= texture(u_block, v_uv).rgb;
    vec3 lit = base * (ambient + sunlight);
    out_color = vec4(lit, 1.0);
}

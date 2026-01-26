#version 450

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(v_normal);
    vec3 light_dir = normalize(vec3(0.2, 1.0, 0.1));
    float ndotl = max(dot(n, light_dir), 0.0);
    float ambient = 0.65;
    vec3 sunlight = vec3(1.0, 0.95, 0.85) * ndotl * 0.75;
    vec3 lit = v_color * (ambient + sunlight);
    out_color = vec4(lit, 1.0);
}

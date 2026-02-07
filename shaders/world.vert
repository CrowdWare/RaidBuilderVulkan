#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in uvec4 in_joints;
layout(location = 5) in vec4 in_weights;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) flat out float v_tex_index;
layout(location = 4) flat out uint v_is_skinned;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 tint;
    uvec4 skin;
} push;

layout(std430, set = 0, binding = 2) readonly buffer SkinPalette {
    mat4 mats[];
} skin_palette;

void main() {
    vec4 skinned_pos = vec4(in_pos, 1.0);
    vec3 skinned_normal = in_normal;
    if (push.skin.y != 0u) {
        mat4 m = mat4(0.0);
        m += skin_palette.mats[push.skin.x + in_joints.x] * in_weights.x;
        m += skin_palette.mats[push.skin.x + in_joints.y] * in_weights.y;
        m += skin_palette.mats[push.skin.x + in_joints.z] * in_weights.z;
        m += skin_palette.mats[push.skin.x + in_joints.w] * in_weights.w;
        skinned_pos = m * skinned_pos;
        skinned_normal = mat3(m) * skinned_normal;
    }
    v_color = in_color * push.tint.rgb;
    v_normal = skinned_normal;
    v_uv = in_uv;
    v_tex_index = push.tint.a;
    v_is_skinned = push.skin.z;
    gl_Position = push.mvp * skinned_pos;
}

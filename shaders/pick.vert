#version 450

layout(location = 0) in vec3 in_pos;

layout(push_constant) uniform PickPush {
    mat4 mvp;
    uvec4 id;
} push;

layout(location = 0) flat out uint v_id;

void main() {
    v_id = push.id.x;
    gl_Position = push.mvp * vec4(in_pos, 1.0);
}

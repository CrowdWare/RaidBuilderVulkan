#version 450

layout(location = 0) flat in uint v_id;
layout(location = 0) out uint out_id;

void main() {
    out_id = v_id;
}

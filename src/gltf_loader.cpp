/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of RaidBuilder.
 */

#include "gltf_loader.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include <vector>
#include <cmath>

static bool ReadAccessor(const tinygltf::Model& model,
                         const tinygltf::Accessor& accessor,
                         std::vector<float>* out,
                         int expected_components) {
    if (!out)
        return false;
    out->clear();
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        return false;
    if (accessor.type == TINYGLTF_TYPE_SCALAR && expected_components != 1)
        return false;
    if (accessor.type == TINYGLTF_TYPE_VEC2 && expected_components != 2)
        return false;
    if (accessor.type == TINYGLTF_TYPE_VEC3 && expected_components != 3)
        return false;
    if (accessor.type == TINYGLTF_TYPE_VEC4 && expected_components != 4)
        return false;

    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    const unsigned char* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    size_t stride = accessor.ByteStride(view);
    if (stride == 0)
        stride = expected_components * sizeof(float);

    out->resize(accessor.count * expected_components);
    for (size_t i = 0; i < accessor.count; ++i) {
        const float* src = reinterpret_cast<const float*>(data + stride * i);
        for (int c = 0; c < expected_components; ++c)
            (*out)[i * expected_components + c] = src[c];
    }
    return true;
}

static bool ReadIndices(const tinygltf::Model& model,
                        const tinygltf::Accessor& accessor,
                        std::vector<uint32_t>* out) {
    if (!out)
        return false;
    out->clear();
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    const unsigned char* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    size_t stride = accessor.ByteStride(view);
    if (stride == 0) {
        size_t component = 0;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: component = 1; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: component = 2; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: component = 4; break;
            default: component = 4; break;
        }
        size_t comps = 1;
        stride = component * comps;
    }

    out->resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* src = data + stride * i;
        uint32_t value = 0;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            value = *reinterpret_cast<const uint16_t*>(src);
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            value = *reinterpret_cast<const uint32_t*>(src);
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            value = *reinterpret_cast<const uint8_t*>(src);
        } else {
            return false;
        }
        (*out)[i] = value;
    }
    return true;
}

static void ComputeNormals(const std::vector<float>& positions,
                           std::vector<float>* normals) {
    size_t count = positions.size() / 3;
    normals->assign(count * 3, 0.0f);
    for (size_t i = 0; i + 2 < count; i += 3) {
        const float* p0 = &positions[i * 3];
        const float* p1 = &positions[(i + 1) * 3];
        const float* p2 = &positions[(i + 2) * 3];
        float ux = p1[0] - p0[0];
        float uy = p1[1] - p0[1];
        float uz = p1[2] - p0[2];
        float vx = p2[0] - p0[0];
        float vy = p2[1] - p0[1];
        float vz = p2[2] - p0[2];
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-6f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        for (int v = 0; v < 3; ++v) {
            (*normals)[(i + v) * 3 + 0] = nx;
            (*normals)[(i + v) * 3 + 1] = ny;
            (*normals)[(i + v) * 3 + 2] = nz;
        }
    }
}

bool LoadGltfMesh(const std::string& path, GltfMesh* out_mesh, std::string* error) {
    if (!out_mesh)
        return false;
    out_mesh->mesh = voxel::VoxelRenderer::MeshData();
    out_mesh->has_uv = false;

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err;
    std::string warn;
    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!warn.empty())
        warn.clear();
    if (!ok) {
        if (error)
            *error = err.empty() ? "Failed to load glb" : err;
        return false;
    }
    if (model.meshes.empty() || model.meshes[0].primitives.empty()) {
        if (error)
            *error = "No mesh primitives";
        return false;
    }

    const tinygltf::Primitive& prim = model.meshes[0].primitives[0];
    auto it_pos = prim.attributes.find("POSITION");
    if (it_pos == prim.attributes.end()) {
        if (error)
            *error = "Missing POSITION";
        return false;
    }

    std::vector<float> positions;
    if (!ReadAccessor(model, model.accessors[it_pos->second], &positions, 3))
        return false;

    std::vector<float> normals;
    auto it_norm = prim.attributes.find("NORMAL");
    if (it_norm != prim.attributes.end())
        ReadAccessor(model, model.accessors[it_norm->second], &normals, 3);

    std::vector<float> uvs;
    auto it_uv = prim.attributes.find("TEXCOORD_0");
    if (it_uv != prim.attributes.end()) {
        if (ReadAccessor(model, model.accessors[it_uv->second], &uvs, 2))
            out_mesh->has_uv = true;
    }

    std::vector<float> colors;
    auto it_col = prim.attributes.find("COLOR_0");
    if (it_col != prim.attributes.end())
        ReadAccessor(model, model.accessors[it_col->second], &colors, 4);

    std::vector<uint32_t> indices;
    bool has_indices = (prim.indices >= 0);
    if (has_indices)
        ReadIndices(model, model.accessors[prim.indices], &indices);

    // Expand to non-indexed vertices
    std::vector<float> out_pos;
    std::vector<float> out_norm;
    std::vector<float> out_uv;
    std::vector<float> out_col;

    auto push_vertex = [&](uint32_t idx) {
        out_pos.push_back(positions[idx * 3 + 0]);
        out_pos.push_back(positions[idx * 3 + 1]);
        out_pos.push_back(positions[idx * 3 + 2]);
        if (!normals.empty()) {
            out_norm.push_back(normals[idx * 3 + 0]);
            out_norm.push_back(normals[idx * 3 + 1]);
            out_norm.push_back(normals[idx * 3 + 2]);
        }
        if (!uvs.empty()) {
            out_uv.push_back(uvs[idx * 2 + 0]);
            out_uv.push_back(uvs[idx * 2 + 1]);
        }
        if (!colors.empty()) {
            out_col.push_back(colors[idx * 4 + 0]);
            out_col.push_back(colors[idx * 4 + 1]);
            out_col.push_back(colors[idx * 4 + 2]);
            out_col.push_back(colors[idx * 4 + 3]);
        }
    };

    if (has_indices) {
        for (size_t i = 0; i < indices.size(); ++i)
            push_vertex(indices[i]);
    } else {
        for (size_t i = 0; i < positions.size() / 3; ++i)
            push_vertex((uint32_t)i);
    }

    if (out_norm.empty()) {
        ComputeNormals(out_pos, &out_norm);
    }

    // Center block models if they are in 0..1 range.
    float min_x = 1e9f, min_y = 1e9f, min_z = 1e9f;
    float max_x = -1e9f, max_y = -1e9f, max_z = -1e9f;
    for (size_t i = 0; i < out_pos.size(); i += 3) {
        min_x = std::min(min_x, out_pos[i + 0]);
        min_y = std::min(min_y, out_pos[i + 1]);
        min_z = std::min(min_z, out_pos[i + 2]);
        max_x = std::max(max_x, out_pos[i + 0]);
        max_y = std::max(max_y, out_pos[i + 1]);
        max_z = std::max(max_z, out_pos[i + 2]);
    }
    if (min_x >= -0.001f && min_y >= -0.001f && min_z >= -0.001f &&
        max_x <= 1.001f && max_y <= 1.001f && max_z <= 1.001f) {
        for (size_t i = 0; i < out_pos.size(); i += 3) {
            out_pos[i + 0] -= 0.5f;
            out_pos[i + 1] -= 0.5f;
            out_pos[i + 2] -= 0.5f;
        }
    }

    out_mesh->mesh.positions = out_pos;
    out_mesh->mesh.normals = out_norm;
    out_mesh->mesh.uvs = out_uv;
    out_mesh->mesh.colors = out_col;
    return true;
}

/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of RaidBuilder.
 */

#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <algorithm>

#include "block_sml.h"

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

static char GetVoxelChar(const BlockDef& block, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0)
        return '.';
    if (x >= block.size || y >= block.layers || z >= block.size)
        return '.';
    int idx = y * block.size + z;
    if (idx < 0 || idx >= (int)block.layers_text.size())
        return '.';
    const std::string& row = block.layers_text[idx];
    if (x >= (int)row.size())
        return '.';
    return row[x];
}

static BlockColor GetVoxelColor(const BlockDef& block, int x, int y, int z) {
    char id = GetVoxelChar(block, x, y, z);
    std::map<char, BlockColor>::const_iterator it = block.palette.find(id);
    if (it != block.palette.end())
        return it->second;
    BlockColor empty;
    empty.a = 0.0f;
    return empty;
}

static void AddFace(std::vector<Vertex>& vertices,
                    std::vector<uint32_t>& indices,
                    const BlockColor& color,
                    const float v0[3],
                    const float v1[3],
                    const float v2[3],
                    const float v3[3]) {
    uint32_t base = (uint32_t)vertices.size();
    Vertex verts[4] = {
        {v0[0], v0[1], v0[2], color.r, color.g, color.b, color.a},
        {v1[0], v1[1], v1[2], color.r, color.g, color.b, color.a},
        {v2[0], v2[1], v2[2], color.r, color.g, color.b, color.a},
        {v3[0], v3[1], v3[2], color.r, color.g, color.b, color.a},
    };
    vertices.insert(vertices.end(), verts, verts + 4);
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

static void BuildMesh(const BlockDef& block,
                      std::vector<Vertex>& out_vertices,
                      std::vector<uint32_t>& out_indices) {
    out_vertices.clear();
    out_indices.clear();

    BlockDef local = block;
    if (local.palette.find('.') == local.palette.end()) {
        BlockColor empty;
        empty.a = 0.0f;
        local.palette['.'] = empty;
    }

    float cell = 1.0f / (float)std::max(1, local.size);
    for (int y = 0; y < local.layers; ++y) {
        for (int z = 0; z < local.size; ++z) {
            for (int x = 0; x < local.size; ++x) {
                BlockColor color = GetVoxelColor(local, x, y, z);
                if (color.a <= 0.0f)
                    continue;

                bool empty_xp = GetVoxelColor(local, x + 1, y, z).a <= 0.0f;
                bool empty_xn = GetVoxelColor(local, x - 1, y, z).a <= 0.0f;
                bool empty_yp = GetVoxelColor(local, x, y + 1, z).a <= 0.0f;
                bool empty_yn = GetVoxelColor(local, x, y - 1, z).a <= 0.0f;
                bool empty_zp = GetVoxelColor(local, x, y, z + 1).a <= 0.0f;
                bool empty_zn = GetVoxelColor(local, x, y, z - 1).a <= 0.0f;

                float x0 = x * cell;
                float x1 = (x + 1) * cell;
                float y0 = y * cell;
                float y1 = (y + 1) * cell;
                float z0 = z * cell;
                float z1 = (z + 1) * cell;

                if (empty_xp) {
                    float v0[3] = {x1, y0, z0};
                    float v1[3] = {x1, y1, z0};
                    float v2[3] = {x1, y1, z1};
                    float v3[3] = {x1, y0, z1};
                    AddFace(out_vertices, out_indices, color, v0, v1, v2, v3);
                }
                if (empty_xn) {
                    float v0[3] = {x0, y0, z1};
                    float v1[3] = {x0, y1, z1};
                    float v2[3] = {x0, y1, z0};
                    float v3[3] = {x0, y0, z0};
                    AddFace(out_vertices, out_indices, color, v0, v1, v2, v3);
                }
                if (empty_yp) {
                    float v0[3] = {x0, y1, z0};
                    float v1[3] = {x0, y1, z1};
                    float v2[3] = {x1, y1, z1};
                    float v3[3] = {x1, y1, z0};
                    AddFace(out_vertices, out_indices, color, v0, v1, v2, v3);
                }
                if (empty_yn) {
                    float v0[3] = {x0, y0, z1};
                    float v1[3] = {x0, y0, z0};
                    float v2[3] = {x1, y0, z0};
                    float v3[3] = {x1, y0, z1};
                    AddFace(out_vertices, out_indices, color, v0, v1, v2, v3);
                }
                if (empty_zp) {
                    float v0[3] = {x1, y0, z1};
                    float v1[3] = {x1, y1, z1};
                    float v2[3] = {x0, y1, z1};
                    float v3[3] = {x0, y0, z1};
                    AddFace(out_vertices, out_indices, color, v0, v1, v2, v3);
                }
                if (empty_zn) {
                    float v0[3] = {x0, y0, z0};
                    float v1[3] = {x0, y1, z0};
                    float v2[3] = {x1, y1, z0};
                    float v3[3] = {x1, y0, z0};
                    AddFace(out_vertices, out_indices, color, v0, v1, v2, v3);
                }
            }
        }
    }
}

static void AppendBytes(std::vector<uint8_t>& dst, const void* data, size_t size) {
    const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
    dst.insert(dst.end(), src, src + size);
}

static void PadTo4(std::vector<uint8_t>& dst, uint8_t value) {
    while (dst.size() % 4 != 0)
        dst.push_back(value);
}

static bool WriteGlb(const std::string& path,
                     const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty())
        return false;

    std::vector<float> positions;
    std::vector<float> colors;
    positions.reserve(vertices.size() * 3);
    colors.reserve(vertices.size() * 4);
    for (size_t i = 0; i < vertices.size(); ++i) {
        positions.push_back(vertices[i].x);
        positions.push_back(vertices[i].y);
        positions.push_back(vertices[i].z);
        colors.push_back(vertices[i].r);
        colors.push_back(vertices[i].g);
        colors.push_back(vertices[i].b);
        colors.push_back(vertices[i].a);
    }

    std::vector<uint8_t> bin;
    size_t pos_offset = 0;
    AppendBytes(bin, positions.data(), positions.size() * sizeof(float));
    PadTo4(bin, 0);
    size_t col_offset = bin.size();
    AppendBytes(bin, colors.data(), colors.size() * sizeof(float));
    PadTo4(bin, 0);
    size_t idx_offset = bin.size();
    AppendBytes(bin, indices.data(), indices.size() * sizeof(uint32_t));
    PadTo4(bin, 0);

    size_t pos_len = positions.size() * sizeof(float);
    size_t col_len = colors.size() * sizeof(float);
    size_t idx_len = indices.size() * sizeof(uint32_t);

    std::ostringstream json;
    json << "{";
    json << "\"asset\":{\"version\":\"2.0\"},";
    json << "\"buffers\":[{\"byteLength\":" << bin.size() << "}],";
    json << "\"bufferViews\":[";
    json << "{\"buffer\":0,\"byteOffset\":" << pos_offset << ",\"byteLength\":" << pos_len << ",\"target\":34962},";
    json << "{\"buffer\":0,\"byteOffset\":" << col_offset << ",\"byteLength\":" << col_len << ",\"target\":34962},";
    json << "{\"buffer\":0,\"byteOffset\":" << idx_offset << ",\"byteLength\":" << idx_len << ",\"target\":34963}";
    json << "],";
    json << "\"accessors\":[";
    json << "{\"bufferView\":0,\"componentType\":5126,\"count\":" << vertices.size()
         << ",\"type\":\"VEC3\"},";
    json << "{\"bufferView\":1,\"componentType\":5126,\"count\":" << vertices.size()
         << ",\"type\":\"VEC4\"},";
    json << "{\"bufferView\":2,\"componentType\":5125,\"count\":" << indices.size()
         << ",\"type\":\"SCALAR\"}";
    json << "],";
    json << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"COLOR_0\":1},\"indices\":2,\"mode\":4}]}],";
    json << "\"nodes\":[{\"mesh\":0}],";
    json << "\"scenes\":[{\"nodes\":[0]}],";
    json << "\"scene\":0";
    json << "}";

    std::string json_str = json.str();
    std::vector<uint8_t> json_bytes(json_str.begin(), json_str.end());
    PadTo4(json_bytes, 0x20);

    uint32_t magic = 0x46546C67; // glTF
    uint32_t version = 2;
    uint32_t length = 12 + 8 + (uint32_t)json_bytes.size() + 8 + (uint32_t)bin.size();

    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out.is_open())
        return false;

    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&length), 4);

    uint32_t json_len = (uint32_t)json_bytes.size();
    uint32_t json_type = 0x4E4F534A; // JSON
    out.write(reinterpret_cast<const char*>(&json_len), 4);
    out.write(reinterpret_cast<const char*>(&json_type), 4);
    out.write(reinterpret_cast<const char*>(json_bytes.data()), json_bytes.size());

    uint32_t bin_len = (uint32_t)bin.size();
    uint32_t bin_type = 0x004E4942; // BIN
    out.write(reinterpret_cast<const char*>(&bin_len), 4);
    out.write(reinterpret_cast<const char*>(&bin_type), 4);
    out.write(reinterpret_cast<const char*>(bin.data()), bin.size());

    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input.sml> <output.glb>\n", argv[0]);
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = argv[2];

    BlockDef block;
    std::string error;
    if (!LoadBlockSml(input_path, &block, &error)) {
        std::fprintf(stderr, "Block SML error: %s\n", error.c_str());
        return 1;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    BuildMesh(block, vertices, indices);

    if (!WriteGlb(output_path, vertices, indices)) {
        std::fprintf(stderr, "Failed to write glb: %s\n", output_path.c_str());
        return 1;
    }

    std::printf("Baked %s -> %s (%zu verts, %zu indices)\n",
                input_path.c_str(), output_path.c_str(), vertices.size(), indices.size());
    return 0;
}

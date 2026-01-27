#pragma once

#include <string>
#include "voxel_renderer.h"

struct GltfMesh {
    voxel::VoxelRenderer::MeshData mesh;
    bool has_uv = false;
};

bool LoadGltfMesh(const std::string& path, GltfMesh* out_mesh, std::string* error);

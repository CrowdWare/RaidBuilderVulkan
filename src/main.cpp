/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of RaidBuilder.
 *
 *  RaidBuilder is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RaidBuilder is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RaidBuilder.  If not, see <http://www.gnu.org/licenses/>.
 */

// RaidBuilder - Vulkan + ImGui + SMLUI

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "sml_ui.h"
#include "sml_parser.h"
#include "voxel_renderer.h"
#include "voxel_character_controller.h"
#include "rotation_controls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cctype>
#include <map>
#include <algorithm>
#include <unordered_set>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#endif
#include "tile_catalog.h"
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#include <limits.h>
#endif

#define PI_F 3.1415926f

static long long BlockKey(int x, int y, int z) {
    constexpr int kOffset = 1 << 20;
    constexpr long long kMask = (1LL << 21) - 1;
    long long xx = static_cast<long long>(x + kOffset) & kMask;
    long long yy = static_cast<long long>(y + kOffset) & kMask;
    long long zz = static_cast<long long>(z + kOffset) & kMask;
    return (xx << 42) | (yy << 21) | zz;
}

static bool IsDebugEnabled(const char* env_name) {
    const char* value = std::getenv(env_name);
    if (!value)
        return false;
    std::string v(value);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

struct Mat4 {
    float m[16];
};

static Mat4 mat4Identity() {
    Mat4 m = {};
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

static Mat4 mat4Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return r;
}

static Mat4 mat4Perspective(float fovy_radians, float aspect, float znear, float zfar) {
    Mat4 m = {};
    float f = 1.0f / std::tan(fovy_radians * 0.5f);
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = zfar / (zfar - znear);
    m.m[11] = 1.0f;
    m.m[14] = (-znear * zfar) / (zfar - znear);
    return m;
}

static Mat4 mat4LookAt(float eye_x, float eye_y, float eye_z,
                       float at_x, float at_y, float at_z,
                       float up_x, float up_y, float up_z) {
    float fx = at_x - eye_x;
    float fy = at_y - eye_y;
    float fz = at_z - eye_z;
    float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= flen;
    fy /= flen;
    fz /= flen;

    float sx = fy * up_z - fz * up_y;
    float sy = fz * up_x - fx * up_z;
    float sz = fx * up_y - fy * up_x;
    float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= slen;
    sy /= slen;
    sz /= slen;

    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    Mat4 m = mat4Identity();
    m.m[0] = sx;
    m.m[4] = sy;
    m.m[8] = sz;
    m.m[1] = ux;
    m.m[5] = uy;
    m.m[9] = uz;
    m.m[2] = fx;
    m.m[6] = fy;
    m.m[10] = fz;
    m.m[12] = -(sx * eye_x + sy * eye_y + sz * eye_z);
    m.m[13] = -(ux * eye_x + uy * eye_y + uz * eye_z);
    m.m[14] = -(fx * eye_x + fy * eye_y + fz * eye_z);
    return m;
}

static bool ProjectToScreen(const Mat4& mvp, float x, float y, float z, const ImVec2& origin, const ImVec2& size, ImVec2* out) {
    float clip_x = mvp.m[0] * x + mvp.m[4] * y + mvp.m[8] * z + mvp.m[12];
    float clip_y = mvp.m[1] * x + mvp.m[5] * y + mvp.m[9] * z + mvp.m[13];
    float clip_w = mvp.m[3] * x + mvp.m[7] * y + mvp.m[11] * z + mvp.m[15];
    if (clip_w <= 0.0001f)
        return false;
    float ndc_x = clip_x / clip_w;
    float ndc_y = clip_y / clip_w;
    out->x = origin.x + (ndc_x * 0.5f + 0.5f) * size.x;
    out->y = origin.y + (ndc_y * 0.5f + 0.5f) * size.y;
    return true;
}

static int FindBlockAt(const std::vector<voxel::VoxelRenderer::Block>& blocks, float x, float y, float z, float eps) {
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (std::fabs(blocks[i].x - x) < eps &&
            std::fabs(blocks[i].y - y) < eps &&
            std::fabs(blocks[i].z - z) < eps) {
            return (int)i;
        }
    }
    return -1;
}

static void ComputeAxisPlacement(bool dir_valid,
                                 float dir_x,
                                 float dir_y,
                                 float dir_z,
                                 float anchor_x,
                                 float anchor_y,
                                 float anchor_z,
                                 float block_size,
                                 float in_x,
                                 float in_y,
                                 float in_z,
                                 float* out_x,
                                 float* out_y,
                                 float* out_z) {
    if (!dir_valid) {
        *out_x = in_x;
        *out_y = in_y;
        *out_z = in_z;
        return;
    }
    float rx = in_x - anchor_x;
    float ry = in_y - anchor_y;
    float rz = in_z - anchor_z;
    float dot = rx * dir_x + ry * dir_y + rz * dir_z;
    if (dot < 0.0f)
        dot = 0.0f;
    float step = std::round(dot / block_size) * block_size;
    *out_x = anchor_x + dir_x * step;
    *out_y = anchor_y + dir_y * step;
    *out_z = anchor_z + dir_z * step;
}

static bool FindNextFreePlacement(const std::vector<voxel::VoxelRenderer::Block>& blocks,
                                  float start_x,
                                  float start_y,
                                  float start_z,
                                  float dir_x,
                                  float dir_y,
                                  float dir_z,
                                  float block_size,
                                  int max_steps,
                                  float* out_x,
                                  float* out_y,
                                  float* out_z) {
    float step_x = 0.0f;
    float step_y = 0.0f;
    float step_z = 0.0f;
    if (dir_x > 0.5f) step_x = block_size;
    else if (dir_x < -0.5f) step_x = -block_size;
    if (dir_y > 0.5f) step_y = block_size;
    else if (dir_y < -0.5f) step_y = -block_size;
    if (dir_z > 0.5f) step_z = block_size;
    else if (dir_z < -0.5f) step_z = -block_size;

    float x = start_x;
    float y = start_y;
    float z = start_z;
    for (int i = 0; i <= max_steps; ++i) {
        if (FindBlockAt(blocks, x, y, z, 0.001f) < 0) {
            *out_x = x;
            *out_y = y;
            *out_z = z;
            return true;
        }
        if (step_x == 0.0f && step_y == 0.0f && step_z == 0.0f)
            break;
        x += step_x;
        y += step_y;
        z += step_z;
    }
    return false;
}

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include "mac_menu.h"
#else
#include "tinyfiledialogs.h"
#endif

static VkAllocationCallbacks* g_Allocator = nullptr;
static VkInstance g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice g_Device = VK_NULL_HANDLE;
static uint32_t g_QueueFamily = (uint32_t)-1;
static VkQueue g_Queue = VK_NULL_HANDLE;
static VkPipelineCache g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData;
static uint32_t g_MinImageCount = 2;
static bool g_SwapChainRebuild = false;
static voxel::VoxelRenderer g_VoxelRenderer;

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

static bool LoadFileText(const char* path, std::string* out_text) {
    std::ifstream file(path);
    if (!file.is_open())
        return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    *out_text = ss.str();
    return true;
}

static bool FileExists(const std::string& path) {
    std::ifstream file(path.c_str());
    return file.good();
}

static bool DirExists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return (st.st_mode & S_IFDIR) != 0;
}

static bool EnsureDir(const std::string& path) {
#if defined(_WIN32)
    if (_mkdir(path.c_str()) == 0)
        return true;
    return errno == EEXIST;
#else
    if (mkdir(path.c_str(), 0755) == 0)
        return true;
    return errno == EEXIST;
#endif
}

static std::string GetUserHomeDir() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home)
        return ".";
    return std::string(home);
}

static std::string GetHistoryDir() {
#if defined(__APPLE__)
    return GetUserHomeDir() + "/Library/Application Support/RaidBuilder/history";
#elif defined(_WIN32)
    return GetUserHomeDir() + "/AppData/Roaming/RaidBuilder/history";
#else
    return GetUserHomeDir() + "/.local/share/raidbuilder/history";
#endif
}

static std::string GetStateDir() {
#if defined(__APPLE__)
    return GetUserHomeDir() + "/Library/Application Support/RaidBuilder";
#elif defined(_WIN32)
    return GetUserHomeDir() + "/AppData/Roaming/RaidBuilder";
#else
    return GetUserHomeDir() + "/.local/share/raidbuilder";
#endif
}

static std::string GetParentDir(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return ".";
    return path.substr(0, slash);
}

static std::string GetFileBaseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    return name;
}

static std::string GetFileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return path;
    return path.substr(slash + 1);
}

static std::string NextHistoryPath(const std::string& history_dir, const std::string& base_name) {
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = history_dir + "/" + base_name + std::to_string(i) + ".sml";
        if (!FileExists(candidate))
            return candidate;
    }
    return history_dir + "/" + base_name + ".sml";
}

static bool CopyFile(const std::string& src, const std::string& dst) {
    std::ifstream in(src.c_str(), std::ios::binary);
    if (!in.is_open())
        return false;
    std::ofstream out(dst.c_str(), std::ios::binary);
    if (!out.is_open())
        return false;
    out << in.rdbuf();
    return true;
}

static bool WriteTextFile(const std::string& path, const std::string& contents) {
    EnsureDir(GetParentDir(path));
    std::ofstream out(path.c_str(), std::ios::trunc);
    if (!out.is_open())
        return false;
    out << contents;
    return true;
}

// Forward declarations for helpers used before their definitions.
static bool IsSymmetricTileModel(const std::string& model_in);
static bool SaveDungeonWithHistory(const std::string& path,
                                   const std::vector<voxel::VoxelRenderer::Block>& blocks,
                                   float block_size,
                                   const std::vector<TileDef>& tiles);

static TileDef MergeTileOverride(const TileDef& base, const TileDef& override_tile) {
    TileDef merged = base;
    const bool base_has_animation = !base.animation.empty();
    const bool override_has_animation = !override_tile.animation.empty();
    if (!override_tile.key.empty())
        merged.key = override_tile.key;
    if (!override_tile.name.empty())
        merged.name = override_tile.name;
    if (!override_tile.icon.empty())
        merged.icon = override_tile.icon;
    if (!override_tile.type.empty())
        merged.type = override_tile.type;
    if (!override_tile.material.empty())
        merged.material = override_tile.material;
    if (!override_tile.placement.empty())
        merged.placement = override_tile.placement;
    if (!override_tile.category.empty())
        merged.category = override_tile.category;
    if (!override_tile.animation.empty())
        merged.animation = override_tile.animation;
    if (!override_tile.model.empty() && (!base_has_animation || override_has_animation))
        merged.model = override_tile.model;
    if (!override_tile.texture.empty() && (!base_has_animation || override_has_animation))
        merged.texture = override_tile.texture;
    if (override_tile.height_cm != 60)
        merged.height_cm = override_tile.height_cm;
    if (override_tile.scale_percent != 100)
        merged.scale_percent = override_tile.scale_percent;
    if (override_tile.height_blocks != 1)
        merged.height_blocks = override_tile.height_blocks;
    return merged;
}

static const int kChunkSizeBlocks = 32;

struct GridBlock {
    int gx = 0;
    int gy = 0;
    int gz = 0;
    std::string key;
};

static std::string BuildBlockKeyForSave(const voxel::VoxelRenderer::Block& block,
                                        const std::map<std::string, bool>& symmetric_by_key) {
    std::string key = block.key.empty() ? "s" : block.key;
    int rot_x = (int)std::round(block.rot_x_deg / 90.0f);
    int rot_y = (int)std::round(block.rot_y_deg / 90.0f);
    int rot_z = (int)std::round(block.rot_z_deg / 90.0f);
    rot_x = ((rot_x % 4) + 4) % 4;
    rot_y = ((rot_y % 4) + 4) % 4;
    rot_z = ((rot_z % 4) + 4) % 4;
    std::map<std::string, bool>::const_iterator sym_it = symmetric_by_key.find(key);
    if (sym_it != symmetric_by_key.end() && sym_it->second) {
        rot_x = 0;
        rot_y = 0;
        rot_z = 0;
    }
    std::vector<std::string> rot_parts;
    if (rot_x != 0)
        rot_parts.push_back("x" + std::to_string(rot_x));
    if (rot_y != 0)
        rot_parts.push_back("y" + std::to_string(rot_y));
    if (rot_z != 0)
        rot_parts.push_back("z" + std::to_string(rot_z));
    if (!rot_parts.empty()) {
        key += ":";
        for (size_t ri = 0; ri < rot_parts.size(); ++ri) {
            key += rot_parts[ri];
            if (ri + 1 < rot_parts.size())
                key += ",";
        }
    }
    return key;
}

static std::vector<GridBlock> CollectGridBlocks(const std::vector<voxel::VoxelRenderer::Block>& blocks,
                                                float block_size,
                                                const std::vector<TileDef>& tiles) {
    std::map<std::string, bool> symmetric_by_key;
    for (size_t i = 0; i < tiles.size(); ++i)
        symmetric_by_key[tiles[i].key] = IsSymmetricTileModel(tiles[i].model);
    std::vector<GridBlock> out;
    out.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        GridBlock gb;
        gb.gx = (int)std::round(blocks[i].x / block_size - 0.5f);
        gb.gz = (int)std::round(blocks[i].z / block_size - 0.5f);
        gb.gy = (int)std::round(blocks[i].y / block_size - 0.5f);
        gb.key = BuildBlockKeyForSave(blocks[i], symmetric_by_key);
        out.push_back(gb);
    }
    return out;
}

struct DungeonBounds {
    bool has = false;
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    int min_z = 0;
    int max_z = 0;
};

static DungeonBounds ComputeBounds(const std::vector<GridBlock>& blocks) {
    DungeonBounds b;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (!b.has) {
            b.has = true;
            b.min_x = b.max_x = blocks[i].gx;
            b.min_y = b.max_y = blocks[i].gy;
            b.min_z = b.max_z = blocks[i].gz;
        } else {
            b.min_x = std::min(b.min_x, blocks[i].gx);
            b.max_x = std::max(b.max_x, blocks[i].gx);
            b.min_y = std::min(b.min_y, blocks[i].gy);
            b.max_y = std::max(b.max_y, blocks[i].gy);
            b.min_z = std::min(b.min_z, blocks[i].gz);
            b.max_z = std::max(b.max_z, blocks[i].gz);
        }
    }
    return b;
}

static bool NeedsChunking(const DungeonBounds& bounds) {
    if (!bounds.has)
        return false;
    int width = bounds.max_x - bounds.min_x + 1;
    int depth = bounds.max_z - bounds.min_z + 1;
    return width > kChunkSizeBlocks || depth > kChunkSizeBlocks;
}

static int FloorDiv(int value, int divisor) {
    int q = value / divisor;
    int r = value % divisor;
    if (r != 0 && ((r > 0) != (divisor > 0)))
        --q;
    return q;
}

struct ChunkCoord {
    int x = 0;
    int y = 0;
    int z = 0;
    bool operator<(const ChunkCoord& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
};

static std::string BuildChunkSml(const std::vector<GridBlock>& blocks) {
    std::map<int, std::map<std::pair<int, int>, std::string> > layers;
    for (size_t i = 0; i < blocks.size(); ++i) {
        layers[blocks[i].gy][std::make_pair(blocks[i].gx, blocks[i].gz)] = blocks[i].key;
    }

    std::ostringstream out;
    out << "Dungeon {\n";
    out << "    TileMap {\n";
    out << "        lines: \"\n";
    for (std::map<int, std::map<std::pair<int, int>, std::string> >::const_iterator it = layers.begin(); it != layers.end(); ++it) {
        out << "#" << it->first << "\n";
        for (int z = 0; z < kChunkSizeBlocks; ++z) {
            for (int x = 0; x < kChunkSizeBlocks; ++x) {
                std::map<std::pair<int, int>, std::string>::const_iterator cell = it->second.find(std::make_pair(x, z));
                const std::string value = (cell != it->second.end()) ? cell->second : ".";
                out << value;
                if (x < kChunkSizeBlocks - 1)
                    out << " ";
            }
            out << "\n";
        }
    }
    out << "        \"\n";
    out << "    }\n";
    out << "}\n";
    return out.str();
}

static std::string BuildChunkedDungeonSml(const std::vector<TileDef>& tiles) {
    std::ostringstream out;
    out << "Dungeon {\n";
    out << "    ChunkSize: " << kChunkSizeBlocks << "\n\n";
    if (!tiles.empty()) {
        out << "    Tiles {\n";
        for (size_t i = 0; i < tiles.size(); ++i) {
            const std::string model = tiles[i].model.empty() ? "block.glb" : tiles[i].model;
            out << "        Tile { key: \"" << tiles[i].key << "\"";
            const bool has_animation = !tiles[i].animation.empty();
            if (!tiles[i].texture.empty() && !has_animation)
                out << " texture: \"" << tiles[i].texture << "\"";
            out << " model: \"" << model << "\"";
            if (has_animation)
                out << " animation: \"" << tiles[i].animation << "\"";
            if (tiles[i].type != "block")
                out << " type: \"" << tiles[i].type << "\"";
            if (tiles[i].height_cm != 60)
                out << " height_cm: " << tiles[i].height_cm;
            if (tiles[i].scale_percent != 100)
                out << " scale_percent: " << tiles[i].scale_percent;
            out << " }\n";
        }
        out << "    }\n";
    }
    out << "}\n";
    return out.str();
}

static std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty())
        return b;
    char last = a[a.size() - 1];
    if (last == '/' || last == '\\')
        return a + b;
    return a + "/" + b;
}

static std::string SelectFolderDialog(const char* title, const std::string& default_path) {
#if defined(__APPLE__)
    return MacSelectFolder(title, default_path.empty() ? nullptr : default_path.c_str());
#else
    const char* selected = tinyfd_selectFolderDialog(title, default_path.empty() ? nullptr : default_path.c_str());
    if (!selected)
        return std::string();
    return std::string(selected);
#endif
}

static bool ParseChunkSize(const std::string& text, int* out_size) {
    if (!out_size)
        return false;
    class ChunkHandler : public sml::SmlHandler {
    public:
        int value = 0;
        bool found = false;
        void startElement(const std::string& name) override { (void)name; }
        void onProperty(const std::string& name, const sml::PropertyValue& value) override {
            if (name == "ChunkSize" && value.type == sml::PropertyValue::Int) {
                this->value = value.int_value;
                this->found = true;
            }
        }
        void endElement(const std::string& name) override { (void)name; }
    };

    ChunkHandler handler;
    try {
        sml::SmlSaxParser parser(text);
        parser.parse(handler);
    } catch (...) {
        return false;
    }
    if (handler.found)
        *out_size = handler.value;
    return handler.found;
}

static bool ParseChunkFilename(const std::string& name, int* out_x, int* out_y, int* out_z) {
    int x = 0, y = 0, z = 0;
    if (std::sscanf(name.c_str(), "dungeon_%d_%d_%d.sml", &x, &y, &z) == 3) {
        if (out_x) *out_x = x;
        if (out_y) *out_y = y;
        if (out_z) *out_z = z;
        return true;
    }
    return false;
}

static bool SaveDungeonChunked(const std::string& folder,
                               const std::vector<GridBlock>& grid_blocks,
                               const std::vector<TileDef>& tiles) {
    if (folder.empty())
        return false;
    if (!EnsureDir(folder))
        return false;

    const std::string dungeon_path = JoinPath(folder, "dungeon.sml");
    if (!WriteTextFile(dungeon_path, BuildChunkedDungeonSml(tiles)))
        return false;

    std::map<ChunkCoord, std::vector<GridBlock> > chunks;
    for (size_t i = 0; i < grid_blocks.size(); ++i) {
        ChunkCoord coord;
        coord.x = FloorDiv(grid_blocks[i].gx, kChunkSizeBlocks);
        coord.y = FloorDiv(grid_blocks[i].gy, kChunkSizeBlocks);
        coord.z = FloorDiv(grid_blocks[i].gz, kChunkSizeBlocks);

        GridBlock local = grid_blocks[i];
        local.gx = grid_blocks[i].gx - coord.x * kChunkSizeBlocks;
        local.gy = grid_blocks[i].gy - coord.y * kChunkSizeBlocks;
        local.gz = grid_blocks[i].gz - coord.z * kChunkSizeBlocks;
        chunks[coord].push_back(local);
    }

    for (std::map<ChunkCoord, std::vector<GridBlock> >::const_iterator it = chunks.begin(); it != chunks.end(); ++it) {
        const ChunkCoord& coord = it->first;
        std::ostringstream name;
        name << "dungeon_" << coord.x << "_" << coord.y << "_" << coord.z << ".sml";
        const std::string chunk_path = JoinPath(folder, name.str());
        if (!WriteTextFile(chunk_path, BuildChunkSml(it->second)))
            return false;
    }

    return true;
}

static bool SaveDungeonAuto(const std::string& path,
                            const std::vector<voxel::VoxelRenderer::Block>& blocks,
                            float block_size,
                            const std::vector<TileDef>& tiles) {
    if (path.empty())
        return false;
    std::vector<GridBlock> grid_blocks = CollectGridBlocks(blocks, block_size, tiles);
    DungeonBounds bounds = ComputeBounds(grid_blocks);
    if (!NeedsChunking(bounds)) {
        return SaveDungeonWithHistory(path, blocks, block_size, tiles);
    }
    const std::string folder = GetParentDir(path);
    return SaveDungeonChunked(folder, grid_blocks, tiles);
}

static void UpdateWindowTitle(GLFWwindow* window,
                              const std::string& base_title,
                              const std::string& file_path,
                              bool dirty) {
    std::string file_name = GetFileName(file_path);
    if (file_name.empty())
        file_name = "untitled.sml";
    std::string title = base_title + " - " + file_name;
    if (dirty)
        title += "*";
    glfwSetWindowTitle(window, title.c_str());
}

struct AppState {
    bool has_pos = false;
    bool has_size = false;
    bool maximized = false;
    int pos_x = 0;
    int pos_y = 0;
    int size_x = 0;
    int size_y = 0;
    std::string last_file_path;
};

static std::string GetStatePath(const std::string& persist) {
    if (persist == "project")
        return "RaidBuilder/state_project.cfg";
    if (persist == "session")
        return "/tmp/raidbuilder_state.cfg";
    return GetStateDir() + "/state_user.cfg";
}

static bool ParseHexColor(const std::string& text, ImVec4* out) {
    if (!out)
        return false;
    std::string s = text;
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0)
        s = s.substr(2);
    if (!s.empty() && s[0] == '#')
        s = s.substr(1);
    if (s.size() != 6 && s.size() != 8)
        return false;
    unsigned int value = 0;
    std::stringstream ss;
    ss << std::hex << s;
    ss >> value;
    unsigned int a = 0xFF;
    unsigned int r = 0, g = 0, b = 0;
    if (s.size() == 8) {
        a = (value >> 24) & 0xFF;
        r = (value >> 16) & 0xFF;
        g = (value >> 8) & 0xFF;
        b = value & 0xFF;
    } else {
        r = (value >> 16) & 0xFF;
        g = (value >> 8) & 0xFF;
        b = value & 0xFF;
    }
    out->x = (float)r / 255.0f;
    out->y = (float)g / 255.0f;
    out->z = (float)b / 255.0f;
    out->w = (float)a / 255.0f;
    return true;
}

static bool LoadThemeFile(const std::string& path, smlui::UiTheme* out_theme) {
    if (!out_theme)
        return false;
    std::string text;
    if (!LoadFileText(path.c_str(), &text))
        return false;
    class ThemeHandler : public sml::SmlHandler {
    public:
        smlui::UiTheme* theme;
        std::vector<std::string> stack;
        explicit ThemeHandler(smlui::UiTheme* t) : theme(t) {}
        void startElement(const std::string& name) override { stack.push_back(name); }
        void onProperty(const std::string& name, const sml::PropertyValue& value) override {
            if (stack.empty() || stack.back() != "Theme" || value.type != sml::PropertyValue::String)
                return;
            ImVec4 color;
            if (!ParseHexColor(value.string_value, &color))
                return;
            if (name == "toolbarBg") theme->toolbar_bg = color;
            else if (name == "statusBg") theme->status_bg = color;
            else if (name == "statusText") theme->status_text = color;
            else if (name == "leftBg") theme->left_bg = color;
            else if (name == "rightBg") theme->right_bg = color;
            else if (name == "centerBg") theme->center_bg = color;
        }
        void endElement(const std::string& name) override { (void)name; if (!stack.empty()) stack.pop_back(); }
    };
    ThemeHandler handler(out_theme);
    try {
        sml::SmlSaxParser parser(text);
        parser.parse(handler);
    } catch (...) {
        return false;
    }
    return true;
}

static std::string ResolveThemePath(const std::string& theme_name) {
    if (theme_name.empty())
        return "";
    if (theme_name.find('/') != std::string::npos || theme_name.find('\\') != std::string::npos)
        return theme_name;
    return std::string("RaidBuilder/themes/") + theme_name + ".sml";
}

static bool LoadAppState(const std::string& path, AppState* out_state) {
    EnsureDir(GetParentDir(path));
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key == "pos") {
            std::sscanf(value.c_str(), "%d,%d", &out_state->pos_x, &out_state->pos_y);
            out_state->has_pos = true;
        } else if (key == "size") {
            std::sscanf(value.c_str(), "%d,%d", &out_state->size_x, &out_state->size_y);
            out_state->has_size = true;
        } else if (key == "maximized") {
            out_state->maximized = (value == "1");
        } else if (key == "lastFilePath") {
            out_state->last_file_path = value;
        }
    }
    return true;
}

static void SaveAppState(const std::string& path, const AppState& state) {
    EnsureDir(GetParentDir(path));
    std::ofstream file(path.c_str(), std::ios::trunc);
    if (!file.is_open())
        return;
    if (state.has_pos)
        file << "pos=" << state.pos_x << "," << state.pos_y << "\n";
    if (state.has_size)
        file << "size=" << state.size_x << "," << state.size_y << "\n";
    file << "maximized=" << (state.maximized ? "1" : "0") << "\n";
    if (!state.last_file_path.empty())
        file << "lastFilePath=" << state.last_file_path << "\n";
}

using ::TileDef;

// Forward declarations for helpers used ahead of their definitions.
static std::string ResolveRepoDir(const std::string& rel);
static std::string ResolveWorkspacePath(const std::string& rel);

static std::string GetActionBarPath() {
    return GetStateDir() + "/actionbar.cfg";
}

enum MenuActionId {
    MENU_ACTION_OPEN = 1001,
    MENU_ACTION_SAVE = 1002,
    MENU_ACTION_SAVE_AS = 1003,
    MENU_ACTION_CLOSE_QUERY = 1099
};

static void LoadActionBar(const std::string& path,
                          const std::map<std::string, int>& tile_index_by_key,
                          std::vector<int>* action_slots,
                          int* active_slot) {
    if (!action_slots)
        return;
    EnsureDir(GetParentDir(path));
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key == "activeSlot" && active_slot) {
            *active_slot = std::atoi(value.c_str());
            continue;
        }
        if (key.rfind("slot", 0) != 0)
            continue;
        int slot = std::atoi(key.c_str() + 4);
        if (slot < 0 || slot >= (int)action_slots->size())
            continue;
        std::map<std::string, int>::const_iterator it = tile_index_by_key.find(value);
        if (it != tile_index_by_key.end())
            (*action_slots)[slot] = it->second;
    }
}

static void SaveActionBar(const std::string& path,
                          const std::vector<int>& action_slots,
                          const std::vector<TileDef>& tiles,
                          int active_slot) {
    EnsureDir(GetParentDir(path));
    std::ofstream file(path.c_str(), std::ios::trunc);
    if (!file.is_open())
        return;
    file << "activeSlot=" << active_slot << "\n";
    for (size_t i = 0; i < action_slots.size(); ++i) {
        int idx = action_slots[i];
        std::string key = "";
        if (idx >= 0 && idx < (int)tiles.size())
            key = tiles[idx].key;
        file << "slot" << i << "=" << key << "\n";
    }
}


static std::string BuildDungeonSml(const std::vector<voxel::VoxelRenderer::Block>& blocks,
                                   float block_size,
                                   const std::vector<TileDef>& tiles) {
    if (blocks.empty())
        return "Dungeon {\n}\n";

    struct Bounds {
        bool has = false;
        int min_x = 0;
        int max_x = 0;
        int min_z = 0;
        int max_z = 0;
    };

    std::map<int, std::map<std::pair<int, int>, std::string> > layers;
    std::map<int, Bounds> bounds;
    Bounds global_bounds;
    std::map<std::string, bool> symmetric_by_key;
    for (size_t i = 0; i < tiles.size(); ++i)
        symmetric_by_key[tiles[i].key] = IsSymmetricTileModel(tiles[i].model);

    for (size_t i = 0; i < blocks.size(); ++i) {
        // Blocks are stored at grid centers ((cell + 0.5) * block_size).
        // Subtract the half-cell offset when converting back to integer cells.
        int gx = (int)std::round(blocks[i].x / block_size - 0.5f);
        int gz = (int)std::round(blocks[i].z / block_size - 0.5f);
        int layer = (int)std::round(blocks[i].y / block_size - 0.5f);
        std::string key = blocks[i].key.empty() ? "s" : blocks[i].key;
        int rot_x = (int)std::round(blocks[i].rot_x_deg / 90.0f);
        int rot_y = (int)std::round(blocks[i].rot_y_deg / 90.0f);
        int rot_z = (int)std::round(blocks[i].rot_z_deg / 90.0f);
        rot_x = ((rot_x % 4) + 4) % 4;
        rot_y = ((rot_y % 4) + 4) % 4;
        rot_z = ((rot_z % 4) + 4) % 4;
        std::map<std::string, bool>::const_iterator sym_it = symmetric_by_key.find(key);
        if (sym_it != symmetric_by_key.end() && sym_it->second) {
            rot_x = 0;
            rot_y = 0;
            rot_z = 0;
        }
        std::vector<std::string> rot_parts;
        if (rot_x != 0)
            rot_parts.push_back("x" + std::to_string(rot_x));
        if (rot_y != 0)
            rot_parts.push_back("y" + std::to_string(rot_y));
        if (rot_z != 0)
            rot_parts.push_back("z" + std::to_string(rot_z));
        if (!rot_parts.empty()) {
            key += ":";
            for (size_t ri = 0; ri < rot_parts.size(); ++ri) {
                key += rot_parts[ri];
                if (ri + 1 < rot_parts.size())
                    key += ",";
            }
        }
        layers[layer][std::make_pair(gx, gz)] = key;
        Bounds& b = bounds[layer];
        if (!b.has) {
            b.has = true;
            b.min_x = b.max_x = gx;
            b.min_z = b.max_z = gz;
        } else {
            b.min_x = std::min(b.min_x, gx);
            b.max_x = std::max(b.max_x, gx);
            b.min_z = std::min(b.min_z, gz);
            b.max_z = std::max(b.max_z, gz);
        }
        if (!global_bounds.has) {
            global_bounds.has = true;
            global_bounds.min_x = global_bounds.max_x = gx;
            global_bounds.min_z = global_bounds.max_z = gz;
        } else {
            global_bounds.min_x = std::min(global_bounds.min_x, gx);
            global_bounds.max_x = std::max(global_bounds.max_x, gx);
            global_bounds.min_z = std::min(global_bounds.min_z, gz);
            global_bounds.max_z = std::max(global_bounds.max_z, gz);
        }
    }

    std::ostringstream out;
    out << "Dungeon {\n";
    out << "    TileMap {\n";
    out << "        lines: \"\n";
    for (std::map<int, std::map<std::pair<int, int>, std::string> >::const_iterator it = layers.begin(); it != layers.end(); ++it) {
        int layer = it->first;
        out << "#" << layer << "\n";
        for (int z = global_bounds.min_z; z <= global_bounds.max_z; ++z) {
            for (int x = global_bounds.min_x; x <= global_bounds.max_x; ++x) {
                std::map<std::pair<int, int>, std::string>::const_iterator cell = it->second.find(std::make_pair(x, z));
                const std::string value = (cell != it->second.end()) ? cell->second : ".";
                out << value;
                if (x < global_bounds.max_x)
                    out << " ";
            }
            out << "\n";
        }
    }
    out << "        \"\n";
    out << "    }\n\n";
    if (!tiles.empty()) {
        out << "    Tiles {\n";
        for (size_t i = 0; i < tiles.size(); ++i) {
            const std::string model = tiles[i].model.empty() ? "block.glb" : tiles[i].model;
            out << "        Tile { key: \"" << tiles[i].key << "\"";
            const bool has_animation = !tiles[i].animation.empty();
            if (!tiles[i].texture.empty() && !has_animation)
                out << " texture: \"" << tiles[i].texture << "\"";
            out << " model: \"" << model << "\"";
            if (has_animation)
                out << " animation: \"" << tiles[i].animation << "\"";
            if (tiles[i].type != "block")
                out << " type: \"" << tiles[i].type << "\"";
            if (tiles[i].height_cm != 60)
                out << " height_cm: " << tiles[i].height_cm;
            if (tiles[i].scale_percent != 100)
                out << " scale_percent: " << tiles[i].scale_percent;
            out << " }\n";
        }
        out << "    }\n\n";
    }
    out << "}\n";
    return out.str();
}

static bool SaveDungeonWithHistory(const std::string& path,
                                   const std::vector<voxel::VoxelRenderer::Block>& blocks,
                                   float block_size,
                                   const std::vector<TileDef>& tiles) {
    if (path.empty())
        return false;
    if (FileExists(path)) {
        std::string history_dir = GetHistoryDir();
        if (EnsureDir(history_dir)) {
            std::string base_name = GetFileBaseName(path);
            std::string history_path = NextHistoryPath(history_dir, base_name);
            if (std::rename(path.c_str(), history_path.c_str()) != 0) {
                if (CopyFile(path, history_path))
                    std::remove(path.c_str());
            }
        }
    }

    std::ofstream out(path.c_str(), std::ios::trunc);
    if (!out.is_open())
        return false;
    out << BuildDungeonSml(blocks, block_size, tiles);
    return true;
}

static std::string ResolveAssetPath(const std::string& path, const char* prefix) {
    if (path.empty())
        return path;
    if (path[0] == '/' || path[0] == '.')
        return path;
    std::string prefix_str(prefix);
    if (path.compare(0, prefix_str.size(), prefix_str) == 0)
        return path;
    return prefix_str + path;
}

static std::string GetExecutableDir() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(&buf[0], &size) != 0)
        return ".";
    buf.resize(std::strlen(buf.c_str()));
    char real_path[PATH_MAX];
    if (realpath(buf.c_str(), real_path))
        buf = real_path;
    size_t slash = buf.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    return buf.substr(0, slash);
#else
    return ".";
#endif
}

static std::string ResolveRepoPath(const std::string& rel) {
    std::string exe_dir = GetExecutableDir();
    // First try app bundle resources (macOS packaging).
#if defined(__APPLE__)
    std::string bundle_candidate = exe_dir + "/../Resources/RaidBuilder/" + rel;
    if (FileExists(bundle_candidate))
        return bundle_candidate;
#endif
    std::string dir = exe_dir;
    for (int i = 0; i < 6; ++i) {
        std::string candidate = dir + "/RaidBuilder/" + rel;
        if (FileExists(candidate))
            return candidate;
        size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos)
            break;
        dir = dir.substr(0, slash);
    }
    return "RaidBuilder/" + rel;
}

static std::string ResolveRepoDir(const std::string& rel) {
    std::string exe_dir = GetExecutableDir();
#if defined(__APPLE__)
    std::string bundle_candidate = exe_dir + "/../Resources/RaidBuilder/" + rel;
    if (DirExists(bundle_candidate))
        return bundle_candidate;
#endif
    std::string dir = exe_dir;
    for (int i = 0; i < 6; ++i) {
        std::string candidate = dir + "/RaidBuilder/" + rel;
        if (DirExists(candidate))
            return candidate;
        size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos)
            break;
        dir = dir.substr(0, slash);
    }
    std::string fallback = "RaidBuilder/" + rel;
    return DirExists(fallback) ? fallback : "";
}

static std::string ResolveWorkspacePath(const std::string& rel) {
    if (DirExists("RaidBuilder")) {
        if (rel.empty())
            return ".";
        return std::string("./") + rel;
    }
    std::string exe_dir = GetExecutableDir();
    std::string dir = exe_dir;
    for (int i = 0; i < 8; ++i) {
        std::string marker = dir + "/RaidBuilder";
        if (DirExists(marker)) {
            if (rel.empty())
                return dir;
            return dir + "/" + rel;
        }
        size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos)
            break;
        dir = dir.substr(0, slash);
    }
    return rel;
}


static bool EndsWith(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

static bool IsSymmetricTileModel(const std::string& model_in) {
    std::string model = model_in;
    size_t hash = model.find('#');
    if (hash != std::string::npos)
        model = model.substr(0, hash);
    if (model.empty())
        model = "block.glb";
    if (model.compare(0, 8, "texture:") == 0)
        model = "block.glb";
    const std::string prefix = "res://";
    if (model.compare(0, prefix.size(), prefix) == 0)
        model = model.substr(prefix.size());
    if (model.empty())
        model = "block.glb";
    return model == "block.glb" || EndsWith(model, "/block.glb") || EndsWith(model, "\\block.glb");
}

static bool ParseRotationSuffix(const std::string& suffix,
                                voxel::VoxelRenderer::Block* block,
                                std::string* error_message) {
    if (!block)
        return false;
    if (suffix.empty())
        return true;

    auto set_quarter_turns = [&](char axis, int turns) {
        int norm = ((turns % 4) + 4) % 4;
        float deg = (float)(norm * 90);
        if (axis == 'x' || axis == 'X')
            block->rot_x_deg = deg;
        else if (axis == 'y' || axis == 'Y')
            block->rot_y_deg = deg;
        else if (axis == 'z' || axis == 'Z')
            block->rot_z_deg = deg;
    };

    auto parse_axis_value = [&](char axis, const std::string& value_text) -> bool {
        if (value_text.empty()) {
            if (error_message)
                *error_message = std::string("Missing rotation value for axis '") + axis + "'";
            return false;
        }
        for (size_t i = 0; i < value_text.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(value_text[i]))) {
                if (error_message)
                    *error_message = std::string("Invalid rotation value '") + value_text + "'";
                return false;
            }
        }
        int value = std::atoi(value_text.c_str());
        if (value >= 0 && value <= 3) {
            set_quarter_turns(axis, value);
            return true;
        }
        if (value == 0 || value == 90 || value == 180 || value == 270) {
            set_quarter_turns(axis, value / 90);
            return true;
        }
        if (error_message)
            *error_message = std::string("Rotation must be 0..3 or 0/90/180/270, got '") + value_text + "'";
        return false;
    };

    // Allow both comma-separated (x1,y3) and compact (x1y3z0).
    std::string expanded = suffix;
    for (size_t i = 0; i < expanded.size(); ++i) {
        char c = expanded[i];
        if ((c == 'x' || c == 'X' || c == 'y' || c == 'Y' || c == 'z' || c == 'Z') && i > 0 && expanded[i - 1] != ',')
            expanded.insert(i++, ",");
    }

    std::istringstream rot_stream(expanded);
    std::string part;
    while (std::getline(rot_stream, part, ',')) {
        if (part.empty())
            continue;
        char axis = part[0];
        if (axis != 'x' && axis != 'X' && axis != 'y' && axis != 'Y' && axis != 'z' && axis != 'Z') {
            if (error_message)
                *error_message = std::string("Unknown rotation axis in '") + part + "'";
            return false;
        }
        std::string value_text = part.substr(1);
        if (!parse_axis_value(axis, value_text))
            return false;
    }

    return true;
}

static int ComputeHeightBlocks(int height_cm, int scale_percent, int block_cm) {
    if (height_cm <= 0)
        height_cm = block_cm;
    if (scale_percent <= 0)
        scale_percent = 100;
    int eff_cm = height_cm * scale_percent;
    int denom = block_cm * 100;
    if (denom <= 0)
        denom = 1;
    return (eff_cm + denom - 1) / denom;
}

struct SpawnPoint {
    bool valid = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

static bool ParseDungeon(const std::string& text,
                         float block_size,
                         std::vector<voxel::VoxelRenderer::Block>* out_blocks,
                         std::vector<TileDef>* out_tiles,
                         SpawnPoint* out_spawn,
                         std::string* error_message) {
    class DungeonHandler : public sml::SmlHandler {
    public:
        std::string lines;
        std::vector<std::string> stack;
        TileDef tile;
        std::vector<TileDef> tiles;

        void startElement(const std::string& name) override { stack.push_back(name); }
        void onProperty(const std::string& name, const sml::PropertyValue& value) override {
            if (stack.empty())
                return;
            if (stack.back() == "TileMap" && name == "lines" && value.type == sml::PropertyValue::String)
                lines = value.string_value;
            if (stack.back() == "Tile" && name == "key" && value.type == sml::PropertyValue::String)
                tile.key = value.string_value;
            if (stack.back() == "Tile" && name == "name" && value.type == sml::PropertyValue::String)
                tile.name = value.string_value;
            if (stack.back() == "Tile" && name == "icon" && value.type == sml::PropertyValue::String)
                tile.icon = value.string_value;
            if (stack.back() == "Tile" && name == "texture" && value.type == sml::PropertyValue::String)
                tile.texture = value.string_value;
            if (stack.back() == "Tile" && name == "model" && value.type == sml::PropertyValue::String)
                tile.model = value.string_value;
            if (stack.back() == "Tile" && name == "animation" && value.type == sml::PropertyValue::String)
                tile.animation = value.string_value;
            if (stack.back() == "Tile" && name == "type" && value.type == sml::PropertyValue::String)
                tile.type = value.string_value;
            if (stack.back() == "Tile" && name == "material" && value.type == sml::PropertyValue::EnumType)
                tile.material = value.string_value;
            if (stack.back() == "Tile" && name == "placement" && value.type == sml::PropertyValue::EnumType)
                tile.placement = value.string_value;
            if (stack.back() == "Tile" && name == "height_cm" && value.type == sml::PropertyValue::Int)
                tile.height_cm = value.int_value;
            if (stack.back() == "Tile" && name == "scale_percent" && value.type == sml::PropertyValue::Int)
                tile.scale_percent = value.int_value;
        }
        void endElement(const std::string& name) override {
            if (name == "Tile") {
                if (!tile.key.empty()) {
                    tile.height_blocks = ComputeHeightBlocks(tile.height_cm, tile.scale_percent, 60);
                    tiles.push_back(tile);
                }
                tile = TileDef();
            }
            if (!stack.empty())
                stack.pop_back();
        }
    };

    DungeonHandler handler;
    try {
        sml::SmlSaxParser parser(text);
        parser.registerEnumValue("material", "texture");
        parser.registerEnumValue("material", "vertex");
        parser.registerEnumValue("placement", "ground");
        parser.registerEnumValue("placement", "wall");
        parser.registerEnumValue("placement", "ceiling");
        parser.parse(handler);
    } catch (const sml::SmlParseException& e) {
        if (error_message)
            *error_message = e.what();
        return false;
    }

    if (out_tiles)
        *out_tiles = handler.tiles;
    if (out_spawn)
        *out_spawn = SpawnPoint();

    if (handler.lines.empty()) {
        if (out_blocks)
            out_blocks->clear();
        if (out_spawn)
            out_spawn->valid = false;
        if (error_message)
            *error_message = "Spawn marker error: expected 1 spawn tile, found 0";
        return true;
    }

    std::vector<voxel::VoxelRenderer::Block> blocks;
    std::istringstream iss(handler.lines);
    std::string line;
    int current_layer = 0;
    int max_cols = 0;
    int max_rows = 0;

    std::vector<std::vector<std::vector<std::string> > > layer_rows;
    layer_rows.resize(1);

    std::map<std::string, int> tex_index_map;
    if (!handler.tiles.empty()) {
        for (size_t i = 0; i < handler.tiles.size(); ++i)
            tex_index_map[handler.tiles[i].key] = (int)i;
    }

    while (std::getline(iss, line)) {
        if (line.empty())
            continue;
        if (line.size() > 1 && line[0] == '#') {
            current_layer = std::atoi(line.c_str() + 1);
            if ((int)layer_rows.size() <= current_layer)
                layer_rows.resize(current_layer + 1);
            continue;
        }

        std::istringstream row_stream(line);
        std::vector<std::string> tokens;
        std::string token;
        while (row_stream >> token)
            tokens.push_back(token);
        if (!tokens.empty()) {
            if ((int)tokens.size() > max_cols)
                max_cols = (int)tokens.size();
            layer_rows[current_layer].push_back(tokens);
        }
    }

    std::vector<int> row_counts(layer_rows.size(), 0);
    const float offset_x = block_size * 0.5f;
    const float offset_z = block_size * 0.5f;
    size_t spawn_count = 0;
    for (size_t layer = 0; layer < layer_rows.size(); ++layer) {
        const std::vector<std::vector<std::string> >& rows = layer_rows[layer];
        for (size_t r = 0; r < rows.size(); ++r) {
            const std::vector<std::string>& tokens = rows[r];
            for (int col = 0; col < max_cols; ++col) {
                std::string raw = (col < (int)tokens.size()) ? tokens[col] : ".";
                std::string id = raw;
                std::string suffix;
                size_t colon = raw.find(':');
                if (colon != std::string::npos) {
                    id = raw.substr(0, colon);
                    suffix = raw.substr(colon + 1);
                }
                if (id == "S") {
                    spawn_count += 1;
                    if (out_spawn) {
                        out_spawn->x = col * block_size + offset_x;
                        out_spawn->y = (float)layer * block_size + block_size * 0.5f;
                        out_spawn->z = row_counts[layer] * block_size + offset_z;
                    }
                }
                if (!handler.tiles.empty() && tex_index_map.find(id) == tex_index_map.end())
                    continue;
                if (id != ".") {
                    voxel::VoxelRenderer::Block block;
                    block.x = (float)col;
                    block.y = (float)layer;
                    block.z = (float)row_counts[layer];
                    block.key = id;
                    if (!suffix.empty()) {
                        if (!ParseRotationSuffix(suffix, &block, error_message)) {
                            if (error_message && error_message->empty())
                                *error_message = std::string("Invalid rotation suffix '") + suffix + "'";
                            return false;
                        }
                    }
                    block.tex_index = tex_index_map.empty() ? 0 : tex_index_map[id];
                    blocks.push_back(block);
                }
            }
            row_counts[layer] += 1;
        }
    }

    for (size_t i = 0; i < row_counts.size(); ++i)
        if (row_counts[i] > max_rows)
            max_rows = row_counts[i];

    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].x = blocks[i].x * block_size + offset_x;
        blocks[i].z = blocks[i].z * block_size + offset_z;
        blocks[i].y = blocks[i].y * block_size + block_size * 0.5f;
    }

    if (out_spawn)
        out_spawn->valid = (spawn_count == 1);
    if (spawn_count != 1) {
        if (error_message)
            *error_message = std::string("Spawn marker error: expected 1 spawn tile, found ") + std::to_string(spawn_count);
    }

    *out_blocks = blocks;
    return true;
}

struct ChunkFileInfo {
    int x = 0;
    int y = 0;
    int z = 0;
    std::string path;
};

static std::vector<ChunkFileInfo> CollectChunkFiles(const std::string& folder) {
    std::vector<ChunkFileInfo> files;
#if defined(_WIN32)
    std::string pattern = JoinPath(folder, "dungeon_*.sml");
    _finddata_t data;
    intptr_t handle = _findfirst(pattern.c_str(), &data);
    if (handle == -1)
        return files;
    do {
        if (data.name[0] == '.')
            continue;
        int cx = 0, cy = 0, cz = 0;
        if (ParseChunkFilename(data.name, &cx, &cy, &cz)) {
            ChunkFileInfo info;
            info.x = cx;
            info.y = cy;
            info.z = cz;
            info.path = JoinPath(folder, data.name);
            files.push_back(info);
        }
    } while (_findnext(handle, &data) == 0);
    _findclose(handle);
#else
    DIR* dir = opendir(folder.c_str());
    if (!dir)
        return files;
    dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (!entry->d_name || entry->d_name[0] == '.')
            continue;
        std::string name(entry->d_name);
        int cx = 0, cy = 0, cz = 0;
        if (ParseChunkFilename(name, &cx, &cy, &cz)) {
            ChunkFileInfo info;
            info.x = cx;
            info.y = cy;
            info.z = cz;
            info.path = JoinPath(folder, name);
            files.push_back(info);
        }
    }
    closedir(dir);
#endif
    std::sort(files.begin(), files.end(), [](const ChunkFileInfo& a, const ChunkFileInfo& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    return files;
}

static bool LoadChunkedDungeon(const std::string& folder,
                               int chunk_size_blocks,
                               float block_size,
                               std::vector<voxel::VoxelRenderer::Block>* out_blocks,
                               SpawnPoint* out_spawn,
                               std::string* error_message) {
    if (!out_blocks)
        return false;
    out_blocks->clear();
    if (out_spawn)
        *out_spawn = SpawnPoint();

    std::vector<ChunkFileInfo> files = CollectChunkFiles(folder);
    if (files.empty()) {
        if (error_message)
            *error_message = "No chunk files (dungeon_*.sml) found in " + folder;
        return false;
    }

    const bool debug_chunks = IsDebugEnabled("CHUNK_DEBUG");
    if (debug_chunks) {
        fprintf(stdout, "Chunk load order (%zu total):\n", files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            fprintf(stdout, "chunk[%zu] = (%d,%d,%d) %s\n",
                    i, files[i].x, files[i].y, files[i].z, files[i].path.c_str());
        }
    }

    bool spawn_found = false;
    for (size_t i = 0; i < files.size(); ++i) {
        std::string chunk_text;
        if (!LoadFileText(files[i].path.c_str(), &chunk_text)) {
            if (error_message)
                *error_message = "Dungeon load error: could not read " + files[i].path;
            return false;
        }
        std::vector<voxel::VoxelRenderer::Block> chunk_blocks;
        std::vector<TileDef> chunk_tiles;
        SpawnPoint chunk_spawn;
        std::string chunk_error;
        if (!ParseDungeon(chunk_text, block_size, &chunk_blocks, &chunk_tiles, &chunk_spawn, &chunk_error)) {
            if (error_message)
                *error_message = chunk_error.empty() ? "Chunk parse error" : chunk_error;
            return false;
        }
        const float offset_x = (float)(files[i].x * chunk_size_blocks) * block_size;
        const float offset_y = (float)(files[i].y * chunk_size_blocks) * block_size;
        const float offset_z = (float)(files[i].z * chunk_size_blocks) * block_size;
        for (size_t bi = 0; bi < chunk_blocks.size(); ++bi) {
            chunk_blocks[bi].x += offset_x;
            chunk_blocks[bi].y += offset_y;
            chunk_blocks[bi].z += offset_z;
            out_blocks->push_back(chunk_blocks[bi]);
        }
        if (out_spawn && chunk_spawn.valid && !spawn_found) {
            chunk_spawn.x += offset_x;
            chunk_spawn.y += offset_y;
            chunk_spawn.z += offset_z;
            *out_spawn = chunk_spawn;
            spawn_found = true;
        }
    }

    if (out_spawn && !spawn_found)
        out_spawn->valid = false;
    return true;
}

static bool LoadDungeonFromPath(const std::string& path,
                                float block_size,
                                std::vector<voxel::VoxelRenderer::Block>* out_blocks,
                                std::vector<TileDef>* out_tiles,
                                SpawnPoint* out_spawn,
                                std::string* error_message) {
    std::string text;
    if (!LoadFileText(path.c_str(), &text)) {
        if (error_message)
            *error_message = std::string("Dungeon load error: could not read ") + path;
        return false;
    }

    int chunk_size = 0;
    const bool has_chunk = ParseChunkSize(text, &chunk_size);
    std::vector<voxel::VoxelRenderer::Block> base_blocks;
    std::vector<TileDef> base_tiles;
    SpawnPoint base_spawn;
    std::string parse_error;
    if (!ParseDungeon(text, block_size, &base_blocks, &base_tiles, &base_spawn, &parse_error)) {
        if (error_message)
            *error_message = parse_error;
        return false;
    }

    if (out_tiles)
        *out_tiles = base_tiles;

    if (!has_chunk) {
        if (out_blocks)
            *out_blocks = base_blocks;
        if (out_spawn)
            *out_spawn = base_spawn;
        if (error_message)
            *error_message = parse_error;
        return true;
    }

    const int effective_chunk = (chunk_size > 0) ? chunk_size : kChunkSizeBlocks;
    std::vector<voxel::VoxelRenderer::Block> chunk_blocks;
    SpawnPoint chunk_spawn;
    std::string chunk_error;
    if (!LoadChunkedDungeon(GetParentDir(path), effective_chunk, block_size, &chunk_blocks, &chunk_spawn, &chunk_error)) {
        if (error_message)
            *error_message = chunk_error;
        return false;
    }

    if (out_blocks)
        *out_blocks = chunk_blocks;
    if (out_spawn)
        *out_spawn = chunk_spawn;
    if (error_message)
        error_message->clear();
    return true;
}

struct RaycastHit {
    bool hit;
    float t;
    int block_index;
    float nx;
    float ny;
    float nz;
    float hit_x;
    float hit_y;
    float hit_z;
    bool ground;
};

static float SnapToGridCenter(float v, float block_size) {
    float cell = std::floor(v / block_size);
    return (cell + 0.5f) * block_size;
}

struct Mat3 {
    // Column-major to match the renderer's Mat4 conventions.
    float m[9];
};

static Mat3 Mat3Identity() {
    Mat3 r = {};
    r.m[0] = 1.0f;
    r.m[4] = 1.0f;
    r.m[8] = 1.0f;
    return r;
}

static Mat3 Mat3Multiply(const Mat3& a, const Mat3& b) {
    Mat3 r = {};
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            r.m[col * 3 + row] =
                a.m[0 * 3 + row] * b.m[col * 3 + 0] +
                a.m[1 * 3 + row] * b.m[col * 3 + 1] +
                a.m[2 * 3 + row] * b.m[col * 3 + 2];
        }
    }
    return r;
}

static Mat3 Mat3Transpose(const Mat3& a) {
    Mat3 r = {};
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row)
            r.m[col * 3 + row] = a.m[row * 3 + col];
    return r;
}

static void Mat3MulVec(const Mat3& m, const float v[3], float out[3]) {
    for (int row = 0; row < 3; ++row) {
        out[row] =
            m.m[0 * 3 + row] * v[0] +
            m.m[1 * 3 + row] * v[1] +
            m.m[2 * 3 + row] * v[2];
    }
}

static Mat3 Mat3RotateX(float radians) {
    Mat3 r = Mat3Identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    r.m[4] = c;
    r.m[5] = s;
    r.m[7] = -s;
    r.m[8] = c;
    return r;
}

static Mat3 Mat3RotateY(float radians) {
    Mat3 r = Mat3Identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    r.m[0] = c;
    r.m[2] = -s;
    r.m[6] = s;
    r.m[8] = c;
    return r;
}

static Mat3 Mat3RotateZ(float radians) {
    Mat3 r = Mat3Identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    r.m[0] = c;
    r.m[1] = s;
    r.m[3] = -s;
    r.m[4] = c;
    return r;
}

static float ToRadians(float degrees) {
    return degrees * 0.01745329252f;
}

static Mat3 BlockRotationMatrix(const voxel::VoxelRenderer::Block& block) {
    const Mat3 rot_x = Mat3RotateX(ToRadians(block.rot_x_deg));
    const Mat3 rot_y = Mat3RotateY(ToRadians(block.rot_y_deg));
    const Mat3 rot_z = Mat3RotateZ(ToRadians(block.rot_z_deg));
    // Match the renderer: R = rot_y * (rot_x * rot_z)
    return Mat3Multiply(rot_y, Mat3Multiply(rot_x, rot_z));
}

struct Vec3i;
struct DiceOrientation;

static DiceOrientation OrientationFromBlock(const voxel::VoxelRenderer::Block& block);
static void ApplyWorldRotation90(DiceOrientation* o, raidbuilder::AxisId axis, int dir);

struct Vec3i {
    int x = 0;
    int y = 0;
    int z = 0;
};

static Vec3i MakeVec3i(int x, int y, int z) {
    Vec3i v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static Vec3i NegateVec3i(const Vec3i& v) {
    return MakeVec3i(-v.x, -v.y, -v.z);
}

static int DotVec3i(const Vec3i& a, const Vec3i& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3i CrossVec3i(const Vec3i& a, const Vec3i& b) {
    return MakeVec3i(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

static bool EqualVec3i(const Vec3i& a, const Vec3i& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static Vec3i SnapAxis(const float v[3]) {
    int axis = 0;
    float best = std::fabs(v[0]);
    if (std::fabs(v[1]) > best) {
        axis = 1;
        best = std::fabs(v[1]);
    }
    if (std::fabs(v[2]) > best)
        axis = 2;
    float sign = (v[axis] >= 0.0f) ? 1.0f : -1.0f;
    if (axis == 0)
        return MakeVec3i((int)sign, 0, 0);
    if (axis == 1)
        return MakeVec3i(0, (int)sign, 0);
    return MakeVec3i(0, 0, (int)sign);
}

static Vec3i RotateVecAroundX90(const Vec3i& v, int dir) {
    // dir = +1 -> +90deg, dir = -1 -> -90deg
    if (dir >= 0)
        return MakeVec3i(v.x, -v.z, v.y);
    return MakeVec3i(v.x, v.z, -v.y);
}

static Vec3i RotateVecAroundY90(const Vec3i& v, int dir) {
    if (dir >= 0)
        return MakeVec3i(v.z, v.y, -v.x);
    return MakeVec3i(-v.z, v.y, v.x);
}

static Vec3i RotateVecAroundZ90(const Vec3i& v, int dir) {
    if (dir >= 0)
        return MakeVec3i(-v.y, v.x, v.z);
    return MakeVec3i(v.y, -v.x, v.z);
}

static Vec3i RotateVecAroundAxis90(const Vec3i& v, raidbuilder::AxisId axis, int dir) {
    if (axis == raidbuilder::AxisId::X)
        return RotateVecAroundX90(v, dir);
    if (axis == raidbuilder::AxisId::Y)
        return RotateVecAroundY90(v, dir);
    return RotateVecAroundZ90(v, dir);
}

struct DiceOrientation {
    // World directions of local +X, +Y, +Z axes.
    Vec3i x_axis = MakeVec3i(1, 0, 0);
    Vec3i y_axis = MakeVec3i(0, 1, 0);
    Vec3i z_axis = MakeVec3i(0, 0, 1);
};

static DiceOrientation OrientationFromBlock(const voxel::VoxelRenderer::Block& block) {
    const Mat3 rot = BlockRotationMatrix(block);
    float col_x[3] = {rot.m[0], rot.m[1], rot.m[2]};
    float col_y[3] = {rot.m[3], rot.m[4], rot.m[5]};
    float col_z[3] = {rot.m[6], rot.m[7], rot.m[8]};

    DiceOrientation o;
    o.x_axis = SnapAxis(col_x);
    o.y_axis = SnapAxis(col_y);
    o.z_axis = SnapAxis(col_z);
    return o;
}

static void ApplyWorldRotation90(DiceOrientation* o, raidbuilder::AxisId axis, int dir) {
    if (!o)
        return;
    o->x_axis = RotateVecAroundAxis90(o->x_axis, axis, dir);
    o->y_axis = RotateVecAroundAxis90(o->y_axis, axis, dir);
    o->z_axis = RotateVecAroundAxis90(o->z_axis, axis, dir);
}

static float ShortestDeltaDeg(float start_deg, float target_deg) {
    float diff = std::fmod(target_deg - start_deg, 360.0f);
    if (diff > 180.0f)
        diff -= 360.0f;
    if (diff < -180.0f)
        diff += 360.0f;
    return diff;
}

static Mat3 Mat3FromOrientation(const DiceOrientation& o) {
    Mat3 r = {};
    r.m[0] = (float)o.x_axis.x;
    r.m[1] = (float)o.x_axis.y;
    r.m[2] = (float)o.x_axis.z;
    r.m[3] = (float)o.y_axis.x;
    r.m[4] = (float)o.y_axis.y;
    r.m[5] = (float)o.y_axis.z;
    r.m[6] = (float)o.z_axis.x;
    r.m[7] = (float)o.z_axis.y;
    r.m[8] = (float)o.z_axis.z;
    return r;
}

static bool Mat3MatchesOrientation(const Mat3& rot, const DiceOrientation& o) {
    float col_x[3] = {rot.m[0], rot.m[1], rot.m[2]};
    float col_y[3] = {rot.m[3], rot.m[4], rot.m[5]};
    float col_z[3] = {rot.m[6], rot.m[7], rot.m[8]};
    const Vec3i sx = SnapAxis(col_x);
    const Vec3i sy = SnapAxis(col_y);
    const Vec3i sz = SnapAxis(col_z);
    return EqualVec3i(sx, o.x_axis) && EqualVec3i(sy, o.y_axis) && EqualVec3i(sz, o.z_axis);
}

static void ApplyOrientationToBlock(const DiceOrientation& target,
                                    voxel::VoxelRenderer::Block* block) {
    if (!block)
        return;
    // Search all quarter-turn Euler combinations and pick the closest match.
    float best_score = 1e30f;
    int best_rx = 0, best_ry = 0, best_rz = 0;
    for (int rx = 0; rx < 4; ++rx) {
        for (int ry = 0; ry < 4; ++ry) {
            for (int rz = 0; rz < 4; ++rz) {
                voxel::VoxelRenderer::Block candidate = *block;
                candidate.rot_x_deg = (float)(rx * 90);
                candidate.rot_y_deg = (float)(ry * 90);
                candidate.rot_z_deg = (float)(rz * 90);
                const Mat3 rot = BlockRotationMatrix(candidate);
                if (!Mat3MatchesOrientation(rot, target))
                    continue;
                const float score =
                    std::fabs(ShortestDeltaDeg(block->rot_x_deg, candidate.rot_x_deg)) +
                    std::fabs(ShortestDeltaDeg(block->rot_y_deg, candidate.rot_y_deg)) +
                    std::fabs(ShortestDeltaDeg(block->rot_z_deg, candidate.rot_z_deg));
                if (score < best_score) {
                    best_score = score;
                    best_rx = rx;
                    best_ry = ry;
                    best_rz = rz;
                }
            }
        }
    }
    block->rot_x_deg = (float)(best_rx * 90);
    block->rot_y_deg = (float)(best_ry * 90);
    block->rot_z_deg = (float)(best_rz * 90);
}

struct InspectorContext {
    std::vector<voxel::VoxelRenderer::Block>* blocks = nullptr;
    std::vector<unsigned char>* selected_flags = nullptr;
    float block_size = 1.0f;
    bool* dirty = nullptr;
    std::string* base_window_title = nullptr;
    std::string* current_dungeon_path = nullptr;
    GLFWwindow* window = nullptr;
    bool* close_dialog_active = nullptr;
    bool* edit_mode = nullptr;
    bool* hover_has_block = nullptr;
    int* hover_block_index = nullptr;
    float* main_scale = nullptr;
    AppState* saved_state = nullptr;
};

static void RenderInspectorPanel(const ImVec2& panel_pos, const ImVec2& panel_size, void* user_data) {
    (void)panel_pos;
    (void)panel_size;
    InspectorContext* ctx = reinterpret_cast<InspectorContext*>(user_data);
    if (!ctx || !ctx->blocks || !ctx->selected_flags)
        return;

    int inspector_block_index = -1;
    for (size_t i = 0; i < ctx->selected_flags->size(); ++i) {
        if ((*ctx->selected_flags)[i]) {
            inspector_block_index = (int)i;
            break;
        }
    }
    if (inspector_block_index < 0 && ctx->hover_has_block && *ctx->hover_has_block &&
        ctx->hover_block_index && *ctx->hover_block_index >= 0 &&
        *ctx->hover_block_index < (int)ctx->blocks->size()) {
        inspector_block_index = *ctx->hover_block_index;
    }

    ImGui::Separator();
    if (inspector_block_index >= 0 && inspector_block_index < (int)ctx->blocks->size()) {
        voxel::VoxelRenderer::Block& blk = (*ctx->blocks)[inspector_block_index];
        ImGui::Text("Block #%d  key=%s", inspector_block_index, blk.key.c_str());
        ImGui::Text("Rotation: X=%.0f  Y=%.0f  Z=%.0f", blk.rot_x_deg, blk.rot_y_deg, blk.rot_z_deg);
        if (ImGui::CollapsingHeader("Rotate Gizmo", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto apply_rotation = [&](raidbuilder::AxisId axis, int dir) {
                DiceOrientation o = OrientationFromBlock(blk);
                ApplyWorldRotation90(&o, axis, dir);
                ApplyOrientationToBlock(o, &blk);
                g_VoxelRenderer.setBlocks(*ctx->blocks, ctx->block_size);
                if (ctx->dirty)
                    *ctx->dirty = true;
                if (ctx->window && ctx->base_window_title && ctx->current_dungeon_path && ctx->dirty)
                    UpdateWindowTitle(ctx->window, *ctx->base_window_title, *ctx->current_dungeon_path, *ctx->dirty);
            };
            if (ImGui::Button("X +90")) apply_rotation(raidbuilder::AxisId::X, 1);
            ImGui::SameLine();
            if (ImGui::Button("X -90")) apply_rotation(raidbuilder::AxisId::X, -1);
            if (ImGui::Button("Y +90")) apply_rotation(raidbuilder::AxisId::Y, 1);
            ImGui::SameLine();
            if (ImGui::Button("Y -90")) apply_rotation(raidbuilder::AxisId::Y, -1);
            if (ImGui::Button("Z +90")) apply_rotation(raidbuilder::AxisId::Z, 1);
            ImGui::SameLine();
            if (ImGui::Button("Z -90")) apply_rotation(raidbuilder::AxisId::Z, -1);
        }
    } else {
        ImGui::TextUnformatted("Hover or select a block to edit its properties.");
    }
}

static bool RayAabb(const float origin[3],
                    const float dir[3],
                    const float bmin[3],
                    const float bmax[3],
                    float* out_t,
                    float* out_nx,
                    float* out_ny,
                    float* out_nz) {
    float tmin = 0.0f;
    float tmax = 1e30f;
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float o = origin[axis];
        float d = dir[axis];
        float min_v = bmin[axis];
        float max_v = bmax[axis];
        if (std::fabs(d) < 1e-6f) {
            if (o < min_v || o > max_v)
                return false;
            continue;
        }
        float inv = 1.0f / d;
        float t1 = (min_v - o) * inv;
        float t2 = (max_v - o) * inv;
        float nsign = -1.0f;
        if (t1 > t2) {
            std::swap(t1, t2);
            nsign = 1.0f;
        }
        if (t1 > tmin) {
            tmin = t1;
            nx = (axis == 0) ? nsign : 0.0f;
            ny = (axis == 1) ? nsign : 0.0f;
            nz = (axis == 2) ? nsign : 0.0f;
        }
        if (t2 < tmax)
            tmax = t2;
        if (tmin > tmax)
            return false;
    }
    float t = (tmin >= 0.0f) ? tmin : tmax;
    if (t < 0.0f)
        return false;
    *out_t = t;
    *out_nx = nx;
    *out_ny = ny;
    *out_nz = nz;
    return true;
}

static bool RaycastBlocks(const float origin[3],
                          const float dir[3],
                          const std::vector<voxel::VoxelRenderer::Block>& blocks,
                          float block_size,
                          RaycastHit* out_hit) {
    bool hit = false;
    float best_t = 1e30f;
    int best_index = -1;
    float best_nx = 0.0f, best_ny = 0.0f, best_nz = 0.0f;
    float half = block_size * 0.5f;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const float center[3] = {blocks[i].x, blocks[i].y, blocks[i].z};
        const Mat3 rot = BlockRotationMatrix(blocks[i]);
        const Mat3 inv_rot = Mat3Transpose(rot);

        // Transform the ray into the block's local space so hovered faces
        // track the actual rotated model, not the world-aligned AABB.
        float origin_rel[3] = {
            origin[0] - center[0],
            origin[1] - center[1],
            origin[2] - center[2],
        };
        float origin_local[3] = {};
        float dir_local[3] = {};
        Mat3MulVec(inv_rot, origin_rel, origin_local);
        Mat3MulVec(inv_rot, dir, dir_local);

        float bmin[3] = {-half, -half, -half};
        float bmax[3] = {half, half, half};
        float t = 0.0f;
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        if (RayAabb(origin_local, dir_local, bmin, bmax, &t, &nx, &ny, &nz)) {
            if (t < best_t) {
                best_t = t;
                best_index = (int)i;
                // Rotate the local face normal back to world space.
                float n_local[3] = {nx, ny, nz};
                float n_world[3] = {};
                Mat3MulVec(rot, n_local, n_world);
                best_nx = n_world[0];
                best_ny = n_world[1];
                best_nz = n_world[2];
                hit = true;
            }
        }
    }
    if (!hit)
        return false;
    out_hit->hit = true;
    out_hit->t = best_t;
    out_hit->block_index = best_index;
    out_hit->nx = best_nx;
    out_hit->ny = best_ny;
    out_hit->nz = best_nz;
    out_hit->hit_x = origin[0] + dir[0] * best_t;
    out_hit->hit_y = origin[1] + dir[1] * best_t;
    out_hit->hit_z = origin[2] + dir[2] * best_t;
    out_hit->ground = false;
    return true;
}

static bool RaycastGround(const float origin[3], const float dir[3], RaycastHit* out_hit) {
    if (std::fabs(dir[1]) < 1e-6f)
        return false;
    float t = -origin[1] / dir[1];
    if (t < 0.0f)
        return false;
    out_hit->hit = true;
    out_hit->t = t;
    out_hit->block_index = -1;
    out_hit->nx = 0.0f;
    out_hit->ny = 1.0f;
    out_hit->nz = 0.0f;
    out_hit->hit_x = origin[0] + dir[0] * t;
    out_hit->hit_y = 0.0f;
    out_hit->hit_z = origin[2] + dir[2] * t;
    out_hit->ground = true;
    return true;
}

static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension) {
    for (int i = 0; i < properties.Size; i++)
        if (strcmp(properties[i].extensionName, extension) == 0)
            return true;
    return false;
}

static void SetupVulkan(ImVector<const char*> instance_extensions) {
    VkResult err;
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "RaidBuilder";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VoxelEngine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;
    create_info.pApplicationInfo = &app_info;

    uint32_t properties_count = 0;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
    check_vk_result(err);

    if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
    create_info.ppEnabledExtensionNames = instance_extensions.Data;

    err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
    check_vk_result(err);

    g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
    IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);

    g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
    IM_ASSERT(g_QueueFamily != (uint32_t)-1);

    ImVector<const char*> device_extensions;
    device_extensions.push_back("VK_KHR_swapchain");
    properties_count = 0;
    vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
        device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    const float queue_priority[] = {1.0f};
    VkDeviceQueueCreateInfo queue_info[1] = {};
    queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[0].queueFamilyIndex = g_QueueFamily;
    queue_info[0].queueCount = 1;
    queue_info[0].pQueuePriorities = queue_priority;

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = queue_info;
    device_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
    device_info.ppEnabledExtensionNames = device_extensions.Data;

    err = vkCreateDevice(g_PhysicalDevice, &device_info, g_Allocator, &g_Device);
    check_vk_result(err);
    vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);

    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
    check_vk_result(err);
}

static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height) {
    wd->Surface = surface;

    VkBool32 res = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
    if (res != VK_TRUE) {
        fprintf(stderr, "Error: no WSI support on physical device 0\n");
        exit(1);
    }

    const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, present_modes, IM_ARRAYSIZE(present_modes));

    ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount, 0);
}

static void CleanupVulkan() {
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);
    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

static void CleanupVulkanWindow() {
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}

static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data) {
    VkResult err;
    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
        return;
    }
    check_vk_result(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        check_vk_result(err);
        err = vkResetFences(g_Device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    g_VoxelRenderer.render(fd->CommandBuffer, wd->Width, wd->Height);
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
        check_vk_result(err);
    }
}

static void FramePresent(ImGui_ImplVulkanH_Window* wd) {
    if (g_SwapChainRebuild)
        return;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
        return;
    }
    check_vk_result(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount;
}

int main(int, char**) {
    std::string ui_path = ResolveRepoPath("UI.sml");
    smlui::UiDocument ui_document;
    std::string parse_error;
    std::string ui_text;
    if (!LoadFileText(ui_path.c_str(), &ui_text)) {
        fprintf(stderr, "SML load error: could not read %s\n", ui_path.c_str());
    } else if (!ui_document.parseFromString(ui_text, &parse_error)) {
        fprintf(stderr, "SML parse error: %s\n", parse_error.c_str());
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    smlui::UiWindow ui_window = ui_document.window();
    if (!ui_window.state.theme.empty()) {
        smlui::UiTheme theme;
        std::string theme_path = ResolveThemePath(ui_window.state.theme);
        if (LoadThemeFile(theme_path, &theme))
            ui_document.setTheme(theme);
    }
    AppState saved_state;
    std::string state_path = GetStatePath(ui_window.state.persist);
    LoadAppState(state_path, &saved_state);
    if (ui_window.state.pos && saved_state.has_pos) {
        ui_window.position.x = saved_state.pos_x;
        ui_window.position.y = saved_state.pos_y;
    }
    if (ui_window.state.size && saved_state.has_size) {
        ui_window.size.x = saved_state.size_x;
        ui_window.size.y = saved_state.size_y;
    }
    std::string current_dungeon_path = ResolveRepoPath("dungeon.sml");
    if (ui_window.state.last_file_path && !saved_state.last_file_path.empty() && FileExists(saved_state.last_file_path)) {
        current_dungeon_path = saved_state.last_file_path;
    }
    int pending_menu_action = 0;
    auto menu_action_handler = [](int action_id, void* user_data) {
        if (!user_data)
            return;
        *static_cast<int*>(user_data) = action_id;
    };
    ui_document.setMenuActionCallback(menu_action_handler, &pending_menu_action);
    const float block_size = 0.6f;
    std::vector<voxel::VoxelRenderer::Block> dungeon_blocks;
    std::vector<unsigned char> selected_flags;
    std::vector<TileDef> tiles;
    std::vector<TileDef> dungeon_tiles;
    std::string default_texture_path = "Assets/textures/raid_stone.png";
    std::string catalog_error;
    TileCatalog catalog;
    const std::string repo_root = ResolveWorkspacePath(".");
    const bool catalog_ok = LoadTileCatalog(repo_root, "RaidBuilder/tiles", default_texture_path, &catalog, &catalog_error);
    if (!catalog_error.empty())
        fprintf(stderr, "Tile catalog parse error: %s\n", catalog_error.c_str());
    std::string dungeon_error;
    SpawnPoint dungeon_spawn;
    if (!LoadDungeonFromPath(current_dungeon_path,
                             block_size,
                             &dungeon_blocks,
                             &dungeon_tiles,
                             &dungeon_spawn,
                             &dungeon_error)) {
        fprintf(stderr, "%s\n", dungeon_error.c_str());
    } else if (!dungeon_spawn.valid) {
        fprintf(stderr, "%s\n", dungeon_error.c_str());
    }
    if (catalog_ok && !catalog.tiles.empty()) {
        tiles = catalog.tiles;
        std::map<std::string, size_t> by_key;
        for (size_t i = 0; i < tiles.size(); ++i)
            by_key[tiles[i].key] = i;
        for (size_t i = 0; i < dungeon_tiles.size(); ++i) {
            const TileDef& src = dungeon_tiles[i];
            std::map<std::string, size_t>::iterator it = by_key.find(src.key);
            if (it != by_key.end()) {
                TileDef merged = MergeTileOverride(tiles[it->second], src);
                tiles[it->second] = merged;
            } else if (!src.key.empty()) {
                tiles.push_back(src);
            }
        }
    } else {
        tiles = dungeon_tiles;
    }
    if (tiles.empty()) {
        TileDef tile;
        tile.key = "s";
        tile.texture = default_texture_path;
        tile.model = "block.glb";
        tile.type = "block";
        tile.height_cm = 60;
        tile.scale_percent = 100;
        tile.height_blocks = 1;
        tiles.push_back(tile);
    }

    TileCatalog merged_catalog = catalog;
    if (!tiles.empty()) {
        std::map<std::string, TileDef> overrides;
        for (size_t i = 0; i < dungeon_tiles.size(); ++i) {
            if (!dungeon_tiles[i].key.empty())
                overrides[dungeon_tiles[i].key] = dungeon_tiles[i];
        }
        for (size_t i = 0; i < tiles.size(); ++i) {
            std::map<std::string, TileDef>::const_iterator it = overrides.find(tiles[i].key);
            if (it != overrides.end()) {
                TileDef merged = MergeTileOverride(tiles[i], it->second);
                tiles[i] = merged;
            }
        }
        merged_catalog.tiles = tiles;
        std::vector<TileDef> merged_tiles = merged_catalog.tiles;
        if (!PopulateTileResources(repo_root, default_texture_path, &merged_tiles, &merged_catalog)) {
            fprintf(stderr, "Failed to populate tile resources\n");
        }
        merged_catalog.tiles = merged_tiles;
    }

    std::vector<voxel::VoxelRenderer::MeshData> tile_meshes = merged_catalog.meshes;
    std::vector<bool> tile_mesh_has_uv = merged_catalog.mesh_has_uv;
    std::vector<std::string> block_texture_paths = merged_catalog.texture_paths;
    if (block_texture_paths.empty())
        fprintf(stderr, "RaidBuilder: tile catalog has no textures\n");

    auto tile_tex_index_for = [&](size_t index) -> int {
        if (index >= tiles.size())
            return -2;
        if (index >= tile_mesh_has_uv.size())
            return -2;
        if (tiles[index].texture.empty() || !tile_mesh_has_uv[index])
            return -2;
        return (int)index;
    };

    std::map<std::string, int> tile_index_by_key = merged_catalog.index_by_key;
    std::vector<bool> tile_is_symmetric;
    tile_is_symmetric.resize(tiles.size(), false);
    for (size_t i = 0; i < tiles.size(); ++i)
        tile_is_symmetric[i] = IsSymmetricTileModel(tiles[i].model);
    for (size_t i = 0; i < dungeon_blocks.size(); ++i) {
        int idx = 0;
        std::map<std::string, int>::const_iterator it = tile_index_by_key.find(dungeon_blocks[i].key);
        if (it != tile_index_by_key.end())
            idx = it->second;
        dungeon_blocks[i].mesh_index = idx;
        dungeon_blocks[i].tex_index = tile_tex_index_for((size_t)idx);
    }

    std::map<std::string, std::vector<int> > tiles_by_category;
    for (size_t i = 0; i < tiles.size(); ++i) {
        std::string cat = tiles[i].category.empty() ? "Core" : tiles[i].category;
        tiles_by_category[cat].push_back((int)i);
    }

    std::vector<int> action_slots(10, -1);
    for (int i = 0; i < 10 && i < (int)tiles.size(); ++i)
        action_slots[i] = i;
    int active_slot = 0;
    const std::string actionbar_path = GetActionBarPath();
    bool actionbar_dirty = false;
    LoadActionBar(actionbar_path, tile_index_by_key, &action_slots, &active_slot);
    if (active_slot < 0 || active_slot >= (int)action_slots.size())
        active_slot = 0;
    bool inventory_open = false;
    bool slot_was_down[10] = {false, false, false, false, false, false, false, false, false, false};
    bool arrow_was_down[4] = {false, false, false, false};
    // Simple rotation animation state (mirrors the Godot tween behavior).
    bool rotation_anim_active = false;
    int rotation_anim_block = -1;
    double rotation_anim_start = 0.0;
    const double rotation_anim_duration = 0.12;
    float rotation_anim_start_rx = 0.0f;
    float rotation_anim_start_ry = 0.0f;
    float rotation_anim_start_rz = 0.0f;
    float rotation_anim_end_rx = 0.0f;
    float rotation_anim_end_ry = 0.0f;
    float rotation_anim_end_rz = 0.0f;
    float rotation_anim_target_rx = 0.0f;
    float rotation_anim_target_ry = 0.0f;
    float rotation_anim_target_rz = 0.0f;
    selected_flags.assign(dungeon_blocks.size(), 0);
    g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
    std::string base_window_title = ui_window.title.empty() ? "RaidBuilder" : ui_window.title;
    GLFWwindow* window = glfwCreateWindow((int)(ui_window.size.x * main_scale), (int)(ui_window.size.y * main_scale), base_window_title.c_str(), nullptr, nullptr);
    if (!glfwVulkanSupported()) {
        printf("GLFW: Vulkan Not Supported\n");
        return 1;
    }

#if defined(__APPLE__)
    BuildMacMainMenu(window, ui_window, menu_action_handler, &pending_menu_action);
#endif

    ImVector<const char*> extensions;
    uint32_t extensions_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&extensions_count);
    for (uint32_t i = 0; i < extensions_count; i++)
        extensions.push_back(glfw_extensions[i]);
    SetupVulkan(extensions);

    VkSurfaceKHR surface;
    VkResult err = glfwCreateWindowSurface(g_Instance, window, g_Allocator, &surface);
    check_vk_result(err);

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    SetupVulkanWindow(wd, surface, w, h);

    std::string shader_world_vert = ResolveRepoPath("shaders/world.vert.spv");
    std::string shader_world_frag = ResolveRepoPath("shaders/world.frag.spv");
    std::string shader_pick_vert = ResolveRepoPath("shaders/pick.vert.spv");
    std::string shader_pick_frag = ResolveRepoPath("shaders/pick.frag.spv");
    std::string ground_texture = ResolveWorkspacePath("Assets/textures/raid_ground.png");

    if (!FileExists(shader_world_vert))
        fprintf(stderr, "Missing shader: %s\n", shader_world_vert.c_str());
    if (!FileExists(shader_world_frag))
        fprintf(stderr, "Missing shader: %s\n", shader_world_frag.c_str());
    if (!FileExists(shader_pick_vert))
        fprintf(stderr, "Missing shader: %s\n", shader_pick_vert.c_str());
    if (!FileExists(shader_pick_frag))
        fprintf(stderr, "Missing shader: %s\n", shader_pick_frag.c_str());
    if (!FileExists(ground_texture))
        fprintf(stderr, "Missing ground texture: %s\n", ground_texture.c_str());

    if (!g_VoxelRenderer.init(g_Device, g_PhysicalDevice, g_Queue, g_QueueFamily, wd->RenderPass,
                              shader_world_vert.c_str(),
                              shader_world_frag.c_str(),
                              shader_pick_vert.c_str(),
                              shader_pick_frag.c_str(),
                              ground_texture.c_str(),
                              block_texture_paths)) {
        fprintf(stderr, "VoxelRenderer init failed (missing shaders?)\n");
    }
    g_VoxelRenderer.setBlockMeshes(tile_meshes);
    g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
    g_VoxelRenderer.setSelection(selected_flags);
    g_VoxelRenderer.resizePickResources((uint32_t)w, (uint32_t)h);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_PhysicalDevice;
    init_info.Device = g_Device;
    init_info.QueueFamily = g_QueueFamily;
    init_info.Queue = g_Queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.Allocator = g_Allocator;
    init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);

    ImFont* font_13 = nullptr;
    ImFont* font_15 = nullptr;
    ImFontConfig font_cfg;
    font_cfg.SizePixels = 13.0f;
    font_13 = io.Fonts->AddFontDefault(&font_cfg);
    ImFontConfig label_cfg;
    label_cfg.SizePixels = 15.0f;
    font_15 = io.Fonts->AddFontDefault(&label_cfg);
    io.FontDefault = font_13;

    glfwSetWindowPos(window, (int)(ui_window.position.x * main_scale), (int)(ui_window.position.y * main_scale));
    if (ui_window.state.maximized && saved_state.maximized)
        glfwMaximizeWindow(window);
    glfwSetCursorPos(window, (ui_window.size.x * main_scale) * 0.5, (ui_window.size.y * main_scale) * 0.5);

    bool edit_mode = true;
    bool dirty = false;
    UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
    bool close_pending = false;
    bool close_request = false;
    bool close_dialog_active = false;
    bool inventory_block_prev = false;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    double last_mouse_x = 0.0;
    double last_mouse_y = 0.0;
    bool first_mouse = true;
    const float eye_height = 1.6f;
    float camera_x = dungeon_spawn.valid ? dungeon_spawn.x : 6.0f;
    float camera_z = dungeon_spawn.valid ? dungeon_spawn.z : 6.0f;
    float camera_y = dungeon_spawn.valid ? (dungeon_spawn.y + eye_height) : eye_height;
    float camera_yaw = 3.1415926f * 0.75f;
    float camera_pitch = -0.5f;
    bool f_was_down = false;
    bool e_was_down = false;
    bool q_was_down = false;
    bool g_was_down = false;
    bool c_was_down = false;
    bool esc_was_down = false;
    bool inv_was_down = false;
    bool save_was_down = false;
    bool right_was_down = false;
    bool left_was_down = false;
    bool suppress_backward = false;
    bool gravity_enabled = true;
    bool collision_enabled = true;
    bool painting = false;
    bool ghost_hover_last = false;
    int paint_count = 0;
    bool paint_last_valid = false;
    float paint_last_x = 0.0f;
    float paint_last_y = 0.0f;
    float paint_last_z = 0.0f;
    bool paint_dir_valid = false;
    float paint_dir_x = 0.0f;
    float paint_dir_y = 0.0f;
    float paint_dir_z = 0.0f;
    float paint_anchor_x = 0.0f;
    float paint_anchor_y = 0.0f;
    float paint_anchor_z = 0.0f;
    bool selecting = false;
    ImVec2 select_start(0.0f, 0.0f);
    ImVec2 select_end(0.0f, 0.0f);
    bool hover_has_block = false;
    bool hover_has_ground = false;
    int hover_block_index = -1;
    float hover_face_nx = 0.0f;
    float hover_face_ny = 0.0f;
    float hover_face_nz = 0.0f;
    float hover_place_x = 0.0f;
    float hover_place_y = 0.0f;
    float hover_place_z = 0.0f;
    bool ghost_enabled = false;
    const int max_paint_blocks = 30; // TODO: make configurable.
    const float max_place_distance = 30.0f;
    double last_time = glfwGetTime();

    voxel::CharacterConfig character_config;
    character_config.height = 1.7f;
    character_config.radius = 0.25f;
    character_config.block_size = block_size;
    character_config.can_toggle_gravity_mode = true;
    character_config.can_toggle_collision = true;
    voxel::CharacterController character(character_config);
    std::unordered_set<long long> solid_blocks;
    solid_blocks.reserve(dungeon_blocks.size());
    for (size_t i = 0; i < dungeon_blocks.size(); ++i) {
        if (dungeon_blocks[i].key == "S")
            continue;
        int wx = (int)std::round(dungeon_blocks[i].x / block_size - 0.5f);
        int wy = (int)std::round(dungeon_blocks[i].y / block_size - 0.5f);
        int wz = (int)std::round(dungeon_blocks[i].z / block_size - 0.5f);
        solid_blocks.insert(BlockKey(wx, wy, wz));
    }
    auto add_solid_block = [&](const voxel::VoxelRenderer::Block& block) {
        if (block.key == "S")
            return;
        int wx = (int)std::round(block.x / block_size - 0.5f);
        int wy = (int)std::round(block.y / block_size - 0.5f);
        int wz = (int)std::round(block.z / block_size - 0.5f);
        solid_blocks.insert(BlockKey(wx, wy, wz));
    };
    auto remove_solid_block = [&](const voxel::VoxelRenderer::Block& block) {
        int wx = (int)std::round(block.x / block_size - 0.5f);
        int wy = (int)std::round(block.y / block_size - 0.5f);
        int wz = (int)std::round(block.z / block_size - 0.5f);
        solid_blocks.erase(BlockKey(wx, wy, wz));
    };
    auto is_overlapping_solid = [&](const voxel::Vec3& pos) {
        const float half_height = std::max(character_config.height * 0.5f - character_config.radius, 0.0f);
        const float radius = character_config.radius + character_config.skin;
        const float min_x = pos.x - radius;
        const float max_x = pos.x + radius;
        const float min_y = pos.y - half_height - radius;
        const float max_y = pos.y + half_height + radius;
        const float min_z = pos.z - radius;
        const float max_z = pos.z + radius;
        const int min_ix = (int)std::floor(min_x / block_size);
        const int max_ix = (int)std::floor(max_x / block_size);
        const int min_iy = (int)std::floor(min_y / block_size);
        const int max_iy = (int)std::floor(max_y / block_size);
        const int min_iz = (int)std::floor(min_z / block_size);
        const int max_iz = (int)std::floor(max_z / block_size);
        for (int iz = min_iz; iz <= max_iz; ++iz) {
            for (int iy = min_iy; iy <= max_iy; ++iy) {
                for (int ix = min_ix; ix <= max_ix; ++ix) {
                    if (solid_blocks.find(BlockKey(ix, iy, iz)) != solid_blocks.end())
                        return true;
                }
            }
        }
        return false;
    };
    auto resolve_collision_overlap = [&](voxel::Vec3* pos) {
        if (!pos)
            return;
        if (!is_overlapping_solid(*pos))
            return;
        for (int i = 0; i < 20; ++i) {
            pos->y += block_size;
            if (!is_overlapping_solid(*pos))
                return;
        }
    };
    character.setSolidQuery([&solid_blocks](int ix, int iy, int iz) {
        return solid_blocks.find(BlockKey(ix, iy, iz)) != solid_blocks.end();
    });
    character.setPosition({camera_x, camera_y - (eye_height - character_config.height * 0.5f), camera_z});
    character.setCollisionEnabled(collision_enabled);
    character.setGravityEnabled(gravity_enabled);

    ImVec4 clear_color = ImVec4(0.18f, 0.35f, 0.75f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) {
            if (dirty) {
                close_pending = true;
                glfwSetWindowShouldClose(window, GLFW_FALSE);
            } else {
                break;
            }
        }
        double now_time = glfwGetTime();
        float dt = (float)(now_time - last_time);
        last_time = now_time;

        if (rotation_anim_active && rotation_anim_block >= 0 &&
            rotation_anim_block < (int)dungeon_blocks.size()) {
            double t = (now_time - rotation_anim_start) / rotation_anim_duration;
            if (t < 0.0)
                t = 0.0;
            if (t > 1.0)
                t = 1.0;
            // Ease-out similar to the Godot tween.
            double ease = 1.0 - (1.0 - t) * (1.0 - t);
            voxel::VoxelRenderer::Block& b = dungeon_blocks[rotation_anim_block];
            b.rot_x_deg = (float)(rotation_anim_start_rx + (rotation_anim_end_rx - rotation_anim_start_rx) * ease);
            b.rot_y_deg = (float)(rotation_anim_start_ry + (rotation_anim_end_ry - rotation_anim_start_ry) * ease);
            b.rot_z_deg = (float)(rotation_anim_start_rz + (rotation_anim_end_rz - rotation_anim_start_rz) * ease);
            g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
            if (t >= 1.0) {
                // Snap to the logical target at the end (after shortest-arc animation).
                b.rot_x_deg = rotation_anim_target_rx;
                b.rot_y_deg = rotation_anim_target_ry;
                b.rot_z_deg = rotation_anim_target_rz;
                rotation_anim_active = false;
                rotation_anim_block = -1;
                g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
            }
        }

        if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
            if (!f_was_down) {
                edit_mode = !edit_mode;
                glfwSetInputMode(window, GLFW_CURSOR, edit_mode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                first_mouse = true;
                selecting = false;
                right_was_down = false;
                hover_has_block = false;
                hover_has_ground = false;
                hover_block_index = -1;
                painting = false;
                ghost_hover_last = false;
                if (!edit_mode) {
                    std::fill(selected_flags.begin(), selected_flags.end(), 0);
                    g_VoxelRenderer.setSelection(selected_flags);
                }
            }
            f_was_down = true;
        } else {
            f_was_down = false;
        }
        if (!edit_mode) {
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                if (!esc_was_down) {
                    edit_mode = true;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    first_mouse = true;
                    selecting = false;
                    right_was_down = false;
                    hover_has_block = false;
                    hover_has_ground = false;
                    hover_block_index = -1;
                    painting = false;
                    ghost_hover_last = false;
                    std::fill(selected_flags.begin(), selected_flags.end(), 0);
                    g_VoxelRenderer.setSelection(selected_flags);
                }
                esc_was_down = true;
            } else {
                esc_was_down = false;
            }
        } else {
            esc_was_down = false;
        }

        if (!close_dialog_active) {
            int prev_active_slot = active_slot;
            bool inv_down = (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS);
            if (inv_down && !inv_was_down)
                inventory_open = !inventory_open;
            inv_was_down = inv_down;

            const int slot_keys[10] = {
                GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
                GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9, GLFW_KEY_0
            };
            for (int i = 0; i < 10; ++i) {
                bool down = (glfwGetKey(window, slot_keys[i]) == GLFW_PRESS);
                if (down && !slot_was_down[i])
                    active_slot = i;
                slot_was_down[i] = down;
            }
            if (active_slot != prev_active_slot)
                actionbar_dirty = true;
        } else {
            inv_was_down = false;
            for (int i = 0; i < 10; ++i)
                slot_was_down[i] = false;
        }

        if (pending_menu_action != 0) {
            int action_id = pending_menu_action;
            pending_menu_action = 0;
            if (action_id == MENU_ACTION_SAVE) {
                if (SaveDungeonAuto(current_dungeon_path, dungeon_blocks, block_size, tiles)) {
                    saved_state.last_file_path = current_dungeon_path;
                    SaveAppState(state_path, saved_state);
                    dirty = false;
                    UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
                }
            } else if (action_id == MENU_ACTION_SAVE_AS) {
                std::string default_dir = GetParentDir(current_dungeon_path);
                std::string selected = SelectFolderDialog("Save Dungeon As", default_dir);
                if (!selected.empty()) {
                    std::string target_path = JoinPath(selected, "dungeon.sml");
                    if (SaveDungeonAuto(target_path, dungeon_blocks, block_size, tiles)) {
                        current_dungeon_path = target_path;
                        saved_state.last_file_path = current_dungeon_path;
                        SaveAppState(state_path, saved_state);
                        dirty = false;
                        UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
                    }
                }
            } else if (action_id == MENU_ACTION_OPEN) {
                // TODO: Replace with native file dialog when available.
                fprintf(stderr, "Menu action: open (not implemented)\n");
            } else if (action_id == MENU_ACTION_CLOSE_QUERY) {
                close_pending = true;
            }
        }

        int active_tile_index = 0;
        if (active_slot >= 0 && active_slot < (int)action_slots.size() && action_slots[active_slot] >= 0)
            active_tile_index = action_slots[active_slot];
        if (active_tile_index < 0 || active_tile_index >= (int)tiles.size())
            active_tile_index = 0;
        const std::string active_tile_key = tiles[active_tile_index].key;
        const int active_mesh_index = active_tile_index;
        const int active_tex_index = tile_tex_index_for((size_t)active_tile_index);
        bool inventory_blocks_controls = (inventory_open && !close_dialog_active);
        if (inventory_blocks_controls != inventory_block_prev) {
            if (inventory_blocks_controls) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, edit_mode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                first_mouse = true;
            }
            inventory_block_prev = inventory_blocks_controls;
        }

        bool save_key_down = (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
        bool cmd_down = (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS) ||
                        (glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS);
        bool ctrl_down = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                         (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
        bool save_combo = save_key_down && (cmd_down || ctrl_down);
        if (save_combo && !save_was_down) {
            if (SaveDungeonAuto(current_dungeon_path, dungeon_blocks, block_size, tiles)) {
                saved_state.last_file_path = current_dungeon_path;
                SaveAppState(state_path, saved_state);
                dirty = false;
                UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
            }
        }
        if (!save_key_down && !cmd_down && !ctrl_down)
            save_was_down = false;
        else if (save_combo)
            save_was_down = true;
        if (!edit_mode) {
            bool keyboard_backward = save_key_down;
            if ((cmd_down || ctrl_down || save_combo) && keyboard_backward)
                suppress_backward = true;
            if (suppress_backward && !keyboard_backward)
                suppress_backward = false;
        } else {
            suppress_backward = false;
        }
        if (!edit_mode) {
            if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) {
                if (!g_was_down) {
                    ghost_enabled = !ghost_enabled;
                    painting = false;
                    paint_last_valid = false;
                    paint_dir_valid = false;
                }
                g_was_down = true;
            } else {
                g_was_down = false;
            }
        } else {
            g_was_down = false;
        }

        double mouse_x = 0.0, mouse_y = 0.0;
        glfwGetCursorPos(window, &mouse_x, &mouse_y);
        if (first_mouse) {
            last_mouse_x = mouse_x;
            last_mouse_y = mouse_y;
            first_mouse = false;
        }
        float dx = (float)(mouse_x - last_mouse_x);
        float dy = (float)(mouse_y - last_mouse_y);
        last_mouse_x = mouse_x;
        last_mouse_y = mouse_y;

        if (!edit_mode && !(inventory_open && !close_dialog_active)) {
            const float mouse_sens = 0.0025f;
            camera_yaw += dx * mouse_sens;
            camera_pitch -= dy * mouse_sens;
            const float pitch_limit = 1.55f;
            if (camera_pitch > pitch_limit)
                camera_pitch = pitch_limit;
            if (camera_pitch < -pitch_limit)
                camera_pitch = -pitch_limit;
        }

        float cy = std::cos(camera_yaw);
        float sy = std::sin(camera_yaw);
        float forward_x = cy;
        float forward_z = sy;
        float right_x = -sy;
        float right_z = cy;

        float speed = 5.0f;

        if (!edit_mode) {
            bool controls_blocked = (inventory_open && !close_dialog_active) || rotation_anim_active;
            if (!controls_blocked) {
                bool e_down = (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS);
                bool q_down = (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS);
                bool c_down = (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS);

                if (character_config.can_toggle_gravity_mode) {
                    if (e_down && q_down) {
                        gravity_enabled = true;
                    } else if ((e_down || q_down) && !(e_was_down || q_was_down)) {
                        gravity_enabled = false;
                    }
                }
                const bool step_up = !gravity_enabled && e_down && !e_was_down;
                const bool step_down = !gravity_enabled && q_down && !q_was_down;
                if (character_config.can_toggle_collision && c_down && !c_was_down) {
                    collision_enabled = !collision_enabled;
                    if (collision_enabled) {
                        voxel::Vec3 pos = character.position();
                        resolve_collision_overlap(&pos);
                        character.setPosition(pos);
                        character.setVelocity(voxel::Vec3(0.0f, 0.0f, 0.0f));
                    }
                }
                c_was_down = c_down;
                e_was_down = e_down;
                q_was_down = q_down;

                character.setGravityEnabled(gravity_enabled);
                character.setCollisionEnabled(collision_enabled);

                const bool forward_key = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
                const bool back_key = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
                const bool left_key = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
                const bool right_key = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
                const bool jump_key = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;

                voxel::CharacterInput character_input;
                float move_x = 0.0f;
                float move_z = 0.0f;
                if (forward_key)
                    move_x += forward_x, move_z += forward_z;
                if (back_key && !suppress_backward)
                    move_x -= forward_x, move_z -= forward_z;
                if (left_key)
                    move_x -= right_x, move_z -= right_z;
                if (right_key)
                    move_x += right_x, move_z += right_z;
                float len = std::sqrt(move_x * move_x + move_z * move_z);
                if (len > 0.0001f) {
                    move_x /= len;
                    move_z /= len;
                }

                const float accel = speed * 8.0f;
                if (forward_key || back_key || left_key || right_key) {
                    character_input.accel_x = move_x * accel;
                    character_input.accel_z = move_z * accel;
                } else {
                    const voxel::Vec3 vel = character.velocity();
                    character_input.accel_x = -vel.x * 6.0f;
                    character_input.accel_z = -vel.z * 6.0f;
                }

                character_input.jump = gravity_enabled && jump_key;
                character.update(dt, character_input);
                voxel::Vec3 pos = character.position();
                voxel::Vec3 vel = character.velocity();
                if (!gravity_enabled) {
                    vel.y = 0.0f;
                    if (step_up)
                        pos.y += block_size;
                    if (step_down)
                        pos.y -= block_size;
                }
                const float min_center_y = character_config.height * 0.5f;
                if (pos.y < min_center_y) {
                    pos.y = min_center_y;
                    vel.y = 0.0f;
                }
                character.setPosition(pos);
                character.setVelocity(vel);
                camera_x = pos.x;
                camera_y = pos.y + (eye_height - character_config.height * 0.5f);
                camera_z = pos.z;
            } else {
                e_was_down = false;
                q_was_down = false;
                c_was_down = false;
            }
        } else {
            // Selection handling moved after UI so we can honor hovered UI windows.
        }

        if (!edit_mode) {
            float cp = std::cos(camera_pitch);
            float sp = std::sin(camera_pitch);
            float cy = std::cos(camera_yaw);
            float sy = std::sin(camera_yaw);
            float ray_origin[3] = {camera_x, camera_y, camera_z};
            float ray_dir[3] = {cp * cy, sp, cp * sy};

            RaycastHit block_hit = {};
            RaycastHit ground_hit = {};
            bool has_block = RaycastBlocks(ray_origin, ray_dir, dungeon_blocks, block_size, &block_hit);
            bool has_ground = RaycastGround(ray_origin, ray_dir, &ground_hit);

            RaycastHit best_hit = {};
            bool has_best = false;
            if (has_block && (!has_ground || block_hit.t <= ground_hit.t)) {
                best_hit = block_hit;
                has_best = true;
            } else if (has_ground) {
                best_hit = ground_hit;
                has_best = true;
            }

            hover_has_block = has_block;
            hover_has_ground = has_best && best_hit.ground;
            hover_block_index = has_block ? block_hit.block_index : -1;
            if (has_block) {
                hover_face_nx = block_hit.nx;
                hover_face_ny = block_hit.ny;
                hover_face_nz = block_hit.nz;
            } else {
                hover_face_nx = 0.0f;
                hover_face_ny = 1.0f;
                hover_face_nz = 0.0f;
            }

            int right_state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
            int left_state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
            if (has_best) {
                if (best_hit.ground) {
                    hover_place_x = SnapToGridCenter(best_hit.hit_x, block_size);
                    hover_place_z = SnapToGridCenter(best_hit.hit_z, block_size);
                    hover_place_y = block_size * 0.5f;
                    hover_face_nx = 0.0f;
                    hover_face_ny = 1.0f;
                    hover_face_nz = 0.0f;
                } else {
                    const voxel::VoxelRenderer::Block& base = dungeon_blocks[best_hit.block_index];
                    hover_place_x = base.x + best_hit.nx * block_size;
                    hover_place_y = base.y + best_hit.ny * block_size;
                    hover_place_z = base.z + best_hit.nz * block_size;
                    hover_face_nx = best_hit.nx;
                    hover_face_ny = best_hit.ny;
                    hover_face_nz = best_hit.nz;
                }
            } else {
                hover_has_block = false;
                hover_has_ground = false;
                hover_block_index = -1;
            }

            bool controls_blocked = (inventory_open && !close_dialog_active) || rotation_anim_active;
            if (hover_has_block && hover_block_index >= 0 && hover_block_index < (int)dungeon_blocks.size()) {
                selected_flags.assign(dungeon_blocks.size(), 0);
                selected_flags[hover_block_index] = 1;
                g_VoxelRenderer.setSelection(selected_flags);
                if (!controls_blocked) {
                    int tile_idx = 0;
                    std::map<std::string, int>::const_iterator it = tile_index_by_key.find(dungeon_blocks[hover_block_index].key);
                    if (it != tile_index_by_key.end())
                        tile_idx = it->second;
                    bool symmetric = (tile_idx >= 0 && tile_idx < (int)tile_is_symmetric.size()) ? tile_is_symmetric[tile_idx] : false;
                    if (!symmetric) {
                        const int arrow_keys[4] = {GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN};
                        bool rotated = false;
                        bool down_left = (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS);
                        bool down_right = (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS);
                        bool down_up = (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS);
                        bool down_down = (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
                        bool edge_left = down_left && !arrow_was_down[0];
                        bool edge_right = down_right && !arrow_was_down[1];
                        bool edge_up = down_up && !arrow_was_down[2];
                        bool edge_down = down_down && !arrow_was_down[3];

                        float end_rx = dungeon_blocks[hover_block_index].rot_x_deg;
                        float end_ry = dungeon_blocks[hover_block_index].rot_y_deg;
                        float end_rz = dungeon_blocks[hover_block_index].rot_z_deg;
                        if (edge_left || edge_right) {
                            const int dir = edge_right ? 1 : -1;
                            const voxel::VoxelRenderer::Block& blk = dungeon_blocks[hover_block_index];
                            DiceOrientation orientation = OrientationFromBlock(blk);
                            ApplyWorldRotation90(&orientation, raidbuilder::AxisId::Y, dir);
                            voxel::VoxelRenderer::Block temp = blk;
                            ApplyOrientationToBlock(orientation, &temp);
                            end_rx = temp.rot_x_deg;
                            end_ry = temp.rot_y_deg;
                            end_rz = temp.rot_z_deg;
                            rotated = true;
                        }

                        if (edge_up || edge_down) {
                            // Camera-relative roll for all blocks:
                            // Up = tip top face away from camera (opposite forward).
                            // Down = tip top face toward camera (forward).
                            const float abs_fwd_x = std::fabs(forward_x);
                            const float abs_fwd_z = std::fabs(forward_z);
                            const bool use_z = abs_fwd_z >= abs_fwd_x;
                            const raidbuilder::AxisId axis = use_z ? raidbuilder::AxisId::X : raidbuilder::AxisId::Z;
                            const float fwd_sign = use_z ? forward_z : forward_x;
                            const int target_sign = (edge_up ? 1 : -1) * ((fwd_sign >= 0.0f) ? 1 : -1);
                            const int dir = (axis == raidbuilder::AxisId::X)
                                                ? ((target_sign >= 0) ? 1 : -1)
                                                : ((target_sign >= 0) ? -1 : 1);
                            const voxel::VoxelRenderer::Block& blk = dungeon_blocks[hover_block_index];
                            DiceOrientation orientation = OrientationFromBlock(blk);
                            ApplyWorldRotation90(&orientation, axis, dir);
                            voxel::VoxelRenderer::Block temp = blk;
                            ApplyOrientationToBlock(orientation, &temp);
                            end_rx = temp.rot_x_deg;
                            end_ry = temp.rot_y_deg;
                            end_rz = temp.rot_z_deg;
                            rotated = true;
                        }
                        arrow_was_down[0] = down_left;
                        arrow_was_down[1] = down_right;
                        arrow_was_down[2] = down_up;
                        arrow_was_down[3] = down_down;
                        if (rotated) {
                            auto shortest_delta = [](float start_deg, float target_deg) -> float {
                                float diff = std::fmod(target_deg - start_deg, 360.0f);
                                if (diff > 180.0f)
                                    diff -= 360.0f;
                                if (diff < -180.0f)
                                    diff += 360.0f;
                                return diff;
                            };
                            rotation_anim_active = true;
                            rotation_anim_block = hover_block_index;
                            rotation_anim_start = glfwGetTime();
                            rotation_anim_start_rx = dungeon_blocks[hover_block_index].rot_x_deg;
                            rotation_anim_start_ry = dungeon_blocks[hover_block_index].rot_y_deg;
                            rotation_anim_start_rz = dungeon_blocks[hover_block_index].rot_z_deg;
                            rotation_anim_target_rx = end_rx;
                            rotation_anim_target_ry = end_ry;
                            rotation_anim_target_rz = end_rz;
                            // Animate along the shortest arc, then snap to the logical target at the end.
                            rotation_anim_end_rx = rotation_anim_start_rx + shortest_delta(rotation_anim_start_rx, rotation_anim_target_rx);
                            rotation_anim_end_ry = rotation_anim_start_ry + shortest_delta(rotation_anim_start_ry, rotation_anim_target_ry);
                            rotation_anim_end_rz = rotation_anim_start_rz + shortest_delta(rotation_anim_start_rz, rotation_anim_target_rz);
                            dungeon_blocks[hover_block_index].rot_x_deg = rotation_anim_target_rx;
                            dungeon_blocks[hover_block_index].rot_y_deg = rotation_anim_target_ry;
                            dungeon_blocks[hover_block_index].rot_z_deg = rotation_anim_target_rz;
                            g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
                            dirty = true;
                            UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
                        }
                    } else {
                        const int arrow_keys[4] = {GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN};
                        for (int ai = 0; ai < 4; ++ai)
                            arrow_was_down[ai] = (glfwGetKey(window, arrow_keys[ai]) == GLFW_PRESS);
                    }
                } else {
                    // While controls are blocked (e.g. during rotation animation), keep key state latched
                    // so a held key does not retrigger on the next frame.
                    const int arrow_keys[4] = {GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN};
                    for (int ai = 0; ai < 4; ++ai)
                        arrow_was_down[ai] = (glfwGetKey(window, arrow_keys[ai]) == GLFW_PRESS);
                }
            } else {
                selected_flags.assign(dungeon_blocks.size(), 0);
                g_VoxelRenderer.setSelection(selected_flags);
                for (int ai = 0; ai < 4; ++ai)
                    arrow_was_down[ai] = false;
            }

            if (!close_dialog_active && left_state == GLFW_PRESS && !left_was_down && hover_has_block && hover_block_index >= 0 &&
                hover_block_index < (int)dungeon_blocks.size()) {
                remove_solid_block(dungeon_blocks[hover_block_index]);
                dungeon_blocks.erase(dungeon_blocks.begin() + hover_block_index);
                selected_flags.assign(dungeon_blocks.size(), 0);
                g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
                g_VoxelRenderer.setSelection(selected_flags);
                hover_has_block = false;
                hover_block_index = -1;
                dirty = true;
                UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
            }

            if (!close_dialog_active && ghost_enabled) {
                if (right_state == GLFW_PRESS) {
                    if (!painting) {
                        painting = true;
                        paint_count = 0;
                        paint_last_valid = false;
                        paint_dir_valid = false;
                        ghost_hover_last = false;
                        if (has_best) {
                            float place_x = hover_place_x;
                            float place_y = hover_place_y;
                            float place_z = hover_place_z;
                            float dir_x = hover_face_nx;
                            float dir_y = hover_face_ny;
                            float dir_z = hover_face_nz;
                            float dx = place_x - camera_x;
                            float dy = place_y - camera_y;
                            float dz = place_z - camera_z;
                            float dist2 = dx * dx + dy * dy + dz * dz;
                            if (dist2 <= max_place_distance * max_place_distance &&
                                FindBlockAt(dungeon_blocks, place_x, place_y, place_z, 0.001f) < 0) {
                                voxel::VoxelRenderer::Block new_block;
                                new_block.x = place_x;
                                new_block.y = place_y;
                                new_block.z = place_z;
                                new_block.tex_index = active_tex_index;
                                new_block.key = active_tile_key;
                                new_block.mesh_index = active_mesh_index;
                                dungeon_blocks.push_back(new_block);
                                add_solid_block(new_block);
                                selected_flags.assign(dungeon_blocks.size(), 0);
                                selected_flags.back() = 1;
                                g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
                                g_VoxelRenderer.setSelection(selected_flags);
                                dirty = true;
                                UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
                                paint_last_x = place_x;
                                paint_last_y = place_y;
                                paint_last_z = place_z;
                                paint_last_valid = true;
                                paint_dir_x = dir_x;
                                paint_dir_y = dir_y;
                                paint_dir_z = dir_z;
                                paint_anchor_x = place_x;
                                paint_anchor_y = place_y;
                                paint_anchor_z = place_z;
                                paint_dir_valid = true;
                                paint_count = 1;
                            }
                        }
                    } else if (paint_dir_valid && paint_count < max_paint_blocks) {
                        float free_x = paint_last_x + paint_dir_x * block_size;
                        float free_y = paint_last_y + paint_dir_y * block_size;
                        float free_z = paint_last_z + paint_dir_z * block_size;
                        bool free_ok = (FindBlockAt(dungeon_blocks, free_x, free_y, free_z, 0.001f) < 0);
                        bool ghost_hit = false;
                        if (free_ok) {
                            float half = block_size * 0.5f;
                            float bmin[3] = {free_x - half, free_y - half, free_z - half};
                            float bmax[3] = {free_x + half, free_y + half, free_z + half};
                            float t = 0.0f;
                            float nx = 0.0f, ny = 0.0f, nz = 0.0f;
                            if (RayAabb(ray_origin, ray_dir, bmin, bmax, &t, &nx, &ny, &nz)) {
                                float nearest_scene = 1e30f;
                                if (has_block)
                                    nearest_scene = block_hit.t;
                                if (has_ground && ground_hit.t < nearest_scene)
                                    nearest_scene = ground_hit.t;
                                if (t <= nearest_scene + 0.001f)
                                    ghost_hit = true;
                            }
                        }
                        if (ghost_hit && !ghost_hover_last) {
                            voxel::VoxelRenderer::Block new_block;
                            new_block.x = free_x;
                            new_block.y = free_y;
                            new_block.z = free_z;
                            new_block.tex_index = active_tex_index;
                            new_block.key = active_tile_key;
                            new_block.mesh_index = active_mesh_index;
                            dungeon_blocks.push_back(new_block);
                            add_solid_block(new_block);
                            selected_flags.assign(dungeon_blocks.size(), 0);
                            selected_flags.back() = 1;
                            g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
                            g_VoxelRenderer.setSelection(selected_flags);
                            dirty = true;
                            UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
                            paint_last_x = free_x;
                            paint_last_y = free_y;
                            paint_last_z = free_z;
                            paint_last_valid = true;
                            paint_count++;
                        }
                        ghost_hover_last = ghost_hit;
                    }
                } else {
                    painting = false;
                    paint_last_valid = false;
                    paint_dir_valid = false;
                    ghost_hover_last = false;
                }
            } else if (!close_dialog_active && right_state == GLFW_PRESS && !right_was_down && has_best) {
                float place_x = hover_place_x;
                float place_y = hover_place_y;
                float place_z = hover_place_z;
                float dir_x = hover_face_nx;
                float dir_y = hover_face_ny;
                float dir_z = hover_face_nz;
                float dx = place_x - camera_x;
                float dy = place_y - camera_y;
                float dz = place_z - camera_z;
                float dist2 = dx * dx + dy * dy + dz * dz;
                float free_x = place_x;
                float free_y = place_y;
                float free_z = place_z;
                bool free_ok = FindNextFreePlacement(dungeon_blocks,
                                                     place_x, place_y, place_z,
                                                     dir_x, dir_y, dir_z,
                                                     block_size, max_paint_blocks,
                                                     &free_x, &free_y, &free_z);
                if (dist2 <= max_place_distance * max_place_distance && free_ok) {
                    voxel::VoxelRenderer::Block new_block;
                    new_block.x = free_x;
                    new_block.y = free_y;
                    new_block.z = free_z;
                    new_block.tex_index = active_tex_index;
                    new_block.key = active_tile_key;
                    new_block.mesh_index = active_mesh_index;
                    dungeon_blocks.push_back(new_block);
                    add_solid_block(new_block);
                    selected_flags.assign(dungeon_blocks.size(), 0);
                    selected_flags.back() = 1;
                    g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
                    g_VoxelRenderer.setSelection(selected_flags);
                    dirty = true;
                    UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
                }
            }
            right_was_down = (right_state == GLFW_PRESS);
            left_was_down = (left_state == GLFW_PRESS);
        }

        g_VoxelRenderer.setCamera(camera_x, camera_y, camera_z, camera_yaw, camera_pitch);

        int fb_width, fb_height;
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height)) {
            ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
            g_MainWindowData.FrameIndex = 0;
            g_SwapChainRebuild = false;
            g_VoxelRenderer.resizePickResources((uint32_t)fb_width, (uint32_t)fb_height);
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool ui_capture_mouse = ImGui::GetIO().WantCaptureMouse;
        if (!edit_mode && !close_dialog_active)
            ui_capture_mouse = false;

        if (close_pending) {
            ImGui::OpenPopup("Unsaved Changes");
            close_pending = false;
            close_dialog_active = true;
        }
        bool close_popup_open = true;
        if (ImGui::BeginPopupModal("Unsaved Changes", &close_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("You have unsaved changes.");
            ImGui::Separator();
            if (ImGui::Button("Save")) {
                if (SaveDungeonAuto(current_dungeon_path, dungeon_blocks, block_size, tiles)) {
                    saved_state.last_file_path = current_dungeon_path;
                    SaveAppState(state_path, saved_state);
                    dirty = false;
                    UpdateWindowTitle(window, base_window_title, current_dungeon_path, dirty);
                    close_request = true;
                }
                close_dialog_active = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Ignore")) {
                close_request = true;
                close_dialog_active = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                close_dialog_active = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        close_dialog_active = ImGui::IsPopupOpen("Unsaved Changes");
        if (close_dialog_active)
            ui_capture_mouse = true;

        if (edit_mode && selecting) {
            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            ImU32 color = IM_COL32(255, 255, 0, 255);
            draw_list->AddRect(select_start, select_end, color, 0.0f, 0, 2.0f);
        }
        if (!edit_mode) {
            ImDrawList* draw_list = ImGui::GetForegroundDrawList();
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
            float size = 6.0f * main_scale;
            ImU32 color = IM_COL32(255, 255, 255, 220);
            draw_list->AddLine(ImVec2(center.x - size, center.y), ImVec2(center.x + size, center.y), color, 2.0f);
            draw_list->AddLine(ImVec2(center.x, center.y - size), ImVec2(center.x, center.y + size), color, 2.0f);

            float aspect = (vp->Size.y > 0.0f) ? (vp->Size.x / vp->Size.y) : 1.0f;
            Mat4 proj = mat4Perspective(60.0f * (PI_F / 180.0f), aspect, 0.1f, 100.0f);
            proj.m[5] *= -1.0f;
            float cp = std::cos(camera_pitch);
            float sp = std::sin(camera_pitch);
            float cy = std::cos(camera_yaw);
            float sy = std::sin(camera_yaw);
            float fx = cp * cy;
            float fy = sp;
            float fz = cp * sy;
            Mat4 view = mat4LookAt(camera_x, camera_y, camera_z,
                                   camera_x + fx, camera_y + fy, camera_z + fz,
                                   0.0f, 1.0f, 0.0f);
            Mat4 mvp = mat4Multiply(proj, view);

            if (hover_has_block && hover_block_index >= 0 && hover_block_index < (int)dungeon_blocks.size()) {
                const voxel::VoxelRenderer::Block& base = dungeon_blocks[hover_block_index];
                float half = block_size * 0.5f;
                float center_x = base.x + hover_face_nx * half;
                float center_y = base.y + hover_face_ny * half;
                float center_z = base.z + hover_face_nz * half;

                float tx = 0.0f, ty = 0.0f, tz = 0.0f;
                float bx = 0.0f, by = 0.0f, bz = 0.0f;
                if (std::fabs(hover_face_nx) > 0.5f) {
                    ty = 1.0f;
                    bz = 1.0f;
                } else if (std::fabs(hover_face_ny) > 0.5f) {
                    tx = 1.0f;
                    bz = 1.0f;
                } else {
                    tx = 1.0f;
                    by = 1.0f;
                }

                float corners[4][3] = {
                    {center_x + (tx + bx) * half, center_y + (ty + by) * half, center_z + (tz + bz) * half},
                    {center_x + (tx - bx) * half, center_y + (ty - by) * half, center_z + (tz - bz) * half},
                    {center_x + (-tx - bx) * half, center_y + (-ty - by) * half, center_z + (-tz - bz) * half},
                    {center_x + (-tx + bx) * half, center_y + (-ty + by) * half, center_z + (-tz + bz) * half},
                };
                ImVec2 points[4];
                bool ok = true;
                for (int i = 0; i < 4; ++i) {
                    if (!ProjectToScreen(mvp, corners[i][0], corners[i][1], corners[i][2], vp->Pos, vp->Size, &points[i])) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    ImU32 face_color = IM_COL32(255, 230, 0, 220);
                    draw_list->AddPolyline(points, 4, face_color, ImDrawFlags_Closed, 2.5f);
                }
            }

            if (hover_has_ground) {
                float half = block_size * 0.5f;
                float corners[4][3] = {
                    {hover_place_x + half, 0.0f, hover_place_z + half},
                    {hover_place_x - half, 0.0f, hover_place_z + half},
                    {hover_place_x - half, 0.0f, hover_place_z - half},
                    {hover_place_x + half, 0.0f, hover_place_z - half},
                };
                ImVec2 points[4];
                bool ok = true;
                for (int i = 0; i < 4; ++i) {
                    if (!ProjectToScreen(mvp, corners[i][0], corners[i][1], corners[i][2], vp->Pos, vp->Size, &points[i])) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    ImU32 ground_color = IM_COL32(255, 230, 0, 200);
                    draw_list->AddPolyline(points, 4, ground_color, ImDrawFlags_Closed, 2.0f);
                }
            }

            if (ghost_enabled && (hover_has_block || hover_has_ground)) {
                float half = block_size * 0.5f;
                float ghost_x = hover_place_x;
                float ghost_y = hover_place_y;
                float ghost_z = hover_place_z;
                float dir_x = paint_dir_valid ? paint_dir_x : hover_face_nx;
                float dir_y = paint_dir_valid ? paint_dir_y : hover_face_ny;
                float dir_z = paint_dir_valid ? paint_dir_z : hover_face_nz;
                float free_x = ghost_x;
                float free_y = ghost_y;
                float free_z = ghost_z;
                bool free_ok = true;
                if (paint_dir_valid) {
                    free_x = paint_last_x + paint_dir_x * block_size;
                    free_y = paint_last_y + paint_dir_y * block_size;
                    free_z = paint_last_z + paint_dir_z * block_size;
                    free_ok = (FindBlockAt(dungeon_blocks, free_x, free_y, free_z, 0.001f) < 0);
                } else {
                    free_ok = FindNextFreePlacement(dungeon_blocks,
                                                    ghost_x, ghost_y, ghost_z,
                                                    dir_x, dir_y, dir_z,
                                                    block_size, max_paint_blocks,
                                                    &free_x, &free_y, &free_z);
                }
                float center_x = free_x;
                float center_y = free_y;
                float center_z = free_z;
                float corners[8][3] = {
                    {center_x - half, center_y - half, center_z - half},
                    {center_x + half, center_y - half, center_z - half},
                    {center_x + half, center_y + half, center_z - half},
                    {center_x - half, center_y + half, center_z - half},
                    {center_x - half, center_y - half, center_z + half},
                    {center_x + half, center_y - half, center_z + half},
                    {center_x + half, center_y + half, center_z + half},
                    {center_x - half, center_y + half, center_z + half},
                };
                ImVec2 p[8];
                bool ok = free_ok;
                for (int i = 0; i < 8; ++i) {
                    if (!ProjectToScreen(mvp, corners[i][0], corners[i][1], corners[i][2], vp->Pos, vp->Size, &p[i])) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    ImU32 ghost_fill = IM_COL32(255, 255, 255, 40);
                    ImU32 ghost_color = IM_COL32(255, 255, 255, 140);
                    const int faces[6][4] = {
                        {0, 1, 2, 3},
                        {4, 5, 6, 7},
                        {0, 1, 5, 4},
                        {2, 3, 7, 6},
                        {1, 2, 6, 5},
                        {3, 0, 4, 7}
                    };
                    for (int f = 0; f < 6; ++f) {
                        ImVec2 face[4] = {p[faces[f][0]], p[faces[f][1]], p[faces[f][2]], p[faces[f][3]]};
                        draw_list->AddConvexPolyFilled(face, 4, ghost_fill);
                    }
                    const int edges[12][2] = {
                        {0,1},{1,2},{2,3},{3,0},
                        {4,5},{5,6},{6,7},{7,4},
                        {0,4},{1,5},{2,6},{3,7}
                    };
                    for (int i = 0; i < 12; ++i) {
                        draw_list->AddLine(p[edges[i][0]], p[edges[i][1]], ghost_color, 1.5f);
                    }
                }
            }
        }

        if (close_request) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        bool play_clicked = false;
        if (!close_dialog_active && !edit_mode) {
            // Action bar (10 slots) anchored at the bottom.
            const float margin = 12.0f * main_scale;
            const float target_h = 64.0f * main_scale;
            ImVec2 bar_size(viewport->Size.x - margin * 2.0f, target_h);
            if (bar_size.x < 320.0f * main_scale)
                bar_size.x = 320.0f * main_scale;
            ImVec2 bar_pos(viewport->Pos.x + (viewport->Size.x - bar_size.x) * 0.5f,
                           viewport->Pos.y + viewport->Size.y - bar_size.y - margin);
            ImGui::SetNextWindowPos(bar_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(bar_size, ImGuiCond_Always);
            ImGuiWindowFlags bar_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                         ImGuiWindowFlags_NoScrollbar;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * main_scale, 6.0f * main_scale));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * main_scale, 0.0f));
            if (ImGui::Begin("ActionBar", nullptr, bar_flags)) {
                float avail_w = ImGui::GetContentRegionAvail().x;
                float spacing = ImGui::GetStyle().ItemSpacing.x;
                float slot_w = (avail_w - spacing * 9.0f) / 10.0f;
                if (slot_w < 24.0f * main_scale)
                    slot_w = 24.0f * main_scale;
                ImVec2 slot_size(slot_w, bar_size.y - 12.0f * main_scale);
                for (int i = 0; i < 10; ++i) {
                    int tile_idx = (i >= 0 && i < (int)action_slots.size()) ? action_slots[i] : -1;
                    std::string name = "-";
                    if (tile_idx >= 0 && tile_idx < (int)tiles.size())
                        name = tiles[tile_idx].name.empty() ? tiles[tile_idx].key : tiles[tile_idx].name;
                    int slot_num = (i == 9) ? 0 : (i + 1);
                    std::string label = std::to_string(slot_num) + ":" + name;
                    if (i == active_slot)
                        label = "[" + label + "]";
                    ImGui::PushID(i);
                    if (ImGui::Button(label.c_str(), slot_size))
                        active_slot = i;
                    ImGui::PopID();
                    if (i < 9)
                        ImGui::SameLine();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);

            if (inventory_open) {
                ImGui::SetNextWindowSize(ImVec2(720.0f * main_scale, 420.0f * main_scale), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Inventory", &inventory_open)) {
                    ImGui::Text("Active slot: %d", (active_slot == 9) ? 0 : (active_slot + 1));
                    if (ImGui::BeginTabBar("InventoryTabs")) {
                        for (std::map<std::string, std::vector<int> >::const_iterator it = tiles_by_category.begin();
                             it != tiles_by_category.end(); ++it) {
                            if (!ImGui::BeginTabItem(it->first.c_str()))
                                continue;
                            const std::vector<int>& idxs = it->second;
                            for (size_t ti = 0; ti < idxs.size(); ++ti) {
                                int idx = idxs[ti];
                                if (idx < 0 || idx >= (int)tiles.size())
                                    continue;
                                std::string label = tiles[idx].name.empty() ? tiles[idx].key : tiles[idx].name;
                                bool selected = (active_slot >= 0 && active_slot < (int)action_slots.size() &&
                                                 action_slots[active_slot] == idx);
                                if (ImGui::Selectable(label.c_str(), selected)) {
                                    action_slots[active_slot] = idx;
                                    actionbar_dirty = true;
                                }
                            }
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }
                ImGui::End();
            }
        }

        if (actionbar_dirty) {
            SaveActionBar(actionbar_path, action_slots, tiles, active_slot);
            actionbar_dirty = false;
        }

        InspectorContext inspector_context;
        inspector_context.blocks = &dungeon_blocks;
        inspector_context.selected_flags = &selected_flags;
        inspector_context.block_size = block_size;
        inspector_context.dirty = &dirty;
        inspector_context.base_window_title = &base_window_title;
        inspector_context.current_dungeon_path = &current_dungeon_path;
        inspector_context.window = window;
        inspector_context.close_dialog_active = &close_dialog_active;
        inspector_context.edit_mode = &edit_mode;
        inspector_context.hover_has_block = &hover_has_block;
        inspector_context.hover_block_index = &hover_block_index;
        inspector_context.main_scale = &main_scale;
        inspector_context.saved_state = &saved_state;
        ui_document.setPropertyPanelCallback(&RenderInspectorPanel, &inspector_context);

        if (edit_mode) {
            ui_document.render(viewport, font_15, &play_clicked);
            if (play_clicked) {
                edit_mode = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                first_mouse = true;
                selecting = false;
                right_was_down = false;
                hover_has_block = false;
                hover_has_ground = false;
                hover_block_index = -1;
            }
        }

        if (edit_mode) {
            bool ui_capture_mouse_for_selection = ImGui::GetIO().WantCaptureMouse;
            int win_width = 0;
            int win_height = 0;
            glfwGetWindowSize(window, &win_width, &win_height);
            const float top_h = (float)ui_window.dock.top_height * main_scale;
            const float bottom_h = (float)ui_window.dock.bottom_height * main_scale;
            const float left_w = (float)ui_window.dock.left_width * main_scale;
            const float right_w = (float)ui_window.dock.right_width * main_scale;
            const bool mouse_in_viewport =
                mouse_x >= left_w &&
                mouse_x <= (win_width - right_w) &&
                mouse_y >= top_h &&
                mouse_y <= (win_height - bottom_h);
            if (!close_dialog_active && mouse_in_viewport) {
                int left_state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
                if (left_state == GLFW_PRESS && !selecting) {
                    selecting = true;
                    select_start = ImVec2((float)mouse_x, (float)mouse_y);
                    select_end = select_start;
                } else if (left_state == GLFW_PRESS && selecting) {
                    select_end = ImVec2((float)mouse_x, (float)mouse_y);
                } else if (left_state == GLFW_RELEASE && selecting) {
                    selecting = false;
                }

                if (selecting || left_state == GLFW_PRESS) {
                    float min_x = (select_start.x < select_end.x) ? select_start.x : select_end.x;
                    float max_x = (select_start.x > select_end.x) ? select_start.x : select_end.x;
                    float min_y = (select_start.y < select_end.y) ? select_start.y : select_end.y;
                    float max_y = (select_start.y > select_end.y) ? select_start.y : select_end.y;

                    int fb_width, fb_height;
                    int win_width, win_height;
                    glfwGetFramebufferSize(window, &fb_width, &fb_height);
                    glfwGetWindowSize(window, &win_width, &win_height);
                    float scale_x = (win_width > 0) ? (float)fb_width / (float)win_width : 1.0f;
                    float scale_y = (win_height > 0) ? (float)fb_height / (float)win_height : 1.0f;

                    uint32_t rect_x = (uint32_t)(min_x * scale_x);
                    uint32_t rect_y = (uint32_t)(min_y * scale_y);
                    uint32_t rect_w = (uint32_t)((max_x - min_x) * scale_x);
                    uint32_t rect_h = (uint32_t)((max_y - min_y) * scale_y);
                    if (rect_w == 0) rect_w = 1;
                    if (rect_h == 0) rect_h = 1;

                    if (g_VoxelRenderer.pickRect(rect_x, rect_y, rect_w, rect_h, &selected_flags)) {
                        g_VoxelRenderer.setSelection(selected_flags);
                    }
                }
            } else if (selecting) {
                selecting = false;
            }
        }

        ImGui::Render();
        ImDrawData* main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
        wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
        wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
        wd->ClearValue.color.float32[3] = clear_color.w;
        if (!main_is_minimized)
            FrameRender(wd, main_draw_data);
        FramePresent(wd);
    }

    SaveActionBar(actionbar_path, action_slots, tiles, active_slot);

    AppState out_state;
    if (ui_window.state.pos) {
        glfwGetWindowPos(window, &out_state.pos_x, &out_state.pos_y);
        out_state.has_pos = true;
    }
    if (ui_window.state.size) {
        glfwGetWindowSize(window, &out_state.size_x, &out_state.size_y);
        out_state.has_size = true;
    }
    if (ui_window.state.maximized) {
        out_state.maximized = (glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0);
    }
    if (ui_window.state.last_file_path)
        out_state.last_file_path = current_dungeon_path;
    SaveAppState(state_path, out_state);

    vkDeviceWaitIdle(g_Device);
    g_VoxelRenderer.shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkanWindow();
    CleanupVulkan();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

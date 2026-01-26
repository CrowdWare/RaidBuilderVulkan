// RaidBuilder - Vulkan + ImGui + SMLUI

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "sml_ui.h"
#include "sml_parser.h"
#include "voxel_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <cmath>
#include <map>
#include <algorithm>

#define PI_F 3.1415926f

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

static bool ParseDungeon(const std::string& text,
                         float block_size,
                         std::vector<voxel::VoxelRenderer::Block>* out_blocks,
                         std::string* out_block_texture,
                         std::string* error_message) {
    class DungeonHandler : public sml::SmlHandler {
    public:
        std::string lines;
        std::vector<std::string> stack;
        std::string tile_key;
        std::string tile_model;
        std::map<std::string, std::string> tile_models;

        void startElement(const std::string& name) override { stack.push_back(name); }
        void onProperty(const std::string& name, const sml::PropertyValue& value) override {
            if (stack.empty())
                return;
            if (stack.back() == "TileMap" && name == "lines" && value.type == sml::PropertyValue::String)
                lines = value.string_value;
            if (stack.back() == "Tile" && name == "key" && value.type == sml::PropertyValue::String)
                tile_key = value.string_value;
            if (stack.back() == "Tile" && name == "model" && value.type == sml::PropertyValue::String)
                tile_model = value.string_value;
        }
        void endElement(const std::string& name) override {
            if (name == "Tile") {
                if (!tile_key.empty() && !tile_model.empty())
                    tile_models[tile_key] = tile_model;
                tile_key.clear();
                tile_model.clear();
            }
            if (!stack.empty())
                stack.pop_back();
        }
    };

    DungeonHandler handler;
    try {
        sml::SmlSaxParser parser(text);
        parser.parse(handler);
    } catch (const sml::SmlParseException& e) {
        if (error_message)
            *error_message = e.what();
        return false;
    }

    if (out_block_texture) {
        std::map<std::string, std::string>::const_iterator it = handler.tile_models.find("s");
        if (it != handler.tile_models.end())
            *out_block_texture = it->second;
    }

    if (handler.lines.empty()) {
        out_blocks->clear();
        return true;
    }

    std::vector<voxel::VoxelRenderer::Block> blocks;
    std::istringstream iss(handler.lines);
    std::string line;
    int current_layer = 0;
    int max_cols = 0;
    int max_rows = 0;
    std::vector<int> row_counts(1, 0);

    while (std::getline(iss, line)) {
        if (line.empty())
            continue;
        if (line.size() > 1 && line[0] == '#') {
            current_layer = std::atoi(line.c_str() + 1);
            if ((int)row_counts.size() <= current_layer)
                row_counts.resize(current_layer + 1, 0);
            continue;
        }

        std::istringstream row_stream(line);
        std::string token;
        int col = 0;
        while (row_stream >> token) {
            std::string id = token;
            size_t colon = id.find(':');
            if (colon != std::string::npos)
                id = id.substr(0, colon);
            if (id == "s") {
                voxel::VoxelRenderer::Block block;
                block.x = (float)col;
                block.y = (float)current_layer;
                block.z = (float)row_counts[current_layer];
                blocks.push_back(block);
            }
            col++;
        }
        if (col > max_cols)
            max_cols = col;
        row_counts[current_layer] += 1;
    }

    for (size_t i = 0; i < row_counts.size(); ++i)
        if (row_counts[i] > max_rows)
            max_rows = row_counts[i];

    float offset_x = -0.5f * (max_cols - 1) * block_size;
    float offset_z = -0.5f * (max_rows - 1) * block_size;
    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].x = blocks[i].x * block_size + offset_x;
        blocks[i].z = blocks[i].z * block_size + offset_z;
        blocks[i].y = blocks[i].y * block_size + block_size * 0.5f;
    }

    *out_blocks = blocks;
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
        float bmin[3] = {blocks[i].x - half, blocks[i].y - half, blocks[i].z - half};
        float bmax[3] = {blocks[i].x + half, blocks[i].y + half, blocks[i].z + half};
        float t = 0.0f;
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        if (RayAabb(origin, dir, bmin, bmax, &t, &nx, &ny, &nz)) {
            if (t < best_t) {
                best_t = t;
                best_index = (int)i;
                best_nx = nx;
                best_ny = ny;
                best_nz = nz;
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
    const char* ui_path = "RaidBuilder/UI.sml";
    smlui::UiDocument ui_document;
    std::string parse_error;
    std::string ui_text;
    if (!LoadFileText(ui_path, &ui_text)) {
        fprintf(stderr, "SML load error: could not read %s\n", ui_path);
    } else if (!ui_document.parseFromString(ui_text, &parse_error)) {
        fprintf(stderr, "SML parse error: %s\n", parse_error.c_str());
    }

    const char* dungeon_path = "RaidBuilder/dungeon.sml";
    std::vector<voxel::VoxelRenderer::Block> dungeon_blocks;
    std::vector<unsigned char> selected_flags;
    std::string block_texture_path = "RaidBuilder/assets/textures/raid_stone.png";
    std::string dungeon_text;
    std::string dungeon_error;
    if (!LoadFileText(dungeon_path, &dungeon_text)) {
        fprintf(stderr, "Dungeon load error: could not read %s\n", dungeon_path);
    } else if (!ParseDungeon(dungeon_text, 0.6f, &dungeon_blocks, &block_texture_path, &dungeon_error)) {
        fprintf(stderr, "Dungeon parse error: %s\n", dungeon_error.c_str());
    }
    block_texture_path = ResolveAssetPath(block_texture_path, "RaidBuilder/");
    selected_flags.assign(dungeon_blocks.size(), 0);

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    const smlui::UiWindow& ui_window = ui_document.window();
    const char* window_title = ui_window.title.empty() ? "RaidBuilder" : ui_window.title.c_str();
    GLFWwindow* window = glfwCreateWindow((int)(ui_window.size.x * main_scale), (int)(ui_window.size.y * main_scale), window_title, nullptr, nullptr);
    if (!glfwVulkanSupported()) {
        printf("GLFW: Vulkan Not Supported\n");
        return 1;
    }

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

    if (!g_VoxelRenderer.init(g_Device, g_PhysicalDevice, g_Queue, g_QueueFamily, wd->RenderPass,
                              "RaidBuilder/shaders/world.vert.spv",
                              "RaidBuilder/shaders/world.frag.spv",
                              "RaidBuilder/shaders/pick.vert.spv",
                              "RaidBuilder/shaders/pick.frag.spv",
                              "RaidBuilder/assets/textures/raid_ground.png",
                              block_texture_path.c_str())) {
        fprintf(stderr, "VoxelRenderer init failed (missing shaders?)\n");
    }
    g_VoxelRenderer.setBlocks(dungeon_blocks, 0.6f);
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
    glfwSetCursorPos(window, (ui_window.size.x * main_scale) * 0.5, (ui_window.size.y * main_scale) * 0.5);

    bool edit_mode = true;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    double last_mouse_x = 0.0;
    double last_mouse_y = 0.0;
    bool first_mouse = true;
    float camera_x = 6.0f;
    float camera_z = 6.0f;
    const float block_size = 0.6f;
    const float eye_height = 1.6f;
    float camera_y = eye_height;
    float vertical_velocity = 0.0f;
    float camera_yaw = 3.1415926f * 0.75f;
    float camera_pitch = -0.5f;
    bool f_was_down = false;
    bool e_was_down = false;
    bool q_was_down = false;
    bool g_was_down = false;
    bool right_was_down = false;
    bool painting = false;
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

    ImVec4 clear_color = ImVec4(0.18f, 0.35f, 0.75f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now_time = glfwGetTime();
        float dt = (float)(now_time - last_time);
        last_time = now_time;

        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
            if (!f_was_down) {
                edit_mode = !edit_mode;
                glfwSetInputMode(window, GLFW_CURSOR, edit_mode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                first_mouse = true;
                selecting = false;
                right_was_down = false;
                hover_has_block = false;
                hover_has_ground = false;
                hover_block_index = -1;
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

        if (!edit_mode) {
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
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
                camera_x += forward_x * speed * dt;
                camera_z += forward_z * speed * dt;
            }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
                camera_x -= forward_x * speed * dt;
                camera_z -= forward_z * speed * dt;
            }
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                camera_x -= right_x * speed * dt;
                camera_z -= right_z * speed * dt;
            }
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                camera_x += right_x * speed * dt;
                camera_z += right_z * speed * dt;
            }

            const float gravity = -9.8f;
            vertical_velocity += gravity * dt;
            camera_y += vertical_velocity * dt;
            if (camera_y < eye_height) {
                camera_y = eye_height;
                vertical_velocity = 0.0f;
            }

            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && camera_y <= eye_height + 0.001f) {
                float jump_speed = std::sqrt(2.0f * 9.8f * block_size);
                vertical_velocity = jump_speed;
            }
        } else {
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
                if (!e_was_down)
                    camera_y += block_size;
                e_was_down = true;
            } else {
                e_was_down = false;
            }
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
                if (!q_was_down)
                    camera_y -= block_size;
                q_was_down = true;
            } else {
                q_was_down = false;
            }

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

            if (ghost_enabled) {
                if (right_state == GLFW_PRESS) {
                    if (!painting) {
                        painting = true;
                        paint_count = 0;
                        paint_last_valid = false;
                        paint_dir_valid = false;
                    }
                    if (has_best && paint_count < max_paint_blocks) {
                        float place_x = hover_place_x;
                        float place_y = hover_place_y;
                        float place_z = hover_place_z;
                        float dir_x = paint_dir_valid ? paint_dir_x : hover_face_nx;
                        float dir_y = paint_dir_valid ? paint_dir_y : hover_face_ny;
                        float dir_z = paint_dir_valid ? paint_dir_z : hover_face_nz;
                        if (paint_dir_valid) {
                            ComputeAxisPlacement(true,
                                                 paint_dir_x, paint_dir_y, paint_dir_z,
                                                 paint_anchor_x, paint_anchor_y, paint_anchor_z,
                                                 block_size,
                                                 place_x, place_y, place_z,
                                                 &place_x, &place_y, &place_z);
                        }
                        float dx = place_x - camera_x;
                        float dy = place_y - camera_y;
                        float dz = place_z - camera_z;
                        float dist2 = dx * dx + dy * dy + dz * dz;
                        if (dist2 <= max_place_distance * max_place_distance) {
                            bool dir_ok = true;
                            if (paint_dir_valid) {
                                float rx = place_x - paint_anchor_x;
                                float ry = place_y - paint_anchor_y;
                                float rz = place_z - paint_anchor_z;
                                float dot = rx * paint_dir_x + ry * paint_dir_y + rz * paint_dir_z;
                                float off_x = rx - paint_dir_x * dot;
                                float off_y = ry - paint_dir_y * dot;
                                float off_z = rz - paint_dir_z * dot;
                                float off_len2 = off_x * off_x + off_y * off_y + off_z * off_z;
                                dir_ok = (dot >= -0.001f) && (off_len2 <= 0.0001f);
                            }
                            float free_x = place_x;
                            float free_y = place_y;
                            float free_z = place_z;
                            bool free_ok = FindNextFreePlacement(dungeon_blocks,
                                                                 place_x, place_y, place_z,
                                                                 dir_x, dir_y, dir_z,
                                                                 block_size, max_paint_blocks,
                                                                 &free_x, &free_y, &free_z);
                            bool same_as_last = paint_last_valid &&
                                                std::fabs(free_x - paint_last_x) < 0.001f &&
                                                std::fabs(free_y - paint_last_y) < 0.001f &&
                                                std::fabs(free_z - paint_last_z) < 0.001f;
                            if (free_ok && dir_ok && !same_as_last) {
                                voxel::VoxelRenderer::Block new_block = {place_x, place_y, place_z};
                                new_block.x = free_x;
                                new_block.y = free_y;
                                new_block.z = free_z;
                                dungeon_blocks.push_back(new_block);
                                selected_flags.assign(dungeon_blocks.size(), 0);
                                selected_flags.back() = 1;
                                g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
                                g_VoxelRenderer.setSelection(selected_flags);
                                paint_last_x = free_x;
                                paint_last_y = free_y;
                                paint_last_z = free_z;
                                paint_last_valid = true;
                                if (!paint_dir_valid) {
                                    paint_dir_x = dir_x;
                                    paint_dir_y = dir_y;
                                    paint_dir_z = dir_z;
                                    paint_anchor_x = free_x;
                                    paint_anchor_y = free_y;
                                    paint_anchor_z = free_z;
                                    paint_dir_valid = true;
                                }
                                paint_count++;
                            }
                        }
                    }
                } else {
                    painting = false;
                    paint_last_valid = false;
                    paint_dir_valid = false;
                }
            } else if (right_state == GLFW_PRESS && !right_was_down && has_best) {
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
                    voxel::VoxelRenderer::Block new_block = {free_x, free_y, free_z};
                    dungeon_blocks.push_back(new_block);
                    selected_flags.assign(dungeon_blocks.size(), 0);
                    selected_flags.back() = 1;
                    g_VoxelRenderer.setBlocks(dungeon_blocks, block_size);
                    g_VoxelRenderer.setSelection(selected_flags);
                }
            }
            right_was_down = (right_state == GLFW_PRESS);
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
                if (paint_dir_valid) {
                    ComputeAxisPlacement(true,
                                         paint_dir_x, paint_dir_y, paint_dir_z,
                                         paint_anchor_x, paint_anchor_y, paint_anchor_z,
                                         block_size,
                                         ghost_x, ghost_y, ghost_z,
                                         &ghost_x, &ghost_y, &ghost_z);
                }
                float dir_x = paint_dir_valid ? paint_dir_x : hover_face_nx;
                float dir_y = paint_dir_valid ? paint_dir_y : hover_face_ny;
                float dir_z = paint_dir_valid ? paint_dir_z : hover_face_nz;
                float free_x = ghost_x;
                float free_y = ghost_y;
                float free_z = ghost_z;
                bool free_ok = FindNextFreePlacement(dungeon_blocks,
                                                     ghost_x, ghost_y, ghost_z,
                                                     dir_x, dir_y, dir_z,
                                                     block_size, max_paint_blocks,
                                                     &free_x, &free_y, &free_z);
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

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        bool play_clicked = false;
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
            std::fill(selected_flags.begin(), selected_flags.end(), 0);
            g_VoxelRenderer.setSelection(selected_flags);
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

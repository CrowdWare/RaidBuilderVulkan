#pragma once

#include <string>
#include <vector>
#include <map>

struct BlockColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct BlockDef {
    std::string id;
    int size = 6;
    int layers = 6;
    std::string collision; // full | ramp | none
    std::vector<std::string> layers_text; // flattened: layer0 rows, then layer1 rows...
    std::map<char, BlockColor> palette;
};

bool LoadBlockSml(const std::string& path, BlockDef* out_block, std::string* error);

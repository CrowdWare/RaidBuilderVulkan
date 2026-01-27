/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of RaidBuilder.
 */

#include "block_sml.h"

#include "sml_parser.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <cstdio>

static bool LoadTextFile(const std::string& path, std::string* out_text) {
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    *out_text = ss.str();
    return true;
}

static bool ParseHexColor(const std::string& text, BlockColor* out) {
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
    out->r = (float)r / 255.0f;
    out->g = (float)g / 255.0f;
    out->b = (float)b / 255.0f;
    out->a = (float)a / 255.0f;
    return true;
}

bool LoadBlockSml(const std::string& path, BlockDef* out_block, std::string* error) {
    if (!out_block)
        return false;
    std::string text;
    if (!LoadTextFile(path, &text)) {
        if (error)
            *error = "Failed to read file";
        return false;
    }

    struct Handler : public sml::SmlHandler {
        BlockDef block;
        std::vector<std::string> stack;
        std::string lines;
        std::string color_id;
        std::string color_value;

        void startElement(const std::string& name) override {
            stack.push_back(name);
        }
        void onProperty(const std::string& name, const sml::PropertyValue& value) override {
            if (stack.empty())
                return;
            const std::string& elem = stack.back();
            if (elem == "Block") {
                if (name == "id" && value.type == sml::PropertyValue::String)
                    block.id = value.string_value;
                else if (name == "size" && value.type == sml::PropertyValue::Int)
                    block.size = value.int_value;
                else if (name == "layers" && value.type == sml::PropertyValue::Int)
                    block.layers = value.int_value;
                else if (name == "collision" &&
                         (value.type == sml::PropertyValue::String || value.type == sml::PropertyValue::EnumType))
                    block.collision = value.string_value;
                else if (name == "lines" && value.type == sml::PropertyValue::String)
                    lines = value.string_value;
                else
                    std::fprintf(stderr, "Block SML warning: Unknown Block property '%s'\n", name.c_str());
            } else if (elem == "Color") {
                if (name == "id" && value.type == sml::PropertyValue::String)
                    color_id = value.string_value;
                else if (name == "color" && value.type == sml::PropertyValue::String)
                    color_value = value.string_value;
                else
                    std::fprintf(stderr, "Block SML warning: Unknown Color property '%s'\n", name.c_str());
            }
        }
        void endElement(const std::string& name) override {
            if (name == "Color") {
                if (!color_id.empty() && !color_value.empty()) {
                    BlockColor c;
                    if (ParseHexColor(color_value, &c) && !color_id.empty())
                        block.palette[color_id[0]] = c;
                }
                color_id.clear();
                color_value.clear();
            }
            if (!stack.empty())
                stack.pop_back();
        }
    };

    Handler handler;
    try {
        sml::SmlSaxParser parser(text);
        parser.registerEnumValue("collision", "full");
        parser.registerEnumValue("collision", "ramp");
        parser.registerEnumValue("collision", "none");
        parser.parse(handler);
    } catch (const sml::SmlParseException& e) {
        if (error)
            *error = e.what();
        return false;
    }

    if (handler.block.size <= 0)
        handler.block.size = 6;
    if (handler.block.size != 6) {
        std::fprintf(stderr, "Block SML warning: size=%d is unsupported, forcing 6\n", handler.block.size);
        handler.block.size = 6;
    }
    if (handler.block.layers <= 0)
        handler.block.layers = 6;
    if (handler.block.layers != 6) {
        std::fprintf(stderr, "Block SML warning: layers=%d is unsupported, forcing 6\n", handler.block.layers);
        handler.block.layers = 6;
    }

    // Parse lines into layer rows.
    std::istringstream iss(handler.lines);
    std::string line;
    int current_layer = 0;
    std::vector<std::vector<std::string> > per_layer;
    per_layer.resize(handler.block.layers);

    while (std::getline(iss, line)) {
        if (line.empty())
            continue;
        if (line.size() > 1 && line[0] == '#') {
            current_layer = std::atoi(line.c_str() + 1);
            if (current_layer < 0)
                current_layer = 0;
            if (current_layer >= (int)per_layer.size())
                per_layer.resize(current_layer + 1);
            continue;
        }
        // Trim whitespace
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        if (!line.empty())
            per_layer[current_layer].push_back(line);
    }

    handler.block.layers_text.clear();
    for (int layer = 0; layer < (int)per_layer.size(); ++layer) {
        for (size_t r = 0; r < per_layer[layer].size(); ++r)
            handler.block.layers_text.push_back(per_layer[layer][r]);
    }

    *out_block = handler.block;
    return true;
}

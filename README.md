# RaidBuilder (C++/Vulkan)

C++-Applikation fuer den neuen RaidBuilder. Verbindet VoxelEngine (Vulkan Renderer), SMLUI (ImGui-Layout aus SML) und SMLParser.

## Was hier passiert
- Lädt `UI.sml` und baut daraus das ImGui-Layout (Toolbar, Docking, Viewport).
- Rendert den 3D-Viewport via `VoxelEngine::VoxelRenderer`.
- Dient als Basis fuer den langfristigen Editor.

## Build
Voraussetzungen:
- Vulkan SDK
- GLFW3 (pkg-config)
- `glslc` (Shader Compiler)

Makefile:
```sh
make
```

CMake (nur das Executable):
```sh
cmake -S . -B build
cmake --build build
```

## Run
```sh
./RaidBuilder
```

## Wichtige Dateien
- `src/main.cpp` App-Entry, Input, ImGui, Rendering.
- `UI.sml` UI-Layout in SML.
- `shaders/` GLSL Shader, werden via `glslc` in `.spv` kompiliert.
- `dungeon.sml` Test-Dungeon.

## Status
Aktive Vulkan-Portierung. Rendering, UI und SML-Parsing laufen; Tooling und Features sind im Ausbau.

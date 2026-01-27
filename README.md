# RaidBuilder (C++/Vulkan)

RaidBuilder ist ein Editor für voxel‑basierte Dungeons. Er verbindet:
- **VoxelEngine** (Vulkan Renderer)
- **SMLUI** (ImGui‑Layout aus SML)
- **SMLParser** (SML‑Dateien für UI, Tiles und Blöcke)

Der Fokus liegt auf einem schnellen, direkten Workflow: UI ist deklarativ in SML beschrieben, der Viewport ist Vulkan‑basiert und reagiert direkt auf Input.

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

## Bedienung (Input)

Modi:
- **Edit‑Mode**: UI sichtbar, Blöcke setzen/auswählen/rotieren.
- **Play‑Mode**: UI ausgeblendet, Kamera‑Steuerung aktiv.

Kamera:
- **W/A/S/D**: Bewegen (vor/zurück/links/rechts)
- Maus: Blickrichtung (im Play‑Mode)

Blöcke platzieren (wie Malprogramm):
- **Rechtsklick**: Block platzieren (mit aktivem Tile)
- **G**: Ghost‑Preview ein/aus

Blöcke rotieren:
- **Left/Right**: Yaw um Welt‑Y (90°)
- **Up/Down**: Roll kamera‑relativ (Top kippt von dir weg/zu dir hin)

Hinweis: Symmetrische Tiles werden nicht rotiert.

## Tiles

Tiles definieren, wie Blöcke aussehen und gerendert werden.

Wichtige Dateien:
- `tiles/Core/tiles.sml` – zentrale Tile‑Liste
- `dungeon.sml` – Dungeon‑Layout + optional Tile‑Overrides

Ein Tile kann u. a. definieren:
- `key` (ein Zeichen, z. B. `S`)
- `name`
- `texture`
- `model` (GLB)
- `material` (z. B. `texture` oder `vertex`)

Beispiel (aus `tiles/Core/tiles.sml`):
```sml
Tile {
    key: "S"
    name: "Stone"
    texture: "res://assets/textures/raid_stone.png"
    model: "../build/blocks_cache/block.glb"
    material: texture
}
```

### Eigene Tiles nutzen
Der Workflow ist aktuell „Tile definieren + Modell bereitstellen“.
Das ist in diesem Vulkan‑Projekt **teilweise vorhanden**, aber der komplette End‑User‑Flow (wie im Godot‑Projekt) ist **noch nicht vollständig implementiert**.

Pragmatischer Weg aktuell:
1. **Block‑SML** in `assets/blocks/` anlegen (siehe „Block‑Baking“ unten)
2. **Block backen** → `build/blocks_cache/*.glb`
3. **Tile** in `tiles/Core/tiles.sml` oder `dungeon.sml` eintragen und `model` auf die GLB zeigen lassen

## Block‑Baking ("Kochen" der Blöcke)

Blöcke werden aus SML zu GLB konvertiert.

Pfad:
- Input: `assets/blocks/*.sml`
- Output: `build/blocks_cache/*.glb`

Der Baking‑Step läuft über das Tool **BlockBake** (C++), das beim Build angestoßen wird.
CMake‑Target: **BakeBlocks**

Kurzfassung:
- `assets/blocks/block.sml` → `build/blocks_cache/block.glb`
- `assets/blocks/stairs.sml` → `build/blocks_cache/stairs.glb`
- `assets/blocks/vines.sml` → `build/blocks_cache/vines.glb`

Wenn du neue Block‑SMLs anlegst:
1. Datei in `assets/blocks/` anlegen
2. Eintrag im Bake‑Step (CMake) ergänzen, falls nötig
3. Tile mit `model: "../build/blocks_cache/<name>.glb"` anlegen

## Dungeon‑Format (SML)

Dungeons sind in `dungeon.sml` definiert.
Das Format beschreibt:
- Tiles (optional überschreiben)
- Layout / Grid
- Rotation/Placement (falls vorhanden)

## UI in SML (SMLUI + ImGui)

Die UI wird in **SML** deklariert (`UI.sml`). Daraus generiert **SMLUI** ImGui‑Widgets.
Vorteile:
- UI‑Layout ist deklarativ und schnell änderbar
- Kein „ImGui‑Code‑Wald“ im C++
- Designer/Devs können UI unabhängig vom Rendercode anpassen

Kurz:
- `UI.sml` beschreibt Panels, Docking, Toolbar etc.
- `SMLUI` parst und rendert das Layout über ImGui

## Wichtige Dateien

- `src/main.cpp` – App‑Entry, Input, ImGui, Rendering, Editor‑Logic
- `UI.sml` – UI‑Layout in SML
- `shaders/` – GLSL Shader (via `glslc` → `.spv`)
- `assets/blocks/` – Block‑SMLs (werden gebacken)
- `build/blocks_cache/` – gebackene GLBs
- `tiles/Core/tiles.sml` – Tile‑Definitionen
- `dungeon.sml` – Test‑Dungeon

## Status

Aktive Vulkan‑Portierung. Rendering, UI und SML‑Parsing laufen stabil.
Features und Tooling (z. B. kompletter User‑Tile‑Flow) sind im Ausbau.

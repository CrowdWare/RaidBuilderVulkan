# Block SML Schema

Blocks are authored as `Block { ... }` plus `Color { ... }` entries.
Each block is a 6 x 6 x 6 sub-voxel grid by default, using vertex colors as the primary visual style.

## Block
```
Block {
  id: "stairs_concave"
  size: 6
  layers: 6
  collision: ramp
  lines: "
#0
xxxxxx
xxxxxx
xxxxxx
xxxxxx
xxxxxx
xxxxxx
#1
xxxxxx
xxxxxx
xxxxxx
xxxxxx
xxxxxx
xxxxxx
#2
..xxxx
..xxxx
xxxxxx
xxxxxx
xxxxxx
xxxxxx
#3
..xxxx
..xxxx
xxxxxx
xxxxxx
xxxxxx
xxxxxx
#4
....xx
....xx
....xx
....xx
xxxxxx
xxxxxx
#5
....xx
....xx
....xx
....xx
xxxxxx
xxxxxx
"
}
```

Fields:
- `id`: unique block id.
- `size`: sub-voxel grid width (x/z), default 6.
- `layers`: sub-voxel grid height (y), default 6.
- `collision`: `full | ramp | none` (runtime only, not baked into mesh).
- `lines`: voxel layout. `#<layer>` switches layer, followed by `size` rows of `size` chars.

## Colors
```
Color { id: "x" color: "0xffffffff" }
Color { id: "." color: "0x00000000" }
```
Each character in `lines` maps to a color. Transparent (`alpha = 0`) is treated as empty.

## Notes
- Vertex colors are the default rendering style.
- Mesh baking removes internal faces (only visible faces are kept).

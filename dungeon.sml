Dungeon {
    TileMap {
        lines: "
#0
s . . t . .
. . . . . .
. . . . . .
s s s w . .
s . . . . .
s . . . . .
s . . . . .
. . . . . .
s . . . . .
s . . . . .
#1
s . . . . .
. . . . . .
. . . . . .
s . s . . .
. . . . . .
. . . . . .
s . . . . .
. . . . . .
s . . . . .
s . . . . .
#2
s . . . . .
. . . . . .
. . . . . .
s . s . . .
. . . . . .
. . . . . .
s . . . . .
. . . . . .
s . . . . .
s . . . . .
#3
s . . . . .
. . . . . .
. . . . . .
. . . . . .
. . . . . .
. . . . . .
s . . . . .
. . . . . .
s . . . . .
s . . . . .
#4
s w w w w .
s . . . . .
s . . . . .
s w w w w .
s . . . . .
s . . . . .
s w w w w w
. . . . . .
s w w w w .
s w w w w .
        "
    }

    Tiles {
        Tile { key: "s" texture: "assets/textures/raid_stone.png" model: "block.glb" }
        Tile { key: "w" texture: "assets/textures/raid_wood.png" model: "block.glb" }
        Tile { key: "V" texture: "res://assets/textures/raid_window.png" model: "../build/blocks_cache/block.glb" }
        Tile { key: "d" texture: "res://assets/textures/raid_door.png" model: "../build/blocks_cache/block.glb" }
        Tile { key: "l" texture: "res://assets/textures/raid_lamp.png" model: "../build/blocks_cache/block.glb" }
        Tile { key: "S" texture: "res://assets/textures/raid_spawn.png" model: "../build/blocks_cache/block.glb" }
        Tile { key: "E" texture: "res://assets/textures/raid_enemy.png" model: "../build/blocks_cache/block.glb" }
        Tile { key: "t" texture: "assets/textures/raid_stone.png" model: "stairs.glb" }
        Tile { key: "c" texture: "assets/textures/raid_stair.png" model: "../build/blocks_cache/stairs_concave.glb" }
        Tile { key: "C" texture: "assets/textures/raid_stair.png" model: "../build/blocks_cache/stairs_convex.glb" }
        Tile { key: "g" model: "../build/blocks_cache/vines.glb" }
    }

}

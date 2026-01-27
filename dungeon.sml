Dungeon {
    TileMap {
        lines: "
#0
s . . .
. . . .
. . . .
s s s s
s . . .
s . . .
s . . .
#1
s . .
. . .
. . .
s . s
. . .
. . .
s . .
#2
s . .
. . .
. . .
s . s
. . .
. . .
s . .
#3
s . .
. . .
. . .
s . .
s . .
s . .
s s s
#4
s
        "
    }

    Tiles {
        Tile { key: "s" texture: "assets/textures/raid_stone.png" model: "block.glb" }
        Tile { key: "w" texture: "assets/textures/raid_wood.png" model: "block.glb" }
    }

}

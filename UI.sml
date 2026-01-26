Window {
    title: "RaidBuilder"
    position: 20,20
    size: 1280,720

    state {
        persist: user      // user | project | session
        pos: true
        size: true
        maximized: true
        lastFilePath: true
        docking: true
    }

    ToolBar { 
        height: 48
        ToolButton { icon: play }
    }
    
    StatusBar { 
        height: 24
    }
    
    DockLayout {
        Left {
            label: "Toolbar"
            width: 56
            Column {
                ToolButton { icon: select }
                ToolButton { icon: move }
                ToolButton { icon: paint }
            }
        }

        Right {
            label: "Properties"
            width: 360
            PropertyPanel { }
        }

        Center {
            label: "Viewport"

            Box {
                Viewport3D { }
                Overlay {
                    // gizmos, hints, selection rect
                }
            }
        }
    }
}
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

    DockLayout {

        Top {
            height: 48
            Column {
                //MenuBar { }
                ToolBar { 
                    ToolButton { icon: play }
                }
            }
        }

        Bottom {
            height: 24
            StatusBar { }
        }

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
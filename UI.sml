Window {
    title: "RaidBuilder"
    position: 20,20
    size: 1280,720

    state {
        persist: user
        pos: true
        size: true
        maximized: true
        lastFilePath: true
        docking: true
        theme: "dark"
    }

    MainMenu {
        Menu {
            label: "File"

            MenuItem {
                label: "Open"
                
            }
            Separator {}
            MenuItem {
                label: "Save"
                
            }
            MenuItem {
                label: "Save As"
                
            }
            Separator {}
            MenuItem {
                label: "Exit"
                clicked: "exit"
                
                useOnMac: false
            }
        }
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
            width: 400
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
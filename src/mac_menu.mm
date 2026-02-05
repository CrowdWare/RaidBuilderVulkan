/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of RaidBuilder.
 */

#include "mac_menu.h"

#if defined(__APPLE__)
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>

#include "sml_ui.h"

static NSString* ToNSString(const std::string& value) {
    return value.empty() ? @"" : [NSString stringWithUTF8String:value.c_str()];
}

static int MenuActionIdFor(const std::string& action) {
    if (action == "open") return 1001;
    if (action == "save") return 1002;
    if (action == "saveAs") return 1003;
    if (action == "closeQuery") return 1099;
    return 0;
}

@interface MenuActionDispatcher : NSObject
@property (assign, nonatomic) MenuActionCallback callback;
@property (assign, nonatomic) void* userData;
@end

@implementation MenuActionDispatcher
- (void)performAction:(id)sender {
    if (!self.callback)
        return;
    if (![sender isKindOfClass:[NSMenuItem class]])
        return;
    NSMenuItem* item = (NSMenuItem*)sender;
    if (![item representedObject])
        return;
    NSInteger actionId = [[item representedObject] integerValue];
    self.callback((int)actionId, self.userData);
}
@end

static MenuActionDispatcher* GetDispatcher(MenuActionCallback callback, void* user_data) {
    static MenuActionDispatcher* dispatcher = nil;
    if (!dispatcher) {
        dispatcher = [[MenuActionDispatcher alloc] init];
    }
    dispatcher.callback = callback;
    dispatcher.userData = user_data;
    return dispatcher;
}

static void EnsureAppMenu(NSMenu* main_menu, const std::string& app_name) {
    if ([main_menu numberOfItems] > 0)
        return;
    NSString* title = app_name.empty() ? @"RaidBuilder" : ToNSString(app_name);
    NSMenuItem* app_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    NSMenu* app_menu = [[NSMenu alloc] initWithTitle:title];
    [app_menu addItem:[[NSMenuItem alloc] initWithTitle:@"About"
                                                 action:nil
                                          keyEquivalent:@""]];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit"
                                                 action:@selector(terminate:)
                                          keyEquivalent:@"q"]];
    [app_item setSubmenu:app_menu];
    [main_menu addItem:app_item];
}

static NSInteger FindMenuIndex(NSMenu* main_menu, NSString* title) {
    NSInteger count = [main_menu numberOfItems];
    for (NSInteger i = 0; i < count; ++i) {
        NSMenuItem* item = [main_menu itemAtIndex:i];
        if ([[item title] isEqualToString:title])
            return i;
    }
    return NSNotFound;
}

static NSInteger FindWindowMenuIndex(NSMenu* main_menu) {
    NSMenu* window_menu = [NSApp windowsMenu];
    if (window_menu != nil) {
        NSInteger count = [main_menu numberOfItems];
        for (NSInteger i = 0; i < count; ++i) {
            NSMenuItem* item = [main_menu itemAtIndex:i];
            if ([item submenu] == window_menu)
                return i;
        }
    }
    return FindMenuIndex(main_menu, @"Window");
}

static void EnsureWindowMenu(NSMenu* main_menu) {
    NSInteger idx = FindWindowMenuIndex(main_menu);
    if (idx != NSNotFound)
        return;
    NSMenuItem* window_item = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    NSMenu* window_menu = [[NSMenu alloc] initWithTitle:@"Window"];
    [window_menu addItem:[[NSMenuItem alloc] initWithTitle:@"Minimize"
                                                    action:@selector(performMiniaturize:)
                                             keyEquivalent:@"m"]];
    [window_menu addItem:[[NSMenuItem alloc] initWithTitle:@"Zoom"
                                                    action:@selector(performZoom:)
                                             keyEquivalent:@""]];
    [window_item setSubmenu:window_menu];
    [main_menu addItem:window_item];
    [NSApp setWindowsMenu:window_menu];
}

static void ClearMenuEntries(NSMenu* main_menu, NSInteger start_index, NSInteger end_index) {
    if (!main_menu)
        return;
    if (end_index < start_index)
        return;
    for (NSInteger idx = end_index; idx >= start_index; --idx) {
        [main_menu removeItemAtIndex:idx];
    }
}

void BuildMacMainMenu(GLFWwindow* window, const smlui::UiWindow& ui_window,
                      MenuActionCallback callback, void* user_data) {
    (void)window;
    @autoreleasepool {
        if (NSApp == nil)
            return;

        NSMenu* main_menu = [NSApp mainMenu];
        if (main_menu == nil) {
            main_menu = [[NSMenu alloc] initWithTitle:@""];
            [NSApp setMainMenu:main_menu];
        }

        EnsureAppMenu(main_menu, ui_window.title);
        EnsureWindowMenu(main_menu);

        if (!ui_window.main_menu.enabled || ui_window.main_menu.menus.empty())
            return;

        NSInteger window_index = FindWindowMenuIndex(main_menu);
        NSInteger insert_index = window_index == NSNotFound ? [main_menu numberOfItems] : window_index;
        NSInteger end_index = (window_index == NSNotFound ? [main_menu numberOfItems] - 1 : window_index - 1);
        ClearMenuEntries(main_menu, insert_index, end_index);

        MenuActionDispatcher* dispatcher = GetDispatcher(callback, user_data);

        for (size_t i = 0; i < ui_window.main_menu.menus.size(); ++i) {
            const smlui::UiMenu& menu = ui_window.main_menu.menus[i];
            NSString* menu_title = menu.label.empty() ? @"Menu" : ToNSString(menu.label);
            NSMenuItem* menu_item = [[NSMenuItem alloc] initWithTitle:menu_title action:nil keyEquivalent:@""];
            NSMenu* submenu = [[NSMenu alloc] initWithTitle:menu_title];

            for (size_t j = 0; j < menu.items.size(); ++j) {
                const smlui::UiMenuItem& item = menu.items[j];
                if (item.is_separator) {
                    [submenu addItem:[NSMenuItem separatorItem]];
                    continue;
                }
                if (item.clicked == "exit" || item.label == "Exit")
                    continue;

                NSString* item_title = item.label.empty() ? @"Item" : ToNSString(item.label);
                NSMenuItem* sub_item = [[NSMenuItem alloc] initWithTitle:item_title action:@selector(performAction:) keyEquivalent:@""];
                int action_id = item.action_id != 0 ? item.action_id : MenuActionIdFor(item.action);
                if (action_id != 0) {
                    sub_item.representedObject = [NSNumber numberWithInteger:action_id];
                    sub_item.enabled = YES;
                } else {
                    sub_item.enabled = NO;
                }
                sub_item.target = dispatcher;
                [submenu addItem:sub_item];
            }

            [menu_item setSubmenu:submenu];
            [main_menu insertItem:menu_item atIndex:insert_index++];
        }
    }
}

#else
void BuildMacMainMenu(GLFWwindow*, const smlui::UiWindow&, MenuActionCallback, void*) {}
#endif

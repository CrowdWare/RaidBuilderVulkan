#pragma once

struct GLFWwindow;

namespace smlui {
struct UiWindow;
}

using MenuActionCallback = void(*)(int action_id, void* user_data);
void BuildMacMainMenu(GLFWwindow* window, const smlui::UiWindow& ui_window,
                      MenuActionCallback callback, void* user_data);

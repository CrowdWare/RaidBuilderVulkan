#pragma once

struct GLFWwindow;

namespace smlui {
struct UiWindow;
}

void BuildMacMainMenu(GLFWwindow* window, const smlui::UiWindow& ui_window);

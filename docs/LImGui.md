# LImGui

`LImGui.hpp` 是 Dear ImGui 的轻量 facade。它不会携带 Dear ImGui 本体；项目需要先让 `<imgui.h>` 和需要的 `imgui_impl_*` backend 进入 include/link 路径。检测通过后，链路是：

```text
LImGui.hpp -> detail/LImGui.hpp -> LHelloImGui.hpp / LImGuiBundle.hpp -> Dear ImGui backend
```

## 常用入口

- `LImGui::Text(...)`：Dear ImGui 原生 printf 风格文本接口。
- `LImGui::TextFmt("name: {}", value)`：使用内置 fmt 语法格式化文本，并通过 `TextUnformatted()` 渲染。
- `LImGui::ImColor("#ffffffff")`：LTool 颜色类型，支持 `#rrggbbaa` / `#rrggbb`、RGBA 数值，并可转为 `ImVec4`、`ImU32`、Dear ImGui `ImColor`。
- `LImGui::configure(params, configure)`：按偏好选择 backend，并可挂接系统字体加载回调。
- `LImGui::run(...)`：转发到 HelloImGui 启动窗口。
- `LImGui::theme`、`LImGui::fonts`、`LImGui::bundle`、`LImGui::plot`、`LImGui::spinner`、`LImGui::drawing`、`LImGui::tables`、`LImGui::widgets`：常用主题、字体和 imgui-bundle 命名空间别名。
- `LTOOL_IMGUI_AUTO_ID(label)`：把显示标签和当前文件行号组合成稳定隐藏 ID，适合多个同名按钮。

## 示例

```cpp
#include "LImGui.hpp"

void show_gui() {
    LImGui::TextFmt("FPS: {:.1f}", LImGui::FrameRate());

    if (LImGui::Button(LTOOL_IMGUI_AUTO_ID("Apply"))) {
        LImGui::theme::ApplyOneDark();
    }
}

int main() {
    LImGui::RunnerParams params;
    params.appWindowParams.windowTitle = "LTool ImGui";
    params.imGuiWindowParams.backgroundColor = LImGui::ImColor("#121417ff");
    params.callbacks.ShowGui = show_gui;

    LImGui::ConfigureParams configure;
    configure.backendPreference = LImGui::BackendPreference::PreferVulkan;
    configure.fontParams.fontSize = 18.0f;
    configure.fontParams.chineseFontSize = 18.0f;

    LImGui::configure(params, configure);
    LImGui::run(params);
}
```

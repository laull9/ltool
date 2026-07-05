# LImGui

`LImGui.hpp` 是 Dear ImGui 的轻量 facade。它不会携带 Dear ImGui 本体；项目需要先让 `<imgui.h>` 和需要的 `imgui_impl_*` backend 进入 include/link 路径。检测通过后，链路是：

```text
LImGui.hpp -> detail/LImGui.hpp -> LHelloImGui.hpp / LImGuiBundle.hpp -> Dear ImGui backend
```

## 常用入口

- `LImGui::Text("name: {}", value)`：使用内置 fmt 语法格式化文本，并通过 `ImGui::TextUnformatted()` 渲染。
- `LImGui::configure(params, configure)`：按偏好选择 backend，并可挂接系统字体加载回调。
- `LImGui::run(...)`：转发到 HelloImGui 启动窗口。
- `LImGui::theme`、`LImGui::fonts`、`LImGui::drawing`、`LImGui::tables`、`LImGui::widgets`：常用主题、字体、绘制、表格和小组件命名空间别名。
- `LTOOL_IMGUI_AUTO_ID(label)`：把显示标签和当前文件行号组合成稳定隐藏 ID，适合多个同名按钮。

## 示例

```cpp
#include "LImGui.hpp"

void show_gui() {
    LImGui::Text("FPS: {:.1f}", HelloImGui::FrameRate());

    if (ImGui::Button(LTOOL_IMGUI_AUTO_ID("Apply"))) {
        LImGui::theme::ApplyOneDark();
    }
}

int main() {
    LImGui::RunnerParams params;
    params.appWindowParams.windowTitle = "LTool ImGui";
    params.callbacks.ShowGui = show_gui;

    LImGui::ConfigureParams configure;
    configure.backendPreference = LImGui::BackendPreference::PreferVulkan;
    configure.fontParams.fontSize = 18.0f;
    configure.fontParams.chineseFontSize = 18.0f;

    LImGui::configure(params, configure);
    LImGui::run(params);
}
```

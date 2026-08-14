/**
 * @file LImGui.hpp
 * @brief LTool 的 ImGui 快速启动与便捷工具入口。
 *
 * LImGui 不携带 Dear ImGui 本体和 backend。外部项目提供 imgui.h
 * 和可选 imgui_impl_* backend 后，本头会启用 bundled HelloImGui /
 * imgui-bundle 子集。
 *
 * 主要接口：
 * - RunnerParams / run()         ：一键启动带窗口的 ImGui 应用。
 * - ConfigureParams / configure()：设置 backend、字体（含中文字体）等。
 * - ImColor / ImVec2 / ImDrawList 等 ImGui 常用类型别名。
 *
 * 常用示例：
 * @code
 * LImGui::RunnerParams params;
 * params.appWindowParams.windowTitle = "demo";
 * params.callbacks.ShowGui = [] { ImGui::Text("hello"); };
 * LImGui::run(params);
 * @endcode
 */

#ifndef LTOOL_LIMGUI_INCLUDE
/// LImGui.hpp 的头文件包含保护宏。
#define LTOOL_LIMGUI_INCLUDE

#include "detail/LImGui.hpp"

#endif // LTOOL_LIMGUI_INCLUDE

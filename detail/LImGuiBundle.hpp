/**
 * @file detail/LImGuiBundle.hpp
 * @brief LTool Dear ImGui Bundle-style helper facade.
 *
 * Source reference: https://github.com/pthom/imgui_bundle
 * Includes the upstream header-only imspinner submodule and small LTool table/drawing helpers.
 */

#ifndef LTOOL_DETAIL_LIMGUI_BUNDLE_INCLUDE
#define LTOOL_DETAIL_LIMGUI_BUNDLE_INCLUDE

#include "LToolConfig.hpp"

#if LTOOL_HAS_IMGUI

#include <imgui.h>

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#if LTOOL_HAS_IMGUI_INTERNAL
#include "../pkgs/imgui_bundle/imspinner/imspinner.h"
#define LTOOL_IMGUI_BUNDLE_HAS_IMSPINNER 1
#else
#define LTOOL_IMGUI_BUNDLE_HAS_IMSPINNER 0
#endif

#if LTOOL_HAS_IMPLOT
#if LTOOL_HAS_INCLUDE("../pkgs/imgui_bundle/implot/implot.h")
#include "../pkgs/imgui_bundle/implot/implot.h"
#elif LTOOL_HAS_INCLUDE(<implot/implot.h>)
#include <implot/implot.h>
#else
#include <implot.h>
#endif
#define LTOOL_IMGUI_BUNDLE_HAS_IMPLOT 1
#else
#define LTOOL_IMGUI_BUNDLE_HAS_IMPLOT 0
#endif

#if LTOOL_HAS_IMPLOT3D
#if LTOOL_HAS_INCLUDE(<implot3d/implot3d.h>)
#include <implot3d/implot3d.h>
#else
#include <implot3d.h>
#endif
#define LTOOL_IMGUI_BUNDLE_HAS_IMPLOT3D 1
#else
#define LTOOL_IMGUI_BUNDLE_HAS_IMPLOT3D 0
#endif

namespace ImGuiBundle {

struct Capabilities {
    static constexpr bool spinner = LTOOL_IMGUI_BUNDLE_HAS_IMSPINNER != 0;
    static constexpr bool implot = LTOOL_IMGUI_BUNDLE_HAS_IMPLOT != 0;
    static constexpr bool implot3d = LTOOL_IMGUI_BUNDLE_HAS_IMPLOT3D != 0;
    static constexpr bool drawing = true;
    static constexpr bool tables = true;
};

namespace Plot {

inline bool available() {
    return LTOOL_IMGUI_BUNDLE_HAS_IMPLOT != 0;
}

inline bool begin(const char* title,
                  const ImVec2& size = ImVec2(-1.0f, 0.0f)
#if LTOOL_IMGUI_BUNDLE_HAS_IMPLOT
                  ,
                  ImPlotFlags flags = 0
#endif
) {
#if LTOOL_IMGUI_BUNDLE_HAS_IMPLOT
    return ImPlot::BeginPlot(title, size, flags);
#else
    (void)title;
    (void)size;
    return false;
#endif
}

inline void end() {
#if LTOOL_IMGUI_BUNDLE_HAS_IMPLOT
    ImPlot::EndPlot();
#endif
}

template<class T>
inline void line(const char* label, const T* xs, const T* ys, int count) {
#if LTOOL_IMGUI_BUNDLE_HAS_IMPLOT
    ImPlot::PlotLine(label, xs, ys, count);
#else
    (void)label;
    (void)xs;
    (void)ys;
    (void)count;
#endif
}

template<class T>
inline void line(const char* label, const T* values, int count) {
#if LTOOL_IMGUI_BUNDLE_HAS_IMPLOT
    ImPlot::PlotLine(label, values, count);
#else
    (void)label;
    (void)values;
    (void)count;
#endif
}

} // namespace Plot

namespace Plot3D {

inline bool available() {
    return LTOOL_IMGUI_BUNDLE_HAS_IMPLOT3D != 0;
}

} // namespace Plot3D

namespace Drawing {

inline ImU32 color_u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

inline void underline_last_item(const ImVec4& color = ImVec4(0.40f, 0.65f, 1.00f, 1.00f),
                                float thickness = 1.0f) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    draw_list->AddLine(ImVec2(min.x, max.y), max, color_u32(color), thickness);
}

inline void rect_around_last_item(const ImVec4& color = ImVec4(0.40f, 0.65f, 1.00f, 1.00f),
                                  float rounding = 4.0f,
                                  float thickness = 1.0f,
                                  float padding = 2.0f) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    min.x -= padding;
    min.y -= padding;
    max.x += padding;
    max.y += padding;
    draw_list->AddRect(min, max, color_u32(color), rounding, 0, thickness);
}

inline void progress_bar_overlay(float fraction,
                                 const ImVec2& size,
                                 const char* overlay = nullptr,
                                 const ImVec4& fill = ImVec4(0.10f, 0.45f, 0.90f, 1.00f)) {
    ImGui::ProgressBar(fraction, size, overlay);
    rect_around_last_item(fill, 3.0f, 1.0f, 0.0f);
}

inline void centered_text(const char* text, float width = 0.0f) {
    const float available = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    if (available > text_size.x) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - text_size.x) * 0.5f);
    }
    ImGui::TextUnformatted(text);
}

inline bool icon_button(const char* label, const char* tooltip = nullptr, const ImVec2& size = ImVec2()) {
    const bool clicked = ImGui::Button(label, size);
    if (tooltip && tooltip[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

} // namespace Drawing

namespace Tables {

class TableScope {
public:
    TableScope(const char* id,
               int column_count,
               ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
               const ImVec2& outer_size = ImVec2(),
               float inner_width = 0.0f) {
        active_ = ImGui::BeginTable(id, column_count, flags, outer_size, inner_width);
    }

    ~TableScope() {
        if (active_) {
            ImGui::EndTable();
        }
    }

    explicit operator bool() const {
        return active_;
    }

private:
    bool active_ = false;
};

inline void setup_columns(std::initializer_list<const char*> names,
                          ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None) {
    for (const char* name : names) {
        ImGui::TableSetupColumn(name, flags);
    }
    ImGui::TableHeadersRow();
}

inline void text_cell(const char* text) {
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(text ? text : "");
}

inline void text_cell(const std::string& text) {
    text_cell(text.c_str());
}

inline void row(std::initializer_list<const char*> cells) {
    ImGui::TableNextRow();
    for (const char* cell : cells) {
        text_cell(cell);
    }
}

template<class RowRenderer>
inline bool simple_table(const char* id,
                         std::initializer_list<const char*> columns,
                         std::size_t row_count,
                         RowRenderer render_row,
                         ImGuiTableFlags flags = ImGuiTableFlags_Borders |
                                                 ImGuiTableFlags_RowBg |
                                                 ImGuiTableFlags_Resizable |
                                                 ImGuiTableFlags_ScrollY,
                         const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    TableScope table(id, static_cast<int>(columns.size()), flags, size);
    if (!table) {
        return false;
    }
    setup_columns(columns);
    for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
        ImGui::TableNextRow();
        render_row(row_index);
    }
    return true;
}

} // namespace Tables

namespace Widgets {

inline bool toggle(const char* label, bool* value) {
    return ImGui::Checkbox(label, value);
}

inline bool small_toggle(const char* label, bool* value) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    const bool changed = ImGui::Checkbox(label, value);
    ImGui::PopStyleVar();
    return changed;
}

inline bool knob_float(const char* label, float* value, float min_value, float max_value) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const bool changed = ImGui::VSliderFloat("##knob", ImVec2(36.0f, 96.0f), value, min_value, max_value);
    ImGui::PopID();
    return changed;
}

inline void spinner(const char* label,
                    float radius = 12.0f,
                    float thickness = 2.0f,
                    const ImVec4& color = ImVec4(0.40f, 0.65f, 1.00f, 1.00f)) {
#if LTOOL_IMGUI_BUNDLE_HAS_IMSPINNER
    ImSpinner::SpinnerAng(label, radius, thickness, ImColor(color));
#else
    (void)radius;
    (void)thickness;
    ImGui::TextUnformatted(label);
#endif
}

} // namespace Widgets

} // namespace ImGuiBundle

#endif // LTOOL_HAS_IMGUI

#endif // LTOOL_DETAIL_LIMGUI_BUNDLE_INCLUDE

/**
 * @file detail/LImGui.hpp
 * @brief LTool 自己的 ImGui facade。
 */

#ifndef LTOOL_DETAIL_LIMGUI_INCLUDE
/// detail/LImGui.hpp 的头文件包含保护宏。
#define LTOOL_DETAIL_LIMGUI_INCLUDE

#include "LToolConfig.hpp"

#if LTOOL_HAS_IMGUI

#include "LFmt.hpp"
#include "LHelloImGui.hpp"
#include "LImGuiBundle.hpp"

#if LTOOL_HAS_INCLUDE(<misc/cpp/imgui_stdlib.h>)
#include <misc/cpp/imgui_stdlib.h>
#elif LTOOL_HAS_INCLUDE(<imgui_stdlib.h>)
#include <imgui_stdlib.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace LImGui {

using namespace ::ImGui;

using ::ImDrawList;
using ::ImFont;
using ::ImGuiCond;
using ::ImGuiCond_FirstUseEver;
using ::ImGuiID;
using ::ImGuiInputTextFlags;
using ::ImGuiInputTextFlags_AllowTabInput;
using ::ImGuiIO;
using ::ImGuiStyle;
using ::ImGuiTableColumnFlags;
using ::ImGuiTableFlags;
using ::ImTextureID;
using ::ImU32;
using ::ImVec2;
using ::ImVec4;

/// 描述当前编译环境里可用的 ImGui 与 backend 能力。
struct Capabilities {
    /// 是否检测到 Dear ImGui 头文件。
    static constexpr bool imgui = LTOOL_HAS_IMGUI != 0;
    /// 是否可使用 GLFW + OpenGL3 backend。
    static constexpr bool backend_glfw_opengl3 = LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3 != 0;
    /// 是否可使用 GLFW + Vulkan backend。
    static constexpr bool backend_glfw_vulkan = LTOOL_HAS_IMGUI_BACKEND_GLFW_VULKAN != 0;
    /// 是否可使用 SDL2 + OpenGL3 backend。
    static constexpr bool backend_sdl2_opengl3 = LTOOL_HAS_IMGUI_BACKEND_SDL2_OPENGL3 != 0;
    /// 是否可使用 SFML backend。
    static constexpr bool backend_sfml = LTOOL_HAS_IMGUI_BACKEND_SFML != 0;
    /// 是否可使用 Vulkan renderer。
    static constexpr bool renderer_vulkan = LTOOL_HAS_IMGUI_RENDERER_VULKAN != 0;
    /// 是否启用内置 HelloImGui 子集。
    static constexpr bool hello_imgui = LTOOL_HAS_BUNDLED_HELLO_IMGUI != 0;
    /// 是否启用内置 imgui-bundle 子集。
    static constexpr bool imgui_bundle = LTOOL_HAS_BUNDLED_IMGUI_BUNDLE != 0;
    /// 是否可使用 imspinner 小组件。
    static constexpr bool imspinner = ImGuiBundle::Capabilities::spinner;
    /// 是否可使用 ImPlot 绘图能力。
    static constexpr bool implot = ImGuiBundle::Capabilities::implot;
};

/// HelloImGui 屏幕尺寸类型别名。
using ScreenSize = HelloImGui::ScreenSize;
/// HelloImGui 应用窗口参数类型别名。
using AppWindowParams = HelloImGui::AppWindowParams;
/// HelloImGui 完整运行参数类型别名。
using RunnerParams = HelloImGui::RunnerParams;
/// HelloImGui 简化运行参数类型别名。
using SimpleRunnerParams = HelloImGui::SimpleRunnerParams;
/// HelloImGui 无参回调类型别名。
using VoidFunction = HelloImGui::VoidFunction;
/// HelloImGui 外部 backend 回调类型别名。
using ExternalBackendCallbacks = HelloImGui::ExternalBackendCallbacks;
/// HelloImGui 平台 backend 类型别名。
using PlatformBackendType = HelloImGui::PlatformBackendType;
/// HelloImGui 渲染 backend 类型别名。
using RendererBackendType = HelloImGui::RendererBackendType;
/// HelloImGui 默认窗口尺寸。
inline const ScreenSize& DefaultWindowSize = HelloImGui::DefaultWindowSize;

/// HelloImGui 主题辅助命名空间别名。
namespace theme = HelloImGui::Theme;
/// HelloImGui 字体辅助命名空间别名。
namespace fonts = HelloImGui::Fonts;
/// imgui-bundle 顶层命名空间别名。
namespace bundle = ImGuiBundle;
/// imgui-bundle 绘图命名空间别名。
namespace plot = ImGuiBundle::Plot;
/// imgui-bundle 3D 绘图命名空间别名。
namespace plot3d = ImGuiBundle::Plot3D;
#if LTOOL_IMGUI_BUNDLE_HAS_IMSPINNER
/// imspinner 小组件命名空间别名。
namespace spinner = ImSpinner;
#endif
/// imgui-bundle 绘制辅助命名空间别名。
namespace drawing = ImGuiBundle::Drawing;
/// imgui-bundle 表格辅助命名空间别名。
namespace tables = ImGuiBundle::Tables;
/// imgui-bundle 小组件辅助命名空间别名。
namespace widgets = ImGuiBundle::Widgets;

/// LImGui 颜色类型，支持 RGBA 数值、#rrggbbaa 文本，并可转为 ImGui 颜色类型。
struct ImColor {
    ImVec4 value = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    constexpr ImColor() = default;

    constexpr ImColor(float r, float g, float b, float a = 1.0f)
        : value(r, g, b, a) {}

    constexpr ImColor(int r, int g, int b, int a = 255)
        : value(channel_to_float(r),
                channel_to_float(g),
                channel_to_float(b),
                channel_to_float(a)) {}

    explicit ImColor(ImU32 color)
        : value(ImGui::ColorConvertU32ToFloat4(color)) {}

    explicit ImColor(const ImVec4& color)
        : value(color) {}

    explicit ImColor(std::string_view hex)
        : value(parse_hex_or(hex, ImVec4(0.0f, 0.0f, 0.0f, 1.0f))) {}

    static constexpr float channel_to_float(int value) {
        return static_cast<float>(value < 0 ? 0 : (value > 255 ? 255 : value)) / 255.0f;
    }

    static ImColor from_rgb(int r, int g, int b) {
        return ImColor(r, g, b, 255);
    }

    static ImColor from_rgba(int r, int g, int b, int a) {
        return ImColor(r, g, b, a);
    }

    static ImColor from_hex(std::string_view hex) {
        return ImColor(hex);
    }

    static ImColor from_u32(ImU32 color) {
        return ImColor(color);
    }

    static bool parse_hex(std::string_view text, ImColor& color) {
        ImVec4 parsed;
        if (!parse_hex(text, parsed)) {
            return false;
        }
        color = ImColor(parsed);
        return true;
    }

    ImVec4 to_vec4() const {
        return value;
    }

    ImU32 to_u32() const {
        return ImGui::ColorConvertFloat4ToU32(value);
    }

    ::ImColor to_imgui_color() const {
        return ::ImColor(value);
    }

    operator ImVec4() const {
        return to_vec4();
    }

    operator ImU32() const {
        return to_u32();
    }

    operator ::ImColor() const {
        return to_imgui_color();
    }

private:
    static constexpr int hex_value(char c) {
        return c >= '0' && c <= '9' ? c - '0'
             : c >= 'a' && c <= 'f' ? c - 'a' + 10
             : c >= 'A' && c <= 'F' ? c - 'A' + 10
             : -1;
    }

    static constexpr int hex_byte(char high, char low) {
        const int h = hex_value(high);
        const int l = hex_value(low);
        return h < 0 || l < 0 ? -1 : h * 16 + l;
    }

    static bool parse_hex(std::string_view text, ImVec4& color) {
        if (!text.empty() && text.front() == '#') {
            text.remove_prefix(1);
        }
        if (text.size() != 6 && text.size() != 8) {
            return false;
        }

        const int r = hex_byte(text[0], text[1]);
        const int g = hex_byte(text[2], text[3]);
        const int b = hex_byte(text[4], text[5]);
        const int a = text.size() == 8 ? hex_byte(text[6], text[7]) : 255;
        if (r < 0 || g < 0 || b < 0 || a < 0) {
            return false;
        }

        color = ImVec4(channel_to_float(r),
                       channel_to_float(g),
                       channel_to_float(b),
                       channel_to_float(a));
        return true;
    }

    static ImVec4 parse_hex_or(std::string_view text, const ImVec4& fallback) {
        ImVec4 color;
        return parse_hex(text, color) ? color : fallback;
    }
};

inline ImU32 GetColorU32(const ImColor& color) {
    return color.to_u32();
}

/// LImGui 内部实现细节命名空间。
namespace detail {

/// 返回当前线程复用的 fmt 文本缓冲区。
inline fmt::memory_buffer& text_buffer() {
    thread_local fmt::memory_buffer buffer;
    buffer.clear();
    return buffer;
}

/// 将已格式化文本直接交给 ImGui 渲染。
inline void text_unformatted(fmt::string_view text) {
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

} // namespace detail

/// 显示一段无需格式化的文本。
inline void TextUnformatted(fmt::string_view text) {
    detail::text_unformatted(text);
}

/// 使用 fmt 语法格式化并显示文本。
template<typename... Args>
inline void TextFmt(fmt::format_string<Args...> fmt_text, Args&&... args) {
    if constexpr (sizeof...(Args) == 0) {
        detail::text_unformatted(fmt_text.get());
    } else {
        auto& buffer = detail::text_buffer();
        fmt::format_to(std::back_inserter(buffer), fmt_text, std::forward<Args>(args)...);
        ImGui::TextUnformatted(buffer.data(), buffer.data() + buffer.size());
    }
}

/// 后端选择策略。
enum class BackendPreference {
    /// 自动选择第一个可用 backend。
    Auto,
    /// 优先选择 Vulkan，失败后回退到其它 backend。
    PreferVulkan,
    /// 优先选择 OpenGL3，失败后回退到其它 backend。
    PreferOpenGL3,
    /// 强制选择 GLFW + Vulkan。
    GlfwVulkan,
    /// 强制选择 GLFW + OpenGL3。
    GlfwOpenGL3,
    /// 强制选择 SDL2 + OpenGL3。
    Sdl2OpenGL3,
    /// 强制选择 SFML。
    Sfml
};

/// LImGui 初始化配置。
struct ConfigureParams {
    /// backend 选择偏好。
    BackendPreference backendPreference = BackendPreference::Auto;
    /// 是否在启动时加载系统字体。
    bool loadSystemFonts = true;
    /// 系统字体加载参数。
    HelloImGui::Fonts::SystemFontLoadingParams fontParams;
    /// 可选字体加载结果输出指针。
    HelloImGui::Fonts::SystemFontLoadingResult* fontResult = nullptr;
};

/// 根据偏好为 RunnerParams 选择可用 backend。
inline bool configure_backend(RunnerParams& params,
                              BackendPreference preference = BackendPreference::Auto) {
    auto use_glfw_vulkan = [&] {
#if LTOOL_HAS_IMGUI_BACKEND_GLFW_VULKAN
        if (params.externalBackendCallbacks.Setup || params.externalBackendCallbacks.NewFrame ||
            params.externalBackendCallbacks.RenderDrawData) {
            params.platformBackendType = HelloImGui::PlatformBackendType::Glfw;
            params.renderingBackendType = HelloImGui::RendererBackendType::Vulkan;
            return true;
        }
        return false;
#else
        return false;
#endif
    };
    auto use_glfw_opengl3 = [&] {
#if LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3
        params.platformBackendType = HelloImGui::PlatformBackendType::Glfw;
        params.renderingBackendType = HelloImGui::RendererBackendType::OpenGL3;
        return true;
#else
        return false;
#endif
    };
    auto use_sdl2_opengl3 = [&] {
#if LTOOL_HAS_IMGUI_BACKEND_SDL2_OPENGL3
        params.platformBackendType = HelloImGui::PlatformBackendType::Sdl;
        params.renderingBackendType = HelloImGui::RendererBackendType::OpenGL3;
        return true;
#else
        return false;
#endif
    };
    auto use_sfml = [&] {
#if LTOOL_HAS_IMGUI_BACKEND_SFML
        params.platformBackendType = HelloImGui::PlatformBackendType::Sfml;
        params.renderingBackendType = HelloImGui::RendererBackendType::Sfml;
        return true;
#else
        return false;
#endif
    };

    switch (preference) {
    case BackendPreference::GlfwVulkan:
        return use_glfw_vulkan();
    case BackendPreference::GlfwOpenGL3:
        return use_glfw_opengl3();
    case BackendPreference::Sdl2OpenGL3:
        return use_sdl2_opengl3();
    case BackendPreference::Sfml:
        return use_sfml();
    case BackendPreference::PreferVulkan:
        return use_glfw_vulkan() || use_glfw_opengl3() || use_sdl2_opengl3() || use_sfml();
    case BackendPreference::PreferOpenGL3:
        return use_glfw_opengl3() || use_sdl2_opengl3() || use_sfml() || use_glfw_vulkan();
    case BackendPreference::Auto:
        return use_glfw_vulkan() || use_glfw_opengl3() || use_sdl2_opengl3() || use_sfml();
    }
    return false;
}

/// 配置启动阶段的系统字体加载回调。
inline void configure_system_fonts(
    RunnerParams& params,
    const HelloImGui::Fonts::SystemFontLoadingParams& fontParams =
        HelloImGui::Fonts::SystemFontLoadingParams(),
    HelloImGui::Fonts::SystemFontLoadingResult* result = nullptr) {
    params.callbacks.LoadAdditionalFonts = [fontParams, result] {
        auto loaded = HelloImGui::Fonts::LoadSystemFonts(fontParams);
        if (result) {
            *result = std::move(loaded);
        }
    };
}

/// 配置 backend 和字体等 LImGui 默认启动项。
inline bool configure(RunnerParams& params, const ConfigureParams& configureParams = ConfigureParams()) {
    bool backend_configured = true;
    if (params.platformBackendType == HelloImGui::PlatformBackendType::FirstAvailable &&
        params.renderingBackendType == HelloImGui::RendererBackendType::FirstAvailable) {
        backend_configured = configure_backend(params, configureParams.backendPreference);
    }
    if (configureParams.loadSystemFonts && !params.callbacks.LoadAdditionalFonts) {
        configure_system_fonts(params, configureParams.fontParams, configureParams.fontResult);
    }
    return backend_configured;
}

/// 配置外部提供的 GLFW + Vulkan backend 回调。
inline void configure_external_vulkan_backend(
    RunnerParams& params,
    HelloImGui::ExternalBackendCallbacks callbacks) {
    if (callbacks.description.empty()) {
        callbacks.description = "External - Vulkan";
    }
    params.platformBackendType = HelloImGui::PlatformBackendType::Glfw;
    params.renderingBackendType = HelloImGui::RendererBackendType::Vulkan;
    params.externalBackendCallbacks = std::move(callbacks);
}

/// 请求 HelloImGui 在当前帧后退出。
inline void RequestExit() {
    HelloImGui::RequestExit();
}

/// 返回 HelloImGui 统计的当前帧率。
inline float FrameRate() {
    return HelloImGui::FrameRate();
}

/// 自动 ID 后缀的固定宽度。
constexpr std::uint32_t auto_id_width = 8;
/// 自动 ID 后缀使用的字符表。
static constexpr char auto_id_alphabet[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
/// 自动 ID 字符表长度。
constexpr std::uint32_t auto_id_alphabet_size = sizeof(auto_id_alphabet) - 1;
/// 自动 ID 编码进制。
constexpr std::uint64_t auto_id_base = auto_id_alphabet_size;

/// 计算字符串的 constexpr FNV-1a 哈希。
constexpr std::uint64_t fnv1a_hash(const char* text,
                                   std::uint64_t hash = 0xcbf29ce484222325ULL) {
    return *text ? fnv1a_hash(text + 1,
                              (hash ^ static_cast<std::uint64_t>(*text)) *
                                  0x100000001b3ULL)
                 : hash;
}

/// 将整数值编码成固定宽度自动 ID 后缀。
template<std::uint64_t Value>
struct AutoIdSuffix {
    /// 返回空字符结尾的自动 ID 后缀。
    static constexpr std::array<char, auto_id_width + 1> value() {
        return {{
            auto_id_alphabet[(Value / 1ULL) % auto_id_base],
            auto_id_alphabet[(Value / auto_id_base) % auto_id_base],
            auto_id_alphabet[(Value / (auto_id_base * auto_id_base)) % auto_id_base],
            auto_id_alphabet[(Value / (auto_id_base * auto_id_base * auto_id_base)) %
                             auto_id_base],
            auto_id_alphabet[(Value / (auto_id_base * auto_id_base * auto_id_base *
                                       auto_id_base)) %
                             auto_id_base],
            auto_id_alphabet[(Value / (auto_id_base * auto_id_base * auto_id_base *
                                       auto_id_base * auto_id_base)) %
                             auto_id_base],
            auto_id_alphabet[(Value / (auto_id_base * auto_id_base * auto_id_base *
                                       auto_id_base * auto_id_base * auto_id_base)) %
                             auto_id_base],
            auto_id_alphabet[(Value / (auto_id_base * auto_id_base * auto_id_base *
                                       auto_id_base * auto_id_base * auto_id_base *
                                       auto_id_base)) %
                             auto_id_base],
            '\0'
        }};
    }
};

/// 为给定标签生成稳定的 ImGui 隐藏 ID。
template<std::uint64_t FileHash, std::uint64_t Line>
inline const char* auto_id(const char* label) {
    static constexpr auto suffix = AutoIdSuffix<(FileHash ^ Line)>::value();
    static const std::string value =
        std::string(label ? label : "") + "##" + suffix.data();
    return value.c_str();
}

/// 使用完整 RunnerParams 启动 HelloImGui。
inline void run(RunnerParams& params) {
    HelloImGui::Run(params);
}

/// 使用简化 SimpleRunnerParams 启动 HelloImGui。
inline void run(const SimpleRunnerParams& params) {
    HelloImGui::Run(params);
}

/// 使用单个 GUI 回调和窗口信息启动 HelloImGui。
inline void run(const VoidFunction& gui,
                const std::string& title = "LTool ImGui",
                const ScreenSize& size = DefaultWindowSize) {
    HelloImGui::Run(gui, title, false, false, size);
}

} // namespace LImGui

/// 拼接两个预处理 token 的内部实现宏。
#define LTOOL_IMGUI_CONCAT_INNER(left, right) left##right
/// 拼接两个预处理 token。
#define LTOOL_IMGUI_CONCAT(left, right) LTOOL_IMGUI_CONCAT_INNER(left, right)

/// 为当前文件行号生成稳定的 Dear ImGui 隐藏 ID。
#define LTOOL_IMGUI_AUTO_ID(label) \
    LImGui::auto_id<LImGui::fnv1a_hash(__FILE__), static_cast<std::uint64_t>(__LINE__)>(label)

#endif // LTOOL_HAS_IMGUI

#endif // LTOOL_DETAIL_LIMGUI_INCLUDE

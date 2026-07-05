/**
 * @file detail/LHelloImGui.hpp
 * @brief LTool header-only HelloImGui-compatible facade.
 *
 * Source reference: https://github.com/pthom/hello_imgui
 * Imported reference commit: e24530d0591a346f7869a007ad4b17d995cd01c7
 *
 * This is a header-only adaptation of the public HelloImGui API. It does not
 * vendor Dear ImGui or native backends. Backends are enabled when the host
 * project provides Dear ImGui backend headers/libraries, or through
 * RunnerParams.externalBackendCallbacks.
 */

#ifndef LTOOL_DETAIL_LHELLO_IMGUI_INCLUDE
#define LTOOL_DETAIL_LHELLO_IMGUI_INCLUDE

#include "LToolConfig.hpp"

#if LTOOL_HAS_IMGUI

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3
#include <GLFW/glfw3.h>
#if LTOOL_HAS_IMGUI_IMPL_GLFW_BACKENDS
#include <backends/imgui_impl_glfw.h>
#else
#include <imgui_impl_glfw.h>
#endif
#if LTOOL_HAS_IMGUI_IMPL_OPENGL3_BACKENDS
#include <backends/imgui_impl_opengl3.h>
#else
#include <imgui_impl_opengl3.h>
#endif
#endif

#if LTOOL_HAS_IMGUI_BACKEND_SDL2_OPENGL3
#if LTOOL_HAS_SDL2_PREFIXED
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#if LTOOL_HAS_IMGUI_IMPL_SDL2_BACKENDS
#include <backends/imgui_impl_sdl2.h>
#else
#include <imgui_impl_sdl2.h>
#endif
#if LTOOL_HAS_IMGUI_IMPL_OPENGL3_BACKENDS
#include <backends/imgui_impl_opengl3.h>
#else
#include <imgui_impl_opengl3.h>
#endif
#endif

#if LTOOL_HAS_IMGUI_BACKEND_SFML
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <imgui-SFML.h>
#endif

#if LTOOL_HAS_IMGUI_RENDERER_VULKAN
#include <vulkan/vulkan.h>
#if LTOOL_HAS_IMGUI_IMPL_VULKAN_BACKENDS
#include <backends/imgui_impl_vulkan.h>
#else
#include <imgui_impl_vulkan.h>
#endif
#endif

#if defined(__APPLE__) && LTOOL_HAS_INCLUDE(<OpenGL/gl3.h>)
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#define LTOOL_HELLO_IMGUI_HAS_GL_HEADER 1
#elif LTOOL_HAS_INCLUDE(<GL/gl.h>)
#include <GL/gl.h>
#define LTOOL_HELLO_IMGUI_HAS_GL_HEADER 1
#else
#define LTOOL_HELLO_IMGUI_HAS_GL_HEADER 0
#endif

#ifndef HIMG_ERROR
#define HIMG_ERROR(msg) throw std::runtime_error(std::string("HelloImGui: ") + (msg))
#endif

struct ImGuiTestEngine;

namespace ImGuiTheme {

enum ImGuiTheme_ {
    ImGuiTheme_ImGuiColorsClassic = 0,
    ImGuiTheme_ImGuiColorsDark,
    ImGuiTheme_ImGuiColorsLight,
    ImGuiTheme_MaterialFlat,
    ImGuiTheme_PhotoshopStyle,
    ImGuiTheme_GrayVariations,
    ImGuiTheme_GrayVariations_Darker,
    ImGuiTheme_MicrosoftStyle,
    ImGuiTheme_Cherry,
    ImGuiTheme_Darcula,
    ImGuiTheme_DarculaDarker,
    ImGuiTheme_LightRounded,
    ImGuiTheme_SoDark_AccentBlue,
    ImGuiTheme_SoDark_AccentYellow,
    ImGuiTheme_SoDark_AccentRed,
    ImGuiTheme_BlackIsBlack,
    ImGuiTheme_WhiteIsWhite,
    ImGuiTheme_Count
};

struct ImGuiThemeTweaks {
    float Rounding = -1.0f;
    float RoundingScrollbarRatio = 4.0f;
    float AlphaMultiplier = -1.0f;
    float Hue = -1.0f;
    float SaturationMultiplier = -1.0f;
    float ValueMultiplierFront = -1.0f;
    float ValueMultiplierBg = -1.0f;
    float ValueMultiplierText = -1.0f;
    float ValueMultiplierFrameBg = -1.0f;
};

struct ImGuiTweakedTheme {
    ImGuiTheme_ Theme = ImGuiTheme_DarculaDarker;
    ImGuiThemeTweaks Tweaks;
    ImGuiTweakedTheme(ImGuiTheme_ theme = ImGuiTheme_DarculaDarker,
                      const ImGuiThemeTweaks& tweaks = ImGuiThemeTweaks())
        : Theme(theme), Tweaks(tweaks) {}
};

inline const char* ImGuiTheme_Name(ImGuiTheme_ theme) {
    static const char* names[] = {
        "ImGuiColorsClassic", "ImGuiColorsDark", "ImGuiColorsLight",
        "MaterialFlat", "PhotoshopStyle", "GrayVariations",
        "GrayVariations_Darker", "MicrosoftStyle", "Cherry", "Darcula",
        "DarculaDarker", "LightRounded", "SoDark_AccentBlue",
        "SoDark_AccentYellow", "SoDark_AccentRed", "BlackIsBlack",
        "WhiteIsWhite"
    };
    const int index = static_cast<int>(theme);
    return index >= 0 && index < static_cast<int>(ImGuiTheme_Count) ? names[index] : "Unknown";
}

inline ImGuiTheme_ ImGuiTheme_FromName(const char* themeName) {
    if (!themeName) {
        return ImGuiTheme_DarculaDarker;
    }
    for (int i = 0; i < static_cast<int>(ImGuiTheme_Count); ++i) {
        if (std::strcmp(themeName, ImGuiTheme_Name(static_cast<ImGuiTheme_>(i))) == 0) {
            return static_cast<ImGuiTheme_>(i);
        }
    }
    return ImGuiTheme_DarculaDarker;
}

inline ImGuiStyle Darcula(float rounding = 1.0f,
                          float = -1.0f,
                          float = 1.0f,
                          float = 1.0f,
                          float = 1.0f,
                          float alpha_bg_transparency = 1.0f) {
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = rounding;
    style.ChildRounding = rounding;
    style.FrameRounding = rounding + 2.0f;
    style.PopupRounding = rounding + 2.0f;
    style.TabRounding = rounding + 3.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.16f, 0.17f, 0.20f, alpha_bg_transparency);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.24f, 0.28f, 0.92f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.45f, 0.90f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.48f, 0.79f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.82f, 0.86f, 1.00f);
    return style;
}

inline ImGuiStyle SoDark(float hue) {
    (void)hue;
    return Darcula(6.0f);
}

inline ImGuiStyle ShadesOfGray(float rounding = 0.0f,
                               float value_multiplier_front = 1.0f,
                               float value_multiplier_bg = 1.0f) {
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = rounding;
    style.FrameRounding = rounding;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        ImVec4& color = style.Colors[i];
        const float gray = (color.x + color.y + color.z) / 3.0f;
        const float mul = i == ImGuiCol_WindowBg || i == ImGuiCol_ChildBg || i == ImGuiCol_PopupBg
                              ? value_multiplier_bg
                              : value_multiplier_front;
        color.x = color.y = color.z = std::max(0.0f, std::min(1.0f, gray * mul));
    }
    return style;
}

inline ImGuiStyle ThemeToStyle(ImGuiTheme_ theme) {
    ImGuiStyle style;
    switch (theme) {
    case ImGuiTheme_ImGuiColorsClassic:
        ImGui::StyleColorsClassic(&style);
        break;
    case ImGuiTheme_ImGuiColorsLight:
    case ImGuiTheme_WhiteIsWhite:
    case ImGuiTheme_LightRounded:
        ImGui::StyleColorsLight(&style);
        style.WindowRounding = theme == ImGuiTheme_LightRounded ? 9.0f : style.WindowRounding;
        style.FrameRounding = theme == ImGuiTheme_LightRounded ? 7.0f : style.FrameRounding;
        break;
    case ImGuiTheme_GrayVariations:
    case ImGuiTheme_GrayVariations_Darker:
        style = ShadesOfGray(theme == ImGuiTheme_GrayVariations_Darker ? 3.0f : 0.0f, 1.0f, 0.85f);
        break;
    case ImGuiTheme_BlackIsBlack:
        ImGui::StyleColorsDark(&style);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 1);
        break;
    default:
        style = Darcula(theme == ImGuiTheme_DarculaDarker ? 4.0f : 6.0f);
        break;
    }
    return style;
}

inline ImGuiStyle TweakedThemeThemeToStyle(const ImGuiTweakedTheme& tweaked_theme) {
    ImGuiStyle style = ThemeToStyle(tweaked_theme.Theme);
    const ImGuiThemeTweaks& tweaks = tweaked_theme.Tweaks;
    if (tweaks.Rounding >= 0.0f) {
        style.WindowRounding = tweaks.Rounding;
        style.ChildRounding = tweaks.Rounding;
        style.FrameRounding = tweaks.Rounding;
        style.PopupRounding = tweaks.Rounding;
        style.TabRounding = tweaks.Rounding;
        style.GrabRounding = tweaks.Rounding;
        style.ScrollbarRounding = tweaks.Rounding / std::max(1.0f, tweaks.RoundingScrollbarRatio);
    }
    if (tweaks.AlphaMultiplier >= 0.0f) {
        style.Alpha *= tweaks.AlphaMultiplier;
    }
    return style;
}

inline void ApplyTheme(ImGuiTheme_ theme) {
    ImGuiStyle style = ThemeToStyle(theme);
    ImGui::GetStyle() = style;
}

inline void ApplyTweakedTheme(const ImGuiTweakedTheme& tweaked_theme) {
    ImGuiStyle style = TweakedThemeThemeToStyle(tweaked_theme);
    ImGui::GetStyle() = style;
}

inline void PushTweakedTheme(const ImGuiTweakedTheme& tweaked_theme) {
    ApplyTweakedTheme(tweaked_theme);
}

inline void PopTweakedTheme() {}

inline bool ShowThemeTweakGui(ImGuiTweakedTheme* tweaked_theme) {
    if (!tweaked_theme) {
        return false;
    }
    bool changed = false;
    int theme = static_cast<int>(tweaked_theme->Theme);
    const char* preview = ImGuiTheme_Name(tweaked_theme->Theme);
    if (ImGui::BeginCombo("Theme", preview)) {
        for (int i = 0; i < static_cast<int>(ImGuiTheme_Count); ++i) {
            const bool selected = theme == i;
            if (ImGui::Selectable(ImGuiTheme_Name(static_cast<ImGuiTheme_>(i)), selected)) {
                theme = i;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    changed |= ImGui::SliderFloat("Rounding", &tweaked_theme->Tweaks.Rounding, -1.0f, 16.0f);
    if (changed) {
        tweaked_theme->Theme = static_cast<ImGuiTheme_>(theme);
        ApplyTweakedTheme(*tweaked_theme);
    }
    return changed;
}

} // namespace ImGuiTheme

namespace HelloImGui {

using VoidFunction = std::function<void(void)>;
using AnyEventCallback = std::function<bool(void*)>;
using ConfirmExitCallback = std::function<bool(void)>;

inline VoidFunction EmptyVoidFunction() { return {}; }
inline AnyEventCallback EmptyEventCallback() { return {}; }
inline ConfirmExitCallback EmptyConfirmExitCallback() { return {}; }

inline VoidFunction SequenceFunctions(const VoidFunction& f1, const VoidFunction& f2) {
    if (!f1) {
        return f2;
    }
    if (!f2) {
        return f1;
    }
    return [f1, f2] {
        f1();
        f2();
    };
}

inline VoidFunction AppendCallback(const VoidFunction& previousCallback, const VoidFunction& newCallback) {
    return SequenceFunctions(previousCallback, newCallback);
}

struct ScreenPosition {
    int x = 0;
    int y = 0;
    int& operator[](std::size_t index) { return index == 0 ? x : y; }
    const int& operator[](std::size_t index) const { return index == 0 ? x : y; }
};

struct ScreenSize {
    int width = 800;
    int height = 600;
    int& operator[](std::size_t index) { return index == 0 ? width : height; }
    const int& operator[](std::size_t index) const { return index == 0 ? width : height; }
};

constexpr ScreenPosition DefaultScreenPosition = {0, 0};
constexpr ScreenSize DefaultWindowSize = {800, 600};

struct ScreenBounds {
    ScreenPosition position = DefaultScreenPosition;
    ScreenSize size = DefaultWindowSize;

    ScreenPosition TopLeftCorner() const { return position; }
    ScreenPosition BottomRightCorner() const { return {position.x + size.width, position.y + size.height}; }
    ScreenPosition Center() const { return {position.x + size.width / 2, position.y + size.height / 2}; }
    bool Contains(ScreenPosition pixel) const {
        return pixel.x >= position.x && pixel.y >= position.y &&
               pixel.x < position.x + size.width && pixel.y < position.y + size.height;
    }
    ScreenPosition WinPositionCentered(ScreenSize windowSize) const {
        return {position.x + (size.width - windowSize.width) / 2,
                position.y + (size.height - windowSize.height) / 2};
    }
    int DistanceFromPixel(ScreenPosition point) const {
        const ScreenPosition c = Center();
        const int dx = c.x - point.x;
        const int dy = c.y - point.y;
        return dx * dx + dy * dy;
    }
    ScreenBounds EnsureWindowFitsThisMonitor(ScreenBounds windowBoundsOriginal) const {
        windowBoundsOriginal.position.x =
            std::max(position.x, std::min(windowBoundsOriginal.position.x, position.x + size.width - windowBoundsOriginal.size.width));
        windowBoundsOriginal.position.y =
            std::max(position.y, std::min(windowBoundsOriginal.position.y, position.y + size.height - windowBoundsOriginal.size.height));
        return windowBoundsOriginal;
    }
    bool operator==(const ScreenBounds& other) const {
        return position.x == other.position.x && position.y == other.position.y &&
               size.width == other.size.width && size.height == other.size.height;
    }
};

enum class FullScreenMode { NoFullScreen, FullScreen, FullScreenDesktopResolution, FullMonitorWorkArea };
enum class WindowSizeState { Standard, Minimized, Maximized };
enum class WindowPositionMode { OsDefault, MonitorCenter, FromCoords };
enum class EmscriptenKeyboardElement { Window, Document, Screen, Canvas, Default };
enum class WindowSizeMeasureMode { ScreenCoords, RelativeTo96Ppi };

struct WindowGeometry {
    ScreenSize size = DefaultWindowSize;
    bool sizeAuto = false;
    WindowSizeState windowSizeState = WindowSizeState::Standard;
    WindowSizeMeasureMode windowSizeMeasureMode = WindowSizeMeasureMode::RelativeTo96Ppi;
    WindowPositionMode positionMode = WindowPositionMode::OsDefault;
    ScreenPosition position = {40, 40};
    int monitorIdx = 0;
    FullScreenMode fullScreenMode = FullScreenMode::NoFullScreen;
    bool resizeAppWindowAtNextFrame = false;
};

struct EdgeInsets {
    double top = 0.0;
    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
};

struct AppWindowParams {
    std::string windowTitle = "LTool ImGui";
    WindowGeometry windowGeometry;
    bool restorePreviousGeometry = false;
    bool resizable = true;
    bool hidden = false;
    bool topMost = false;
    bool borderless = false;
    bool borderlessMovable = true;
    bool borderlessResizable = true;
    bool borderlessClosable = true;
    ImVec4 borderlessHighlightColor = ImVec4(0.2f, 0.4f, 1.0f, 0.3f);
    EdgeInsets edgeInsets;
    bool handleEdgeInsets = true;
    EmscriptenKeyboardElement emscriptenKeyboardElement = EmscriptenKeyboardElement::Default;
    bool emscriptenAllowBrowserZoomShortcuts = true;
    bool repaintDuringResize_GotchaReentrantRepaint = false;
    bool vsync = true; // LTool compatibility alias; upstream uses fpsIdling.vsyncToMonitor.
};

enum class DefaultImGuiWindowType {
    ProvideFullScreenWindow,
    ProvideFullScreenDockSpace,
    NoDefaultWindow
};

struct ImGuiWindowParams {
    DefaultImGuiWindowType defaultImGuiWindowType = DefaultImGuiWindowType::ProvideFullScreenWindow;
    bool enableViewports = false;
    bool configWindowsMoveFromTitleBarOnly = true;
    std::string menuAppTitle;
    bool showMenuBar = false;
    bool showMenu_App = true;
    bool showMenu_App_Quit = true;
    bool showMenu_View = true;
    bool showMenu_View_Themes = true;
    bool rememberTheme = true;
    bool showStatusBar = false;
    bool showStatus_Fps = true;
    bool rememberStatusBarSettings = true;
    ImVec2 fullScreenWindow_MarginTopLeft = ImVec2(0.0f, 0.0f);
    ImVec2 fullScreenWindow_MarginBottomRight = ImVec2(0.0f, 0.0f);
    ImGuiTheme::ImGuiTweakedTheme tweakedTheme;
    ImVec4 backgroundColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
};

enum class EdgeToolbarType { Top, Bottom, Left, Right };

struct EdgeToolbarOptions {
    float sizeEm = 2.5f;
    ImVec2 WindowPaddingEm = ImVec2(0.3f, 0.3f);
    ImVec4 WindowBg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
};

struct EdgeToolbar {
    VoidFunction ShowToolbar = EmptyVoidFunction();
    EdgeToolbarOptions options;
};

inline std::vector<EdgeToolbarType> AllEdgeToolbarTypes() {
    return {EdgeToolbarType::Top, EdgeToolbarType::Bottom, EdgeToolbarType::Left, EdgeToolbarType::Right};
}

inline std::string EdgeToolbarTypeName(EdgeToolbarType e) {
    switch (e) {
    case EdgeToolbarType::Top: return "Top";
    case EdgeToolbarType::Bottom: return "Bottom";
    case EdgeToolbarType::Left: return "Left";
    case EdgeToolbarType::Right: return "Right";
    }
    return "Unknown";
}

enum class DefaultIconFont { NoIcons, FontAwesome4, FontAwesome6 };

struct MobileCallbacks {
    VoidFunction OnDestroy = EmptyVoidFunction();
    VoidFunction OnLowMemory = EmptyVoidFunction();
    VoidFunction OnPause = EmptyVoidFunction();
    VoidFunction OnResume = EmptyVoidFunction();
};

struct RunnerCallbacks {
    VoidFunction ShowGui = EmptyVoidFunction();
    VoidFunction ShowMenus = EmptyVoidFunction();
    VoidFunction ShowAppMenuItems = EmptyVoidFunction();
    VoidFunction ShowStatus = EmptyVoidFunction();
    std::map<EdgeToolbarType, EdgeToolbar> edgesToolbars;
    VoidFunction PostInit_AddPlatformBackendCallbacks = EmptyVoidFunction();
    VoidFunction PostInit = EmptyVoidFunction();
    VoidFunction LoadAdditionalFonts;
    DefaultIconFont defaultIconFont = DefaultIconFont::FontAwesome4;
    VoidFunction SetupImGuiConfig;
    VoidFunction SetupImGuiStyle;
    VoidFunction RegisterTests = EmptyVoidFunction();
    bool registerTestsCalled = false;
    ConfirmExitCallback ConfirmExit = EmptyConfirmExitCallback();
    VoidFunction BeforeExit = EmptyVoidFunction();
    VoidFunction BeforeExit_PostCleanup = EmptyVoidFunction();
    VoidFunction PreNewFrame = EmptyVoidFunction();
    VoidFunction PostNewFrame = EmptyVoidFunction();
    VoidFunction BeforeImGuiRender = EmptyVoidFunction();
    VoidFunction AfterSwap = EmptyVoidFunction();
    VoidFunction CustomBackground = EmptyVoidFunction();
    VoidFunction PostRenderDockableWindows = EmptyVoidFunction();
    VoidFunction ThemeChanged = EmptyVoidFunction();
    AnyEventCallback AnyBackendEventCallback = EmptyEventCallback();
    MobileCallbacks mobileCallbacks;

    void AddEdgeToolbar(EdgeToolbarType edgeToolbarType,
                        VoidFunction guiFunction,
                        const EdgeToolbarOptions& options = EdgeToolbarOptions()) {
        EdgeToolbar toolbar;
        toolbar.ShowToolbar = std::move(guiFunction);
        toolbar.options = options;
        edgesToolbars[edgeToolbarType] = std::move(toolbar);
    }
    void EnqueuePostInit(const VoidFunction& callback) { PostInit = SequenceFunctions(PostInit, callback); }
    void EnqueueBeforeExit(const VoidFunction& callback) { BeforeExit = SequenceFunctions(BeforeExit, callback); }
};

using DockSpaceName = std::string;

struct DockingSplit {
    DockSpaceName initialDock;
    DockSpaceName newDock;
    ImGuiDir direction = ImGuiDir_Down;
    float ratio = 0.25f;
    int nodeFlags = 0;

    DockingSplit(const DockSpaceName& initialDock_ = "",
                 const DockSpaceName& newDock_ = "",
                 ImGuiDir direction_ = ImGuiDir_Down,
                 float ratio_ = 0.25f,
                 int nodeFlags_ = 0)
        : initialDock(initialDock_), newDock(newDock_), direction(direction_),
          ratio(ratio_), nodeFlags(nodeFlags_) {}
};

struct DockableWindow {
    std::string label;
    DockSpaceName dockSpaceName;
    VoidFunction GuiFunction = EmptyVoidFunction();
    bool isVisible = true;
    bool rememberIsVisible = true;
    bool canBeClosed = true;
    bool callBeginEnd = true;
    bool includeInViewMenu = true;
    ImGuiWindowFlags imGuiWindowFlags = 0;
    bool focusWindowAtNextFrame = false;
    ImVec2 windowSize = ImVec2(0.0f, 0.0f);
    ImGuiCond windowSizeCondition = ImGuiCond_FirstUseEver;
    ImVec2 windowPosition = ImVec2(0.0f, 0.0f);
    ImGuiCond windowPositionCondition = ImGuiCond_FirstUseEver;

    DockableWindow(const std::string& label_ = "",
                   const DockSpaceName& dockSpaceName_ = "",
                   const VoidFunction guiFunction_ = EmptyVoidFunction(),
                   bool isVisible_ = true,
                   bool canBeClosed_ = true)
        : label(label_), dockSpaceName(dockSpaceName_), GuiFunction(guiFunction_),
          isVisible(isVisible_), canBeClosed(canBeClosed_) {}
};

enum class DockingLayoutCondition { FirstUseEver, ApplicationStart, Never };

struct DockingParams {
    std::vector<DockingSplit> dockingSplits;
    std::vector<DockableWindow> dockableWindows;
    std::string layoutName = "Default";
    int mainDockSpaceNodeFlags = 0;
    DockingLayoutCondition layoutCondition = DockingLayoutCondition::FirstUseEver;
    bool layoutReset = false;

    DockableWindow* dockableWindowOfName(const std::string& name) {
        for (auto& window : dockableWindows) {
            if (window.label == name) {
                return &window;
            }
        }
        return nullptr;
    }
    bool focusDockableWindow(const std::string& windowName) {
        if (auto* window = dockableWindowOfName(windowName)) {
            window->isVisible = true;
            window->focusWindowAtNextFrame = true;
            return true;
        }
        return false;
    }
    std::optional<ImGuiID> dockSpaceIdFromName(const std::string& dockSpaceName) {
        if (dockSpaceName.empty()) {
            return std::nullopt;
        }
        return ImGui::GetID(dockSpaceName.c_str());
    }
};

struct BackendPointers {
    void* glfwWindow = nullptr;
    void* sdlWindow = nullptr;
    void* sdlGlContext = nullptr;
    void* sfmlWindow = nullptr;
    void* vulkanInstance = nullptr;
    void* vulkanDevice = nullptr;
};

struct RemoteParams {
    bool enableRemoting = false;
    int wsPort = 5003;
    std::string wsHttpRootFolder;
    bool wsProvideIndexHtml = true;
    bool exitWhenServerDisconnected = false;
    double durationMaxDisconnected = 30.0;
    std::string serverHost = "localhost";
    std::uint32_t serverPort = 8888;
    bool transmitWindowSize = false;
};

struct OpenGlOptions {
    std::optional<std::string> GlslVersion = std::nullopt;
    std::optional<int> MajorVersion = std::nullopt;
    std::optional<int> MinorVersion = std::nullopt;
    std::optional<bool> UseCoreProfile = std::nullopt;
    std::optional<bool> UseForwardCompat = std::nullopt;
    std::optional<int> AntiAliasingSamples = std::nullopt;
};

struct VulkanOptions {
    void* instance = nullptr;
    void* physicalDevice = nullptr;
    void* device = nullptr;
    void* queue = nullptr;
    std::uint32_t queueFamily = 0;
};

struct RendererBackendOptions {
    OpenGlOptions openGlOptions;
    VulkanOptions vulkanOptions;
};

struct DpiAwareParams {
    float dpiWindowSizeFactor = 0.0f;
};

enum class PlatformBackendType {
    FirstAvailable,
    Glfw,
    Sdl,
    Sfml,
    OpenCv,
    Null
};

enum class RendererBackendType {
    FirstAvailable,
    OpenGL3,
    Metal,
    Vulkan,
    DirectX11,
    DirectX12,
    Sfml,
    OpenCv,
    Null
};

inline std::string PlatformBackendTypeToString(PlatformBackendType platformBackendType) {
    switch (platformBackendType) {
    case PlatformBackendType::FirstAvailable: return "FirstAvailable";
    case PlatformBackendType::Glfw: return "Glfw";
    case PlatformBackendType::Sdl: return "Sdl";
    case PlatformBackendType::Sfml: return "Sfml";
    case PlatformBackendType::OpenCv: return "OpenCv";
    case PlatformBackendType::Null: return "Null";
    }
    return "Unknown";
}

inline std::string RendererBackendTypeToString(RendererBackendType rendererBackendType) {
    switch (rendererBackendType) {
    case RendererBackendType::FirstAvailable: return "FirstAvailable";
    case RendererBackendType::OpenGL3: return "OpenGL3";
    case RendererBackendType::Metal: return "Metal";
    case RendererBackendType::Vulkan: return "Vulkan";
    case RendererBackendType::DirectX11: return "DirectX11";
    case RendererBackendType::DirectX12: return "DirectX12";
    case RendererBackendType::Sfml: return "Sfml";
    case RendererBackendType::OpenCv: return "OpenCv";
    case RendererBackendType::Null: return "Null";
    }
    return "Unknown";
}

enum class IniFolderType {
    CurrentFolder,
    AbsolutePath,
    AppUserConfigFolder,
    AppExecutableFolder,
    HomeFolder,
    DocumentsFolder,
    TempFolder
};

inline std::string IniFolderLocation(IniFolderType iniFolderType) {
    switch (iniFolderType) {
    case IniFolderType::TempFolder:
        if (const char* value = std::getenv("TMPDIR")) return value;
        if (const char* value = std::getenv("TEMP")) return value;
        return ".";
    case IniFolderType::HomeFolder:
    case IniFolderType::DocumentsFolder:
    case IniFolderType::AppUserConfigFolder:
        if (const char* value = std::getenv("HOME")) return value;
        if (const char* value = std::getenv("USERPROFILE")) return value;
        return ".";
    default:
        return ".";
    }
}

enum class FpsIdlingMode { Sleep, EarlyReturn, Auto };

struct FpsIdling {
    float fpsIdle = 9.0f;
    float timeActiveAfterLastEvent = 3.0f;
    bool enableIdling = true;
    bool isIdling = false;
    bool rememberEnableIdling = false;
    FpsIdlingMode fpsIdlingMode = FpsIdlingMode::Auto;
    bool vsyncToMonitor = true;
    float fpsMax = 0.0f;
};

struct ExternalBackendCallbacks {
    std::string description;
    VoidFunction Setup = EmptyVoidFunction();
    VoidFunction PollEvents = EmptyVoidFunction();
    VoidFunction NewFrame = EmptyVoidFunction();
    VoidFunction RenderDrawData = EmptyVoidFunction();
    VoidFunction SwapBuffers = EmptyVoidFunction();
    VoidFunction Shutdown = EmptyVoidFunction();
    std::function<bool()> ShouldClose;
};

struct RunnerParams {
    RunnerCallbacks callbacks;
    AppWindowParams appWindowParams;
    ImGuiWindowParams imGuiWindowParams;
    DockingParams dockingParams;
    std::vector<DockingParams> alternativeDockingLayouts;
    bool rememberSelectedAlternativeLayout = true;
    BackendPointers backendPointers;
    RendererBackendOptions rendererBackendOptions;
    ExternalBackendCallbacks externalBackendCallbacks;
    PlatformBackendType platformBackendType = PlatformBackendType::FirstAvailable;
    RendererBackendType renderingBackendType = RendererBackendType::FirstAvailable;
    IniFolderType iniFolderType = IniFolderType::CurrentFolder;
    std::string iniFilename;
    bool iniFilename_useAppWindowTitle = true;
    bool iniDisable = false;
    bool iniClearPreviousSettings = false;
    bool appShallExit = false;
    FpsIdling fpsIdling;
    DpiAwareParams dpiAwareParams;
    bool useImGuiTestEngine = false;
    int emscripten_fps = 0;
    RemoteParams remoteParams;

    const char* glslVersion = nullptr; // LTool legacy compatibility.
    bool disableImGuiIni = false;      // LTool legacy compatibility.
    void (*applyTheme)() = nullptr;    // LTool legacy compatibility.
};

struct SimpleRunnerParams {
    VoidFunction guiFunction = EmptyVoidFunction();
    std::string windowTitle;
    bool windowSizeAuto = false;
    bool windowRestorePreviousGeometry = false;
    ScreenSize windowSize = DefaultWindowSize;
    float fpsIdle = 9.0f;
    bool enableIdling = true;
    bool topMost = false;
    bool iniDisable = false;

    RunnerParams ToRunnerParams() const {
        RunnerParams params;
        params.callbacks.ShowGui = guiFunction;
        params.appWindowParams.windowTitle = windowTitle;
        params.appWindowParams.windowGeometry.sizeAuto = windowSizeAuto;
        params.appWindowParams.windowGeometry.size = windowSize;
        params.appWindowParams.restorePreviousGeometry = windowRestorePreviousGeometry;
        params.appWindowParams.topMost = topMost;
        params.fpsIdling.fpsIdle = fpsIdle;
        params.fpsIdling.enableIdling = enableIdling;
        params.iniDisable = iniDisable;
        return params;
    }
};

namespace Fonts {

struct SystemFontLoadingParams {
    float fontSize = 18.0f;
    float chineseFontSize = 18.0f;
    bool useFullChineseGlyphRanges = false;
    bool mergeAllChineseFallbacks = false;
    bool loadImGuiDefaultIfNoSystemFont = true;
    std::vector<std::string> preferredLatinFonts;
    std::vector<std::string> preferredChineseFonts;
};

struct SystemFontLoadingResult {
    ImFont* defaultFont = nullptr;
    bool usedImGuiDefaultFont = false;
    bool loadedLatinFont = false;
    bool loadedChineseFont = false;
    std::vector<std::string> loadedFontFiles;
};

inline bool FontFileExists(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return in.good();
}

inline std::vector<std::string> DefaultLatinFontPaths() {
    std::vector<std::string> paths;
#if defined(_WIN32)
    paths = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf"
    };
#elif defined(__APPLE__)
    paths = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
    };
#else
    paths = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/local/share/fonts/NotoSans-Regular.ttf"
    };
#endif
    return paths;
}

inline std::vector<std::string> DefaultChineseFontPaths() {
    std::vector<std::string> paths;
#if defined(_WIN32)
    paths = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/Deng.ttf",
        "C:/Windows/Fonts/msjhl.ttc"
    };
#elif defined(__APPLE__)
    paths = {
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/System/Library/Fonts/Supplemental/Heiti TC.ttc"
    };
#else
    paths = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/opentype/source-han-sans/SourceHanSansCN-Regular.otf",
        "/usr/local/share/fonts/NotoSansCJK-Regular.ttc"
    };
#endif
    return paths;
}

inline std::vector<std::string> ExistingFontPaths(const std::vector<std::string>& candidates) {
    std::vector<std::string> result;
    for (const auto& path : candidates) {
        if (FontFileExists(path)) {
            result.push_back(path);
        }
    }
    return result;
}

inline const ImWchar* ChineseGlyphRanges(bool full) {
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    if (!fonts) {
        return nullptr;
    }
    return full ? fonts->GetGlyphRangesChineseFull()
                : fonts->GetGlyphRangesChineseSimplifiedCommon();
}

inline SystemFontLoadingResult LoadSystemFonts(const SystemFontLoadingParams& params = SystemFontLoadingParams()) {
    SystemFontLoadingResult result;
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts) {
        return result;
    }

    std::vector<std::string> latinCandidates = params.preferredLatinFonts;
    const auto defaultLatin = DefaultLatinFontPaths();
    latinCandidates.insert(latinCandidates.end(), defaultLatin.begin(), defaultLatin.end());

    std::vector<std::string> chineseCandidates = params.preferredChineseFonts;
    const auto defaultChinese = DefaultChineseFontPaths();
    chineseCandidates.insert(chineseCandidates.end(), defaultChinese.begin(), defaultChinese.end());

    ImFontConfig latinConfig;
    latinConfig.OversampleH = 2;
    latinConfig.OversampleV = 1;
    for (const auto& path : latinCandidates) {
        if (!FontFileExists(path)) {
            continue;
        }
        result.defaultFont = io.Fonts->AddFontFromFileTTF(
            path.c_str(), params.fontSize, &latinConfig, io.Fonts->GetGlyphRangesDefault());
        if (result.defaultFont) {
            result.loadedLatinFont = true;
            result.loadedFontFiles.push_back(path);
            break;
        }
    }

    if (!result.defaultFont && params.loadImGuiDefaultIfNoSystemFont) {
        result.defaultFont = io.Fonts->AddFontDefault();
        result.usedImGuiDefaultFont = result.defaultFont != nullptr;
    }

    ImFontConfig chineseConfig;
    chineseConfig.MergeMode = result.defaultFont != nullptr;
    chineseConfig.OversampleH = 2;
    chineseConfig.OversampleV = 1;
    const ImWchar* chineseRanges = ChineseGlyphRanges(params.useFullChineseGlyphRanges);
    for (const auto& path : chineseCandidates) {
        if (!FontFileExists(path)) {
            continue;
        }
        ImFont* loaded = io.Fonts->AddFontFromFileTTF(
            path.c_str(), params.chineseFontSize, &chineseConfig, chineseRanges);
        if (loaded) {
            if (!result.defaultFont) {
                result.defaultFont = loaded;
            }
            result.loadedChineseFont = true;
            result.loadedFontFiles.push_back(path);
            if (!params.mergeAllChineseFallbacks) {
                break;
            }
            chineseConfig.MergeMode = true;
        }
    }

    if (!result.defaultFont && params.loadImGuiDefaultIfNoSystemFont) {
        result.defaultFont = io.Fonts->AddFontDefault();
        result.usedImGuiDefaultFont = result.defaultFont != nullptr;
    }
    return result;
}

inline SystemFontLoadingResult LoadSystemFonts(float fontSize,
                                               bool useFullChineseGlyphRanges = false) {
    SystemFontLoadingParams params;
    params.fontSize = fontSize;
    params.chineseFontSize = fontSize;
    params.useFullChineseGlyphRanges = useFullChineseGlyphRanges;
    return LoadSystemFonts(params);
}

} // namespace Fonts

namespace ImGuiDefaultSettings {
inline void LoadDefaultFont_WithFontAwesomeIcons() {
    (void)Fonts::LoadSystemFonts();
}

inline void SetupDefaultImGuiConfig() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
#ifdef ImGuiConfigFlags_NavEnableKeyboard
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#endif
}

inline void SetupDefaultImGuiStyle() {
    ImGuiTheme::ApplyTheme(ImGuiTheme::ImGuiTheme_DarculaDarker);
}
} // namespace ImGuiDefaultSettings

namespace detail {

struct BackendRuntime {
    std::string description;
    VoidFunction poll_events;
    VoidFunction new_frame;
    VoidFunction render_draw_data;
    VoidFunction swap_buffers;
    VoidFunction shutdown;
    std::function<bool()> should_close;
};

inline RunnerParams*& current_runner_params_ref() {
    static RunnerParams* params = nullptr;
    return params;
}

inline BackendRuntime*& current_backend_ref() {
    static BackendRuntime* backend = nullptr;
    return backend;
}

inline std::string& assets_folder_ref() {
    static std::string folder = "assets";
    return folder;
}

inline std::vector<std::string>& assets_search_paths_ref() {
    static std::vector<std::string> paths;
    return paths;
}

inline std::vector<std::pair<int, std::string>>& logs_ref() {
    static std::vector<std::pair<int, std::string>> logs;
    return logs;
}

inline void setup_imgui_defaults(RunnerParams& params) {
    if (params.disableImGuiIni) {
        params.iniDisable = true;
    }
    if (params.iniDisable) {
        ImGui::GetIO().IniFilename = nullptr;
    }
    if (params.callbacks.SetupImGuiConfig) {
        params.callbacks.SetupImGuiConfig();
    } else {
        ImGuiDefaultSettings::SetupDefaultImGuiConfig();
    }
    if (params.callbacks.SetupImGuiStyle) {
        params.callbacks.SetupImGuiStyle();
    } else if (params.applyTheme) {
        params.applyTheme();
    } else {
        ImGuiTheme::ApplyTweakedTheme(params.imGuiWindowParams.tweakedTheme);
    }
    if (params.callbacks.LoadAdditionalFonts) {
        params.callbacks.LoadAdditionalFonts();
    } else {
        ImGuiDefaultSettings::LoadDefaultFont_WithFontAwesomeIcons();
    }
}

inline const char* glsl_version(const RunnerParams& params) {
    if (params.glslVersion) {
        return params.glslVersion;
    }
    if (params.rendererBackendOptions.openGlOptions.GlslVersion) {
        static std::string version;
        version = "#" + *params.rendererBackendOptions.openGlOptions.GlslVersion;
        return version.c_str();
    }
#if defined(__APPLE__)
    return "#version 150";
#else
    return "#version 130";
#endif
}

inline bool wants(PlatformBackendType actual, PlatformBackendType wanted) {
    return wanted == PlatformBackendType::FirstAvailable || wanted == actual;
}

inline bool wants(RendererBackendType actual, RendererBackendType wanted) {
    return wanted == RendererBackendType::FirstAvailable || wanted == actual;
}

inline void render_dockable_windows(RunnerParams& params) {
    for (DockableWindow& window : params.dockingParams.dockableWindows) {
        if (!window.isVisible || !window.GuiFunction) {
            continue;
        }
        if (window.focusWindowAtNextFrame) {
            ImGui::SetNextWindowFocus();
            window.focusWindowAtNextFrame = false;
        }
        if (window.windowSize.x > 0.0f || window.windowSize.y > 0.0f) {
            ImGui::SetNextWindowSize(window.windowSize, window.windowSizeCondition);
        }
        if (window.windowPosition.x != 0.0f || window.windowPosition.y != 0.0f) {
            ImGui::SetNextWindowPos(window.windowPosition, window.windowPositionCondition);
        }
        bool visible = window.isVisible;
        if (window.callBeginEnd) {
            const bool opened = ImGui::Begin(window.label.c_str(),
                                             window.canBeClosed ? &visible : nullptr,
                                             window.imGuiWindowFlags);
            window.isVisible = visible;
            if (opened) {
                window.GuiFunction();
            }
            ImGui::End();
        } else {
            window.GuiFunction();
        }
    }
}

inline void draw_default_gui(RunnerParams& params) {
    if (params.imGuiWindowParams.showMenuBar && ImGui::BeginMainMenuBar()) {
        if (params.imGuiWindowParams.showMenu_App && ImGui::BeginMenu(
                params.imGuiWindowParams.menuAppTitle.empty()
                    ? (params.appWindowParams.windowTitle.empty() ? "App" : params.appWindowParams.windowTitle.c_str())
                    : params.imGuiWindowParams.menuAppTitle.c_str())) {
            if (params.callbacks.ShowAppMenuItems) {
                params.callbacks.ShowAppMenuItems();
            }
            if (params.imGuiWindowParams.showMenu_App_Quit && ImGui::MenuItem("Quit")) {
                params.appShallExit = true;
            }
            ImGui::EndMenu();
        }
        if (params.imGuiWindowParams.showMenu_View && ImGui::BeginMenu("View")) {
            for (DockableWindow& window : params.dockingParams.dockableWindows) {
                if (window.includeInViewMenu) {
                    ImGui::MenuItem(window.label.c_str(), nullptr, &window.isVisible);
                }
            }
            if (params.imGuiWindowParams.showMenu_View_Themes && ImGui::BeginMenu("Themes")) {
                for (int i = 0; i < static_cast<int>(ImGuiTheme::ImGuiTheme_Count); ++i) {
                    if (ImGui::MenuItem(ImGuiTheme::ImGuiTheme_Name(static_cast<ImGuiTheme::ImGuiTheme_>(i)))) {
                        params.imGuiWindowParams.tweakedTheme.Theme = static_cast<ImGuiTheme::ImGuiTheme_>(i);
                        ImGuiTheme::ApplyTweakedTheme(params.imGuiWindowParams.tweakedTheme);
                        if (params.callbacks.ThemeChanged) {
                            params.callbacks.ThemeChanged();
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (params.callbacks.ShowMenus) {
            params.callbacks.ShowMenus();
        }
        ImGui::EndMainMenuBar();
    }

    if (params.callbacks.ShowGui) {
        params.callbacks.ShowGui();
    }
    render_dockable_windows(params);
    if (params.callbacks.PostRenderDockableWindows) {
        params.callbacks.PostRenderDockableWindows();
    }
    if (params.imGuiWindowParams.showStatusBar && params.callbacks.ShowStatus) {
        params.callbacks.ShowStatus();
    }
}

inline bool create_imgui_context_if_needed() {
    if (ImGui::GetCurrentContext()) {
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    return true;
}

inline BackendRuntime make_external_backend(RunnerParams& params) {
    if (!params.externalBackendCallbacks.Setup && !params.externalBackendCallbacks.NewFrame &&
        !params.externalBackendCallbacks.RenderDrawData) {
        throw std::runtime_error("HelloImGui external backend requires at least Setup/NewFrame/RenderDrawData callbacks");
    }
    const bool owns_context = create_imgui_context_if_needed();
    setup_imgui_defaults(params);
    if (params.externalBackendCallbacks.Setup) {
        params.externalBackendCallbacks.Setup();
    }
    BackendRuntime runtime;
    runtime.description = params.externalBackendCallbacks.description.empty()
                              ? "External backend"
                              : params.externalBackendCallbacks.description;
    runtime.poll_events = params.externalBackendCallbacks.PollEvents;
    runtime.new_frame = params.externalBackendCallbacks.NewFrame;
    runtime.render_draw_data = params.externalBackendCallbacks.RenderDrawData;
    runtime.swap_buffers = params.externalBackendCallbacks.SwapBuffers;
    runtime.should_close = params.externalBackendCallbacks.ShouldClose;
    runtime.shutdown = [callbacks = params.externalBackendCallbacks, owns_context] {
        if (callbacks.Shutdown) {
            callbacks.Shutdown();
        }
        if (owns_context) {
            ImGui::DestroyContext();
        }
    };
    return runtime;
}

inline BackendRuntime make_null_backend(RunnerParams& params) {
    const bool owns_context = create_imgui_context_if_needed();
    setup_imgui_defaults(params);
    BackendRuntime runtime;
    runtime.description = "Null";
    runtime.poll_events = [] {};
    runtime.new_frame = [] {};
    runtime.render_draw_data = [] {};
    runtime.swap_buffers = [] {};
    runtime.should_close = [] { return false; };
    runtime.shutdown = [owns_context] {
        if (owns_context) {
            ImGui::DestroyContext();
        }
    };
    return runtime;
}

#if LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3 && LTOOL_HELLO_IMGUI_HAS_GL_HEADER
inline BackendRuntime make_glfw_opengl3_backend(RunnerParams& params) {
    if (!glfwInit()) {
        throw std::runtime_error("HelloImGui failed to initialize GLFW");
    }
    const auto& geom = params.appWindowParams.windowGeometry;
#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, params.rendererBackendOptions.openGlOptions.MajorVersion.value_or(3));
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, params.rendererBackendOptions.openGlOptions.MinorVersion.value_or(0));
#endif
    glfwWindowHint(GLFW_RESIZABLE, params.appWindowParams.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, params.appWindowParams.hidden ? GLFW_FALSE : GLFW_TRUE);
    if (params.appWindowParams.borderless) {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }
    GLFWwindow* window = glfwCreateWindow(
        geom.size.width,
        geom.size.height,
        params.appWindowParams.windowTitle.c_str(),
        nullptr,
        nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("HelloImGui failed to create GLFW window");
    }
    params.backendPointers.glfwWindow = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(params.fpsIdling.vsyncToMonitor && params.appWindowParams.vsync ? 1 : 0);
    const bool owns_context = create_imgui_context_if_needed();
    setup_imgui_defaults(params);
    if (params.callbacks.PostInit_AddPlatformBackendCallbacks) {
        params.callbacks.PostInit_AddPlatformBackendCallbacks();
    }
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version(params));

    BackendRuntime runtime;
    runtime.description = "Glfw - OpenGL3";
    runtime.poll_events = [] { glfwPollEvents(); };
    runtime.new_frame = [] {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    };
    runtime.render_draw_data = [window, &params] {
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        const ImVec4 c = params.imGuiWindowParams.backgroundColor;
        glClearColor(c.x, c.y, c.z, c.w == 0.0f ? 1.0f : c.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    };
    runtime.swap_buffers = [window] { glfwSwapBuffers(window); };
    runtime.should_close = [window] { return glfwWindowShouldClose(window) != 0; };
    runtime.shutdown = [window, owns_context, &params] {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        if (owns_context) {
            ImGui::DestroyContext();
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        params.backendPointers.glfwWindow = nullptr;
    };
    return runtime;
}
#endif

#if LTOOL_HAS_IMGUI_BACKEND_SDL2_OPENGL3 && LTOOL_HELLO_IMGUI_HAS_GL_HEADER
inline BackendRuntime make_sdl2_opengl3_backend(RunnerParams& params) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        throw std::runtime_error(std::string("HelloImGui failed to initialize SDL2: ") + SDL_GetError());
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, params.rendererBackendOptions.openGlOptions.MajorVersion.value_or(3));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, params.rendererBackendOptions.openGlOptions.MinorVersion.value_or(0));
    SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if (params.appWindowParams.resizable) {
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_RESIZABLE);
    }
    if (params.appWindowParams.hidden) {
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_HIDDEN);
    }
    SDL_Window* window = SDL_CreateWindow(params.appWindowParams.windowTitle.c_str(),
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          params.appWindowParams.windowGeometry.size.width,
                                          params.appWindowParams.windowGeometry.size.height,
                                          flags);
    if (!window) {
        SDL_Quit();
        throw std::runtime_error(std::string("HelloImGui failed to create SDL2 window: ") + SDL_GetError());
    }
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(params.fpsIdling.vsyncToMonitor && params.appWindowParams.vsync ? 1 : 0);
    params.backendPointers.sdlWindow = window;
    params.backendPointers.sdlGlContext = gl_context;
    const bool owns_context = create_imgui_context_if_needed();
    setup_imgui_defaults(params);
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version(params));

    auto quit = std::make_shared<bool>(false);
    BackendRuntime runtime;
    runtime.description = "Sdl - OpenGL3";
    runtime.poll_events = [window, quit, &params] {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (params.callbacks.AnyBackendEventCallback &&
                params.callbacks.AnyBackendEventCallback(static_cast<void*>(&event))) {
                continue;
            }
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                *quit = true;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.windowID == SDL_GetWindowID(window) &&
                event.window.event == SDL_WINDOWEVENT_CLOSE) {
                *quit = true;
            }
        }
    };
    runtime.new_frame = [window] {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
    };
    runtime.render_draw_data = [window, &params] {
        int display_w = 0;
        int display_h = 0;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        const ImVec4 c = params.imGuiWindowParams.backgroundColor;
        glClearColor(c.x, c.y, c.z, c.w == 0.0f ? 1.0f : c.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    };
    runtime.swap_buffers = [window] { SDL_GL_SwapWindow(window); };
    runtime.should_close = [quit] { return *quit; };
    runtime.shutdown = [window, gl_context, owns_context, &params] {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        if (owns_context) {
            ImGui::DestroyContext();
        }
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        params.backendPointers.sdlWindow = nullptr;
        params.backendPointers.sdlGlContext = nullptr;
    };
    return runtime;
}
#endif

#if LTOOL_HAS_IMGUI_BACKEND_SFML
inline BackendRuntime make_sfml_backend(RunnerParams& params) {
    auto window = std::make_shared<sf::RenderWindow>(
        sf::VideoMode(static_cast<unsigned int>(params.appWindowParams.windowGeometry.size.width),
                      static_cast<unsigned int>(params.appWindowParams.windowGeometry.size.height)),
        params.appWindowParams.windowTitle);
    window->setVerticalSyncEnabled(params.fpsIdling.vsyncToMonitor && params.appWindowParams.vsync);
    params.backendPointers.sfmlWindow = window.get();
    const bool owns_context = create_imgui_context_if_needed();
    if (!ImGui::SFML::Init(*window)) {
        throw std::runtime_error("HelloImGui failed to initialize ImGui-SFML");
    }
    setup_imgui_defaults(params);
    auto clock = std::make_shared<sf::Clock>();
    BackendRuntime runtime;
    runtime.description = "Sfml";
    runtime.poll_events = [window, &params] {
        sf::Event event;
        while (window->pollEvent(event)) {
            ImGui::SFML::ProcessEvent(*window, event);
            if (event.type == sf::Event::Closed) {
                window->close();
            }
        }
    };
    runtime.new_frame = [window, clock] { ImGui::SFML::Update(*window, clock->restart()); };
    runtime.render_draw_data = [window] {
        window->clear();
        ImGui::SFML::Render(*window);
    };
    runtime.swap_buffers = [window] { window->display(); };
    runtime.should_close = [window] { return !window->isOpen(); };
    runtime.shutdown = [window, owns_context, &params] {
        ImGui::SFML::Shutdown(*window);
        if (owns_context) {
            ImGui::DestroyContext();
        }
        params.backendPointers.sfmlWindow = nullptr;
    };
    return runtime;
}
#endif

inline BackendRuntime create_backend(RunnerParams& params) {
    if (params.externalBackendCallbacks.Setup || params.externalBackendCallbacks.NewFrame ||
        params.externalBackendCallbacks.RenderDrawData) {
        return make_external_backend(params);
    }
    if (params.platformBackendType == PlatformBackendType::Null ||
        params.renderingBackendType == RendererBackendType::Null) {
        return make_null_backend(params);
    }
#if LTOOL_HAS_IMGUI_BACKEND_SFML
    if (wants(PlatformBackendType::Sfml, params.platformBackendType) &&
        wants(RendererBackendType::Sfml, params.renderingBackendType)) {
        return make_sfml_backend(params);
    }
#endif
#if LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3 && LTOOL_HELLO_IMGUI_HAS_GL_HEADER
    if (wants(PlatformBackendType::Glfw, params.platformBackendType) &&
        wants(RendererBackendType::OpenGL3, params.renderingBackendType)) {
        return make_glfw_opengl3_backend(params);
    }
#endif
#if LTOOL_HAS_IMGUI_BACKEND_SDL2_OPENGL3 && LTOOL_HELLO_IMGUI_HAS_GL_HEADER
    if (wants(PlatformBackendType::Sdl, params.platformBackendType) &&
        wants(RendererBackendType::OpenGL3, params.renderingBackendType)) {
        return make_sdl2_opengl3_backend(params);
    }
#endif
    if (params.renderingBackendType == RendererBackendType::Vulkan) {
        throw std::runtime_error("HelloImGui Vulkan requires RunnerParams.externalBackendCallbacks in this header-only build");
    }
    if (params.renderingBackendType == RendererBackendType::Metal ||
        params.renderingBackendType == RendererBackendType::DirectX11 ||
        params.renderingBackendType == RendererBackendType::DirectX12 ||
        params.platformBackendType == PlatformBackendType::OpenCv ||
        params.renderingBackendType == RendererBackendType::OpenCv) {
        throw std::runtime_error("Requested HelloImGui backend is detected as an external/header-only integration point; provide RunnerParams.externalBackendCallbacks");
    }
    throw std::runtime_error("No HelloImGui backend is available. Provide GLFW/OpenGL3, SDL2/OpenGL3, ImGui-SFML, or RunnerParams.externalBackendCallbacks.");
}

inline void render_frame(RunnerParams& params, BackendRuntime& backend) {
    if (backend.poll_events) {
        backend.poll_events();
    }
    if (backend.should_close && backend.should_close()) {
        if (!params.callbacks.ConfirmExit || params.callbacks.ConfirmExit()) {
            params.appShallExit = true;
        }
    }
    if (params.callbacks.PreNewFrame) {
        params.callbacks.PreNewFrame();
    }
    if (backend.new_frame) {
        backend.new_frame();
    }
    ImGui::NewFrame();
    if (params.callbacks.PostNewFrame) {
        params.callbacks.PostNewFrame();
    }
    draw_default_gui(params);
    if (params.callbacks.BeforeImGuiRender) {
        params.callbacks.BeforeImGuiRender();
    }
    ImGui::Render();
    if (params.callbacks.CustomBackground) {
        params.callbacks.CustomBackground();
    }
    if (backend.render_draw_data) {
        backend.render_draw_data();
    }
    if (backend.swap_buffers) {
        backend.swap_buffers();
    }
    if (params.callbacks.AfterSwap) {
        params.callbacks.AfterSwap();
    }
}

} // namespace detail

inline RunnerParams* GetRunnerParams() {
    return detail::current_runner_params_ref();
}

inline RunnerParams* CurrentRunnerParams() {
    return GetRunnerParams();
}

inline bool IsUsingHelloImGui() {
    return GetRunnerParams() != nullptr;
}

inline void RequestExit() {
    if (RunnerParams* params = GetRunnerParams()) {
        params->appShallExit = true;
    }
}

inline bool InitGlLoader() {
    return LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3 || LTOOL_HAS_IMGUI_BACKEND_SDL2_OPENGL3;
}

inline float FrameRate(float = 0.5f) {
    return ImGui::GetIO().Framerate;
}

inline ImGuiTestEngine* GetImGuiTestEngine() {
    return nullptr;
}

inline std::string GetBackendDescription() {
    if (detail::current_backend_ref()) {
        return detail::current_backend_ref()->description;
    }
    return "No active backend";
}

inline void ChangeWindowSize(const ScreenSize& windowSize) {
    if (RunnerParams* params = GetRunnerParams()) {
        params->appWindowParams.windowGeometry.size = windowSize;
    }
}

inline void UseWindowFullMonitorWorkArea() {
    if (RunnerParams* params = GetRunnerParams()) {
        params->appWindowParams.windowGeometry.fullScreenMode = FullScreenMode::FullMonitorWorkArea;
    }
}

inline void SwitchLayout(const std::string& layoutName) {
    if (RunnerParams* params = GetRunnerParams()) {
        auto it = std::find_if(params->alternativeDockingLayouts.begin(),
                               params->alternativeDockingLayouts.end(),
                               [&](const DockingParams& layout) { return layout.layoutName == layoutName; });
        if (it != params->alternativeDockingLayouts.end()) {
            params->dockingParams = *it;
        }
    }
}

inline std::string CurrentLayoutName() {
    if (RunnerParams* params = GetRunnerParams()) {
        return params->dockingParams.layoutName;
    }
    return {};
}

inline void AddDockableWindow(const DockableWindow& dockableWindow, bool = false) {
    if (RunnerParams* params = GetRunnerParams()) {
        params->dockingParams.dockableWindows.push_back(dockableWindow);
    }
}

inline void RemoveDockableWindow(const std::string& dockableWindowName) {
    if (RunnerParams* params = GetRunnerParams()) {
        auto& windows = params->dockingParams.dockableWindows;
        windows.erase(std::remove_if(windows.begin(), windows.end(),
                                     [&](const DockableWindow& w) { return w.label == dockableWindowName; }),
                      windows.end());
    }
}

inline void SaveUserPref(const std::string& userPrefName, const std::string& userPrefContent) {
    std::ofstream out(userPrefName.c_str(), std::ios::binary);
    out << userPrefContent;
}

inline std::string LoadUserPref(const std::string& userPrefName) {
    std::ifstream in(userPrefName.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void ShowViewMenu(RunnerParams& runnerParams) {
    for (DockableWindow& window : runnerParams.dockingParams.dockableWindows) {
        if (window.includeInViewMenu) {
            ImGui::MenuItem(window.label.c_str(), nullptr, &window.isVisible);
        }
    }
}

inline void ShowAppMenu(RunnerParams& runnerParams) {
    if (runnerParams.callbacks.ShowAppMenuItems) {
        runnerParams.callbacks.ShowAppMenuItems();
    }
    if (runnerParams.imGuiWindowParams.showMenu_App_Quit && ImGui::MenuItem("Quit")) {
        runnerParams.appShallExit = true;
    }
}

namespace ManualRender {
inline void SetupFromRunnerParams(RunnerParams& runnerParams) {
    if (!runnerParams.callbacks.ShowGui && runnerParams.dockingParams.dockableWindows.empty()) {
        throw std::invalid_argument("HelloImGui::Run needs callbacks.ShowGui or dockable windows");
    }
    detail::current_runner_params_ref() = &runnerParams;
    auto* backend = new detail::BackendRuntime(detail::create_backend(runnerParams));
    detail::current_backend_ref() = backend;
    if (runnerParams.callbacks.PostInit) {
        runnerParams.callbacks.PostInit();
    }
}

inline void SetupFromSimpleRunnerParams(const SimpleRunnerParams& simpleParams) {
    static RunnerParams params;
    params = simpleParams.ToRunnerParams();
    SetupFromRunnerParams(params);
}

inline void SetupFromGuiFunction(const VoidFunction& guiFunction,
                                 const std::string& windowTitle = "",
                                 bool windowSizeAuto = false,
                                 bool windowRestorePreviousGeometry = false,
                                 const ScreenSize& windowSize = DefaultWindowSize,
                                 float fpsIdle = 10.0f,
                                 bool topMost = false) {
    SimpleRunnerParams params;
    params.guiFunction = guiFunction;
    params.windowTitle = windowTitle;
    params.windowSizeAuto = windowSizeAuto;
    params.windowRestorePreviousGeometry = windowRestorePreviousGeometry;
    params.windowSize = windowSize;
    params.fpsIdle = fpsIdle;
    params.topMost = topMost;
    SetupFromSimpleRunnerParams(params);
}

inline void Render() {
    RunnerParams* params = detail::current_runner_params_ref();
    detail::BackendRuntime* backend = detail::current_backend_ref();
    if (!params || !backend) {
        throw std::runtime_error("HelloImGui::ManualRender::Render called before Setup");
    }
    detail::render_frame(*params, *backend);
    if (params->fpsIdling.fpsMax > 0.0f) {
        const auto sleep_us = static_cast<int>(1000000.0f / params->fpsIdling.fpsMax);
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    }
}

inline void TearDown() {
    detail::BackendRuntime* backend = detail::current_backend_ref();
    RunnerParams* params = detail::current_runner_params_ref();
    if (params && params->callbacks.BeforeExit) {
        params->callbacks.BeforeExit();
    }
    if (backend) {
        if (backend->shutdown) {
            backend->shutdown();
        }
        delete backend;
    }
    if (params && params->callbacks.BeforeExit_PostCleanup) {
        params->callbacks.BeforeExit_PostCleanup();
    }
    detail::current_backend_ref() = nullptr;
    detail::current_runner_params_ref() = nullptr;
}
} // namespace ManualRender

inline void Run(RunnerParams& runnerParams) {
    ManualRender::SetupFromRunnerParams(runnerParams);
    while (!runnerParams.appShallExit) {
        ManualRender::Render();
    }
    ManualRender::TearDown();
}

inline void Run(const SimpleRunnerParams& simpleParams) {
    RunnerParams params = simpleParams.ToRunnerParams();
    Run(params);
}

inline void Run(const VoidFunction& guiFunction,
                const std::string& windowTitle = "",
                bool windowSizeAuto = false,
                bool windowRestorePreviousGeometry = false,
                const ScreenSize& windowSize = DefaultWindowSize,
                float fpsIdle = 10.0f,
                bool topMost = false) {
    SimpleRunnerParams params;
    params.guiFunction = guiFunction;
    params.windowTitle = windowTitle;
    params.windowSizeAuto = windowSizeAuto;
    params.windowRestorePreviousGeometry = windowRestorePreviousGeometry;
    params.windowSize = windowSize;
    params.fpsIdle = fpsIdle;
    params.topMost = topMost;
    Run(params);
}

inline std::optional<std::string> IniSettingsLocation(const RunnerParams& runnerParams) {
    if (runnerParams.iniDisable) {
        return std::nullopt;
    }
    std::string filename = runnerParams.iniFilename;
    if (filename.empty() && runnerParams.iniFilename_useAppWindowTitle) {
        filename = runnerParams.appWindowParams.windowTitle.empty() ? "imgui.ini"
                                                                    : runnerParams.appWindowParams.windowTitle + ".ini";
    }
    if (filename.empty()) {
        filename = "imgui.ini";
    }
    return IniFolderLocation(runnerParams.iniFolderType) + "/" + filename;
}

inline bool HasIniSettings(const RunnerParams& runnerParams) {
    auto location = IniSettingsLocation(runnerParams);
    if (!location) {
        return false;
    }
    std::ifstream in(location->c_str(), std::ios::binary);
    return in.good();
}

inline void DeleteIniSettings(const RunnerParams& runnerParams) {
    auto location = IniSettingsLocation(runnerParams);
    if (location) {
        std::remove(location->c_str());
    }
}

inline ImVec2 EmToVec2(float x, float y) {
    const float em = ImGui::GetFontSize();
    return ImVec2(x * em, y * em);
}

inline ImVec2 EmToVec2(ImVec2 v) {
    return EmToVec2(v.x, v.y);
}

inline float EmSize() {
    return ImGui::GetFontSize();
}

inline float EmSize(float nbLines) {
    return ImGui::GetFontSize() * nbLines;
}

inline ImVec2 PixelsToEm(ImVec2 pixels) {
    const float em = std::max(1.0f, ImGui::GetFontSize());
    return ImVec2(pixels.x / em, pixels.y / em);
}

inline float PixelSizeToEm(float pixelSize) {
    return pixelSize / std::max(1.0f, ImGui::GetFontSize());
}

inline DpiAwareParams* GetDpiAwareParams() {
    if (RunnerParams* params = GetRunnerParams()) {
        return &params->dpiAwareParams;
    }
    static DpiAwareParams fallback;
    return &fallback;
}

inline float DpiWindowSizeFactor() {
    return GetDpiAwareParams()->dpiWindowSizeFactor <= 0.0f ? 1.0f : GetDpiAwareParams()->dpiWindowSizeFactor;
}

struct FontLoadingParams {
    bool mergeToLastFont = false;
    bool loadColor = false;
    bool insideAssets = true;
    ImFontConfig fontConfig = ImFontConfig();
};

struct AssetFileData {
    void* data = nullptr;
    std::size_t dataSize = 0;
};

using LoadAssetFileDataFunc = std::function<AssetFileData(const char*)>;

inline LoadAssetFileDataFunc& load_asset_func_ref() {
    static LoadAssetFileDataFunc func;
    return func;
}

inline std::string gAssetsSubfolderFolderName = "assets";

inline void SetAssetsFolder(const std::string& folder) {
    detail::assets_folder_ref() = folder;
}

inline void SetAssetsFolder(const char* folder) {
    SetAssetsFolder(std::string(folder ? folder : ""));
}

inline void overrideAssetsFolder(const char* folder) {
    SetAssetsFolder(folder);
}

inline void AddAssetsSearchPath(const std::string& folder) {
    detail::assets_search_paths_ref().push_back(folder);
}

inline void ClearAssetsSearchPaths() {
    detail::assets_search_paths_ref().clear();
}

inline const std::vector<std::string>& GetAssetsSearchPaths() {
    return detail::assets_search_paths_ref();
}

inline std::string AssetFileFullPath(const std::string& assetRelativeFilename,
                                     bool assertIfNotFound = true) {
    std::vector<std::string> candidates;
    candidates.push_back(detail::assets_folder_ref() + "/" + assetRelativeFilename);
    for (const auto& path : detail::assets_search_paths_ref()) {
        candidates.push_back(path + "/" + assetRelativeFilename);
    }
    candidates.push_back(assetRelativeFilename);
    for (const auto& candidate : candidates) {
        std::ifstream in(candidate.c_str(), std::ios::binary);
        if (in.good()) {
            return candidate;
        }
    }
    if (assertIfNotFound) {
        throw std::runtime_error("HelloImGui asset not found: " + assetRelativeFilename);
    }
    return candidates.front();
}

inline std::string assetFileFullPath(const std::string& assetRelativeFilename,
                                     bool assertIfNotFound = true) {
    return AssetFileFullPath(assetRelativeFilename, assertIfNotFound);
}

inline bool AssetExists(const std::string& assetRelativeFilename) {
    try {
        (void)AssetFileFullPath(assetRelativeFilename, true);
        return true;
    } catch (...) {
        return false;
    }
}

inline AssetFileData DefaultLoadAssetFileData(const char* assetPath) {
    const std::string full = AssetFileFullPath(assetPath ? assetPath : "", true);
    std::ifstream in(full.c_str(), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    AssetFileData result;
    result.dataSize = bytes.size();
    result.data = std::malloc(result.dataSize);
    if (result.data && result.dataSize > 0) {
        std::memcpy(result.data, bytes.data(), result.dataSize);
    }
    return result;
}

inline AssetFileData LoadAssetFileData(const char* assetPath) {
    if (load_asset_func_ref()) {
        return load_asset_func_ref()(assetPath);
    }
    return DefaultLoadAssetFileData(assetPath);
}

inline void FreeAssetFileData(AssetFileData* assetFileData) {
    if (!assetFileData) {
        return;
    }
    std::free(assetFileData->data);
    assetFileData->data = nullptr;
    assetFileData->dataSize = 0;
}

inline void SetLoadAssetFileDataFunction(LoadAssetFileDataFunc func) {
    load_asset_func_ref() = std::move(func);
}

inline ImFont* LoadFont(const std::string& fontFilename,
                        float fontSize,
                        const FontLoadingParams& params = FontLoadingParams()) {
    ImFontConfig config = params.fontConfig;
    config.MergeMode = params.mergeToLastFont;
    if (params.insideAssets) {
        AssetFileData data = LoadAssetFileData(fontFilename.c_str());
        if (!data.data) {
            return nullptr;
        }
        config.FontDataOwnedByAtlas = true;
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data.data, static_cast<int>(data.dataSize), fontSize, &config);
    }
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(fontFilename.c_str(), fontSize, &config);
}

inline ImFont* LoadFontTTF(const std::string& fontFilename,
                           float fontSize,
                           ImFontConfig config = ImFontConfig()) {
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(fontFilename.c_str(), fontSize, &config);
}

inline ImFont* LoadFontTTF_WithFontAwesomeIcons(const std::string& fontFilename,
                                                float fontSize,
                                                ImFontConfig configFont = ImFontConfig()) {
    return LoadFontTTF(fontFilename, fontSize, configFont);
}

inline void ShowThemeTweakGuiWindow(bool* p_open = nullptr) {
    if (p_open && !*p_open) {
        return;
    }
    if (ImGui::Begin("Theme", p_open)) {
        if (RunnerParams* params = GetRunnerParams()) {
            ImGuiTheme::ShowThemeTweakGui(&params->imGuiWindowParams.tweakedTheme);
        }
    }
    ImGui::End();
}

inline void BeginGroupColumn() {
    ImGui::BeginGroup();
}

inline void EndGroupColumn() {
    ImGui::EndGroup();
    ImGui::SameLine();
}

inline ImVec2 WidgetWithResizeHandle(const char* id,
                                     VoidFunction widgetGuiFunction,
                                     float handleSizeEm = 1.0f,
                                     std::optional<VoidFunction> onItemResized = std::nullopt,
                                     std::optional<VoidFunction> onItemHovered = std::nullopt) {
    ImGui::PushID(id);
    if (widgetGuiFunction) {
        widgetGuiFunction();
    }
    ImVec2 size = ImGui::GetItemRectSize();
    ImGui::SameLine();
    if (ImGui::Button("::", EmToVec2(handleSizeEm, handleSizeEm))) {
        if (onItemResized && *onItemResized) {
            (*onItemResized)();
        }
    }
    if (ImGui::IsItemHovered() && onItemHovered && *onItemHovered) {
        (*onItemHovered)();
    }
    ImGui::PopID();
    return size;
}

struct InputTextData {
    std::string Text;
    std::string Hint;
    bool Multiline = false;
    bool Resizable = true;
    ImVec2 SizeEm = ImVec2(0, 0);
    InputTextData(const std::string& text = "", bool multiline = false, ImVec2 size_em = ImVec2(0, 0))
        : Text(text), Multiline(multiline), SizeEm(size_em) {}
};

inline bool InputTextResizable(const char* label, InputTextData* textInput) {
    if (!textInput) {
        return false;
    }
    std::vector<char> buffer(textInput->Text.begin(), textInput->Text.end());
    buffer.resize(std::max<std::size_t>(buffer.size() + 256, 256), '\0');
    const ImVec2 size = EmToVec2(textInput->SizeEm);
    bool changed = textInput->Multiline
                       ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), size)
                       : ImGui::InputTextWithHint(label, textInput->Hint.c_str(), buffer.data(), buffer.size());
    if (changed) {
        textInput->Text = buffer.data();
    }
    return changed;
}

using DictTypeInputTextData = std::map<std::string, std::variant<std::string, bool, float>>;

inline DictTypeInputTextData InputTextDataToDict(const InputTextData& data) {
    return {{"Text", data.Text}, {"Hint", data.Hint}, {"Multiline", data.Multiline},
            {"Resizable", data.Resizable}, {"SizeEmX", data.SizeEm.x}, {"SizeEmY", data.SizeEm.y}};
}

inline InputTextData InputTextDataFromDict(const DictTypeInputTextData& dict) {
    InputTextData data;
    if (auto it = dict.find("Text"); it != dict.end()) data.Text = std::get<std::string>(it->second);
    if (auto it = dict.find("Hint"); it != dict.end()) data.Hint = std::get<std::string>(it->second);
    if (auto it = dict.find("Multiline"); it != dict.end()) data.Multiline = std::get<bool>(it->second);
    if (auto it = dict.find("Resizable"); it != dict.end()) data.Resizable = std::get<bool>(it->second);
    if (auto it = dict.find("SizeEmX"); it != dict.end()) data.SizeEm.x = std::get<float>(it->second);
    if (auto it = dict.find("SizeEmY"); it != dict.end()) data.SizeEm.y = std::get<float>(it->second);
    return data;
}

inline std::string InputTextDataToString(const InputTextData& data) {
    return data.Text;
}

inline InputTextData InputTextDataFromString(const std::string& str) {
    return InputTextData(str);
}

struct ImageBuffer {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> bufferRgb;
};

inline ImageBuffer AppWindowScreenshotRgbBuffer() { return {}; }
inline ImageBuffer FinalAppWindowScreenshotRgbBuffer() { return {}; }
inline float FinalAppWindowScreenshotFramebufferScale() { return 1.0f; }

class TextureGpu {
public:
    int Width = 0;
    int Height = 0;
    virtual ImTextureID TextureID() = 0;
    TextureGpu() = default;
    virtual ~TextureGpu() = default;
    virtual void _impl_StoreTexture(int width, int height, unsigned char*) {
        Width = width;
        Height = height;
    }
};

using TextureGpuPtr = std::shared_ptr<TextureGpu>;

inline TextureGpuPtr CreateTextureGpuFromRgbaData(const unsigned char*, int, int) {
    return {};
}

struct ImageAndSize {
    ImTextureID textureId = ImTextureID(0);
    ImVec2 size = ImVec2(0.0f, 0.0f);
};

struct ImageData {
    unsigned char* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    void Free() {
        std::free(data);
        data = nullptr;
        width = height = channels = 0;
    }
};

inline void ImageFromAsset(const char*, const ImVec2& size = ImVec2(0, 0),
                           const ImVec2& = ImVec2(0, 0),
                           const ImVec2& = ImVec2(1, 1)) {
    ImGui::Dummy(size);
}

inline void ImageFromAssetWithBg(const char*, const ImVec2& size = ImVec2(0, 0),
                                 const ImVec2& = ImVec2(0, 0), const ImVec2& = ImVec2(1, 1),
                                 const ImVec4& = ImVec4(1, 1, 1, 1),
                                 const ImVec4& = ImVec4(0, 0, 0, 0)) {
    ImGui::Dummy(size);
}

inline bool ImageButtonFromAsset(const char* assetPath, const ImVec2& size = ImVec2(0, 0),
                                 const ImVec2& = ImVec2(0, 0), const ImVec2& = ImVec2(1, 1),
                                 int = -1,
                                 const ImVec4& = ImVec4(0, 0, 0, 0),
                                 const ImVec4& = ImVec4(1, 1, 1, 1)) {
    return ImGui::Button(assetPath ? assetPath : "Image", size);
}

inline ImTextureID ImTextureIdFromAsset(const char*) { return ImTextureID(0); }
inline ImVec2 ImageSizeFromAsset(const char*) { return ImVec2(0.0f, 0.0f); }
inline ImageAndSize ImageAndSizeFromAsset(const char*) { return {}; }
inline ImVec2 ImageProportionalSize(const ImVec2& askedSize, const ImVec2& imageSize) {
    if (askedSize.x > 0.0f && askedSize.y > 0.0f) return askedSize;
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) return askedSize;
    if (askedSize.x > 0.0f) return ImVec2(askedSize.x, askedSize.x * imageSize.y / imageSize.x);
    if (askedSize.y > 0.0f) return ImVec2(askedSize.y * imageSize.x / imageSize.y, askedSize.y);
    return imageSize;
}
inline ImageData LoadImageDataFromAsset(const char*, int = 4) { return {}; }
inline ImageData LoadImageDataFromEncodedData(const void*, std::size_t, int = 4) { return {}; }
inline ImageAndSize ImageAndSizeFromEncodedData(const void*, std::size_t, const std::string& = "") { return {}; }
inline void FreeImageCache() {}
namespace internal {
inline void Free_ImageFromAssetMap() {}
}

enum class LogLevel { Debug, Info, Warning, Error };

inline void Log(LogLevel level, char const* const format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    detail::logs_ref().push_back({static_cast<int>(level), buffer});
}

inline void LogClear() {
    detail::logs_ref().clear();
}

inline void LogGui(ImVec2 size = ImVec2(0.0f, 0.0f)) {
    if (ImGui::BeginChild("HelloImGuiLog", size, true)) {
        for (const auto& item : detail::logs_ref()) {
            ImVec4 color = item.first >= static_cast<int>(LogLevel::Error) ? ImVec4(1, 0.25f, 0.25f, 1)
                          : item.first >= static_cast<int>(LogLevel::Warning) ? ImVec4(1, 0.75f, 0.25f, 1)
                                                                               : ImVec4(1, 1, 1, 1);
            ImGui::TextColored(color, "%s", item.second.c_str());
        }
    }
    ImGui::EndChild();
}

namespace Theme {
inline void ApplyOneDark() { ImGuiTheme::ApplyTheme(ImGuiTheme::ImGuiTheme_DarculaDarker); }
inline void ApplyGitHubDark() { ImGuiTheme::ApplyTheme(ImGuiTheme::ImGuiTheme_ImGuiColorsDark); }
inline void ApplyWinUI3Light() { ImGuiTheme::ApplyTheme(ImGuiTheme::ImGuiTheme_LightRounded); }
} // namespace Theme

} // namespace HelloImGui

#endif // LTOOL_HAS_IMGUI

#endif // LTOOL_DETAIL_LHELLO_IMGUI_INCLUDE

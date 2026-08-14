/**
 * @file LProgress.hpp
 * @brief 终端异步进度显示（等待动画/计数/进度条/状态消息）的 ltool 包装。
 *
 * 基于仓库内置 pkgs/barkeep.h 提供异步终端显示：显示在独立线程中刷新，
 * 通过监控已有变量（指针/引用/可调用对象）保持对业务代码的非侵入。
 *
 * 主要接口：
 * - Animation   ：带消息的等待动画，支持多种风格与自定义帧序列。
 * - Status      ：可在线程安全地更新消息的状态显示。
 * - Counter     ：监控一个数值变量的计数显示，可选速度统计。
 * - ProgressBar ：对比 total 计算完成度的进度条。
 * - Composite   ：用 | 或列表把多个显示组合成一行（或换行）输出。
 * - IterableBar ：可直接用于 range-for 的自动进度条。
 *
 * ProgressBar / IterableBar 默认能力：
 * - 宽度自适应：宽度为 0 时按终端列数自动计算条宽，上限为
 *   Config::max_width（默认 60；0 视为默认值）；每次渲染按当前
 *   elapsed/speed 重新测量，运行时窗口大小变化会自动收敛，行宽尽量
 *   贴合终端列数；显式设置 Config::width 可固定条宽。组合显示多个
 *   进度条时建议显式设置 width。
 * - 默认风格：Rich。
 * - 统计信息：默认显示速度（"<it>/s"，单位文本可用 Config::speed_unit
 *   替换）与计时块 `[elapsed - eta]`（mm:ss，Rich 样式下左浅绿右浅蓝）；
 *   用 Config::speed = std::nullopt 可关闭速度统计（ETA 随之不显示），
 *   show_elapsed / show_eta 可单独关闭时间与剩余时间。
 *
 * 与 barkeep 的关系：
 * - 句柄类持有底层 barkeep 显示对象的所有权；最后一个持有者析构时，
 *   底层显示对象自动结束显示线程（RAII），也可显式调用 done() 提前结束。
 * - native() 可访问底层共享指针/对象做高级操作。
 * - 组合显示时各组件需配置 .show = false。
 * - 配置结构体字段与 vendored barkeep（含 ltool 补丁）对齐；升级 barkeep
 *   头文件后需重新应用 pkgs/README_LTOOL.md 记录的补丁并同步字段。
 *
 * 注意事项：
 * - 被监控的进度变量必须比显示对象活得久。
 * - 显示线程会并发读取进度变量；非原子变量存在理论上的数据竞争，
 *   需要严格保证时请使用 std::atomic 或可调用对象。
 * - 高级格式化（Config::format）需要定义 BARKEEP_ENABLE_FMT_FORMAT
 *   或 BARKEEP_ENABLE_STD_FORMAT 编译开关。
 *
 * 常用示例：
 * @code
 * int work{0};
 * LProgress::ProgressBar bar(&work, {.total = 505, .message = "Reading lines"});
 * for (int i = 0; i < 505; i++) { work++; }
 * bar.done();
 * @endcode
 */

#ifndef LTOOL_LPROGRESS_INCLUDE
#define LTOOL_LPROGRESS_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"

#if !LTOOL_HAS_BARKEEP
#error "LProgress requires C++20 and barkeep support"
#endif

#if LTOOL_HAS_BUNDLED_BARKEEP
#include "pkgs/barkeep.h"
#else
#include <barkeep.h>
#endif

#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if LTOOL_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#if defined(BARKEEP_ENABLE_FMT_FORMAT)
#define LPROGRESS_HAS_FMT_FORMAT 1
#else
#define LPROGRESS_HAS_FMT_FORMAT 0
#endif

#if defined(BARKEEP_ENABLE_STD_FORMAT)
#define LPROGRESS_HAS_STD_FORMAT 1
#else
#define LPROGRESS_HAS_STD_FORMAT 0
#endif

namespace LProgress {

/// 描述当前编译环境里可用的 LProgress 能力。
struct Capabilities {
    /// 是否检测到 barkeep 头文件。
    static constexpr bool barkeep = LTOOL_HAS_BARKEEP != 0;
    /// 是否使用 ltool 内置的 barkeep 头文件。
    static constexpr bool bundled = LTOOL_HAS_BUNDLED_BARKEEP != 0;
    /// 是否启用了 barkeep 的 fmt 高级格式化（BARKEEP_ENABLE_FMT_FORMAT）。
    static constexpr bool fmt_format = LPROGRESS_HAS_FMT_FORMAT != 0;
    /// 是否启用了 barkeep 的 std::format 高级格式化（BARKEEP_ENABLE_STD_FORMAT）。
    static constexpr bool std_format = LPROGRESS_HAS_STD_FORMAT != 0;
};

/// 秒为单位的 double 时长。
using Duration = barkeep::Duration;
/// 自定义动画帧序列。
using Strings = barkeep::Strings;
/// 等待动画风格。
using AnimationStyle = barkeep::AnimationStyle;
/// 进度条风格。
using ProgressBarStyle = barkeep::ProgressBarStyle;
/// 自定义进度条样式；fill 需 8 个字符（对应部分填充级别），empty 需与
/// fill 同尺寸或单字符，否则渲染时会断言失败（debug）或未定义行为（release）。
using BarParts = barkeep::BarParts;

/// ANSI 红色转义序列；用于高级格式化串与自定义样式。
inline constexpr const char* red = "\033[31m";
/// ANSI 绿色转义序列。
inline constexpr const char* green = "\033[32m";
/// ANSI 黄色转义序列。
inline constexpr const char* yellow = "\033[33m";
/// ANSI 蓝色转义序列。
inline constexpr const char* blue = "\033[34m";
/// ANSI 品红转义序列。
inline constexpr const char* magenta = "\033[35m";
/// ANSI 青色转义序列。
inline constexpr const char* cyan = "\033[36m";
/// ANSI 颜色重置转义序列。
inline constexpr const char* reset = "\033[0m";

/// 等待动画参数。
struct AnimationConfig {
    std::ostream* out = &std::cout; ///< 输出流。
    std::string message = "";       ///< 动画前显示的消息。
    /// 动画风格（AnimationStyle）或自定义帧序列。
    std::variant<AnimationStyle, Strings> style = AnimationStyle::Ellipsis;
    /// 刷新间隔（Duration 或秒数 double；0 表示按风格默认值）。
    std::variant<Duration, double> interval = Duration{0.};
    bool no_tty = false; ///< 非 TTY 模式：用换行代替回车，默认间隔更长。
    bool show = true;    ///< 构造后立即开始显示。
};

/// 计数显示参数。
struct CounterConfig {
    std::ostream* out = &std::cout; ///< 输出流。
    std::string format = "";        ///< 高级格式串；需启用 barkeep 的格式化开关。
    std::string message = "";       ///< 计数前显示的消息。
    /// 速度平滑因子（0 表示等权平均，1 表示只看最近增量）；
    /// std::nullopt 表示不计算速度。
    std::optional<double> speed = std::nullopt;
    std::string speed_unit = "it/s"; ///< 速度单位文本。
    /// 刷新间隔（Duration 或秒数 double；0 表示默认值）。
    std::variant<Duration, double> interval = Duration{0.};
    bool no_tty = false; ///< 非 TTY 模式。
    bool show = true;    ///< 构造后立即开始显示。
};

/// 进度条参数。
template<class ValueType>
struct ProgressBarConfig {
    std::ostream* out = &std::cout; ///< 输出流。
    ValueType total = ValueType(100); ///< 总工作量；0 表示立即完成。
    std::string format = "";        ///< 高级格式串；需启用 barkeep 的格式化开关。
    std::string message = "";       ///< 进度条前显示的消息。
    /// 速度平滑因子（0 表示等权平均，1 表示只看最近增量）；
    /// 默认 0.5 表示默认开启速度统计，std::nullopt 表示关闭。
    std::optional<double> speed = 0.5;
    /// 速度单位文本（默认 "it/s"，"it" 可替换为任意描述）。
    std::string speed_unit = "it/s";
    /// 进度条风格（ProgressBarStyle）或自定义样式；默认 Rich。
    std::variant<ProgressBarStyle, BarParts> style = ProgressBarStyle::Rich;
    /// 刷新间隔（Duration 或秒数 double；0 表示默认值）。
    std::variant<Duration, double> interval = Duration{0.};
    bool no_tty = false; ///< 非 TTY 模式。
    bool show = true;    ///< 构造后立即开始显示。
    /// 进度条宽度；0 表示按终端宽度自适应（上限为 max_width）。
    std::size_t width = 0;
    /// 自适应模式下的最大条宽；0 视为默认值 60。
    std::size_t max_width = 60;
    /// 显示已运行时间。
    bool show_elapsed = true;
    /// 显示预估剩余时间（依赖速度统计，speed 关闭时不再显示）。
    bool show_eta = true;
};

/// 迭代进度条参数。
struct IterableBarConfig {
    std::ostream* out = &std::cout; ///< 输出流。
    std::string format = "";        ///< 高级格式串；需启用 barkeep 的格式化开关。
    std::string message = "";       ///< 进度条前显示的消息。
    /// 速度平滑因子（0 表示等权平均，1 表示只看最近增量）；
    /// 默认 0.5 表示默认开启速度统计，std::nullopt 表示关闭。
    std::optional<double> speed = 0.5;
    /// 速度单位文本（默认 "it/s"，"it" 可替换为任意描述）。
    std::string speed_unit = "it/s";
    /// 进度条风格；默认 Rich。
    ProgressBarStyle style = ProgressBarStyle::Rich;
    /// 刷新间隔（Duration 或秒数 double；0 表示默认值）。
    std::variant<Duration, double> interval = Duration{0.};
    bool no_tty = false; ///< 非 TTY 模式。
    /// 进度条宽度；0 表示按终端宽度自适应（上限为 max_width）。
    std::size_t width = 0;
    /// 自适应模式下的最大条宽；0 视为默认值 60。
    std::size_t max_width = 60;
    /// 显示已运行时间。
    bool show_elapsed = true;
    /// 显示预估剩余时间（依赖速度统计，speed 关闭时不再显示）。
    bool show_eta = true;
};

namespace detail {

/// 检测 barkeep 配置是否带 ltool 补丁字段（width 等）。
template<class Config, class = void>
struct has_barkeep_extension : std::false_type {};

template<class Config>
struct has_barkeep_extension<
    Config,
    std::void_t<decltype(std::declval<Config>().width)>> : std::true_type {};

/// 返回终端当前列数；无法检测时返回 0。
inline std::size_t terminal_columns() {
#if LTOOL_PLATFORM_WINDOWS
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return static_cast<std::size_t>(csbi.srWindow.Right -
                                        csbi.srWindow.Left + 1);
    }
#else
    struct winsize ws;
    if (::isatty(STDOUT_FILENO) && ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0) {
        return static_cast<std::size_t>(ws.ws_col);
    }
    if (::isatty(STDERR_FILENO) && ::ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0) {
        return static_cast<std::size_t>(ws.ws_col);
    }
#endif
    return 0;
}

/// 返回 total 渲染为文本时的近似长度（浮点按 barkeep 的 2 位小数）。
template<class ValueType>
inline std::size_t total_text_len(ValueType total) {
    std::ostringstream ss;
    if constexpr (std::is_floating_point_v<ValueType>) {
        ss << std::fixed << std::setprecision(2) << total;
    } else {
        ss << total;
    }
    return ss.str().size();
}

/// 速度按 "%.2f" 渲染时的文本长度。
inline std::size_t speed_text_len(double speed) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << std::fabs(speed);
    return ss.str().size();
}

/// 计时按 barkeep "MM:SS" / "H:MM:SS" 渲染时的文本长度。
inline std::size_t duration_text_len(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) { return 5; }
    const auto total_secs = static_cast<long long>(seconds + 0.5);
    const auto hours = total_secs / 3600;
    if (hours == 0) { return 5; } // "MM:SS"
    std::ostringstream ss;
    ss << hours << ":00:00";
    return ss.str().size();
}

/**
 * @brief 解析进度条实际宽度。
 *
 * - width > 0 ：使用显式宽度。
 * - 否则若 no_tty 或列数未知：返回 0（交给 barkeep 默认宽度）。
 * - 否则按列数扣除消息/百分比/计数/速度/统计块（按当前 elapsed 与
 *   speed 的实际渲染长度计算）后取 [min(5, max_width), max_width]
 *   区间内的自适应宽度，使整行尽量贴合终端列数。
 */
inline std::size_t resolve_bar_width(
    std::size_t width, std::size_t max_width, std::size_t columns,
    bool no_tty, std::size_t message_len, std::size_t total_len,
    bool speed_enabled, bool stats_elapsed, bool stats_eta,
    const std::string& speed_unit, std::size_t bar_ends,
    double elapsed_seconds, double current_speed) {
    if (width != 0) { return width; }
    if (no_tty || columns == 0) { return 0; }
    if (max_width == 0) { max_width = 60; } // 0 视为默认上限
    // 固定内容：消息（含尾随空格）、百分比、计数、条后空格、条端、尾部 end。
    std::size_t overhead = message_len + (message_len ? 1 : 0) + 8 +
                           (2 * total_len + 2) + 1 + bar_ends + 1;
    if (speed_enabled) {
        // "(%.2f unit)"：左括号 + 速度文本 + 空格 + 单位 + 右括号。
        overhead += 1 + speed_text_len(current_speed) + 1 + speed_unit.size() + 1;
    }
    if (stats_elapsed || stats_eta) {
        // " [elapsed - eta]"：前导空格 + 左括号 + 已运行 + 分隔 + 剩余 +
        // 右括号；剩余侧预留比已运行多 2 字符（覆盖早期 ETA 更长的情况）。
        const std::size_t el = duration_text_len(elapsed_seconds);
        overhead += 1; // 前导空格
        if (stats_elapsed) { overhead += 1 + el; }
        if (stats_elapsed && stats_eta) { overhead += 3; }
        if (stats_eta) { overhead += el + 2; }
        overhead += 1; // 右括号
    }
    const std::size_t lo = max_width < 5 ? max_width : 5;
    std::size_t avail = columns > overhead ? columns - overhead : lo;
    return avail < lo ? lo : (avail > max_width ? max_width : avail);
}

/// 把 ltool 的动画配置转换为 barkeep 配置。
inline barkeep::AnimationConfig to_barkeep(const AnimationConfig& cfg) {
    barkeep::AnimationConfig out;
    out.out = cfg.out;
    out.message = cfg.message;
    out.style = cfg.style;
    out.interval = cfg.interval;
    out.no_tty = cfg.no_tty;
    out.show = cfg.show;
    return out;
}

/// 把 ltool 的计数配置转换为 barkeep 配置。
inline barkeep::CounterConfig to_barkeep(const CounterConfig& cfg) {
    barkeep::CounterConfig out;
    out.out = cfg.out;
    out.format = cfg.format;
    out.message = cfg.message;
    out.speed = cfg.speed;
    out.speed_unit = cfg.speed_unit;
    out.interval = cfg.interval;
    out.no_tty = cfg.no_tty;
    out.show = cfg.show;
    return out;
}

/// 把 ltool 的进度条配置转换为 barkeep 配置（含自适应宽度）。
template<class ValueType>
inline barkeep::ProgressBarConfig<ValueType> to_barkeep(
    const ProgressBarConfig<ValueType>& cfg) {
    static_assert(
        has_barkeep_extension<barkeep::ProgressBarConfig<ValueType>>::value,
        "vendored barkeep is missing the LTool patch fields (width/"
        "show_elapsed/show_eta); re-apply the changes documented in "
        "ltool/pkgs/README_LTOOL.md or update the vendored pkgs/barkeep.h");
    barkeep::ProgressBarConfig<ValueType> out;
    out.out = cfg.out;
    out.total = cfg.total;
    out.format = cfg.format;
    out.message = cfg.message;
    out.speed = cfg.speed;
    out.speed_unit = cfg.speed_unit;
    out.style = cfg.style;
    out.interval = cfg.interval;
    out.no_tty = cfg.no_tty;
    out.show = cfg.show;
    if (cfg.width != 0) {
        out.width = cfg.width;
    } else {
        // 自适应宽度：每次渲染按当前 elapsed/speed 重新测量终端宽度，
        // 运行时窗口大小变化与计时增长都能在下一次刷新自动收敛。
        // 条端括号数按样式计算（Bars/Blocks 用 | 包裹，Rich/Line 无）。
        const std::size_t bar_ends =
            std::holds_alternative<BarParts>(cfg.style)
                ? std::get<BarParts>(cfg.style).left.size() +
                      std::get<BarParts>(cfg.style).right.size()
                : ((std::get<ProgressBarStyle>(cfg.style) ==
                        ProgressBarStyle::Bars ||
                    std::get<ProgressBarStyle>(cfg.style) ==
                        ProgressBarStyle::Blocks)
                       ? 2
                       : 0);
        out.width_provider =
            [max_width = cfg.max_width, no_tty = cfg.no_tty,
             message_len = cfg.message.size(),
             total_len = detail::total_text_len(cfg.total),
             speed_enabled = cfg.speed.has_value(),
             stats_elapsed = cfg.show_elapsed,
             stats_eta = cfg.show_eta && cfg.speed.has_value(),
             speed_unit = cfg.speed_unit,
             bar_ends](
                Duration elapsed, double current_speed) -> std::size_t {
                return detail::resolve_bar_width(
                    0, max_width, no_tty ? 0 : detail::terminal_columns(),
                    no_tty, message_len, total_len, speed_enabled,
                    stats_elapsed, stats_eta, speed_unit, bar_ends,
                    elapsed.count(), current_speed);
            };
    }
    out.show_elapsed = cfg.show_elapsed;
    out.show_eta = cfg.show_eta;
    return out;
}

/// 把 ltool 的迭代进度条配置转换为 barkeep 配置（含自适应宽度）。
inline barkeep::IterableBarConfig<std::size_t> to_barkeep(
    const IterableBarConfig& cfg, std::size_t total) {
    static_assert(
        has_barkeep_extension<barkeep::IterableBarConfig<std::size_t>>::value,
        "vendored barkeep is missing the LTool patch fields (width/"
        "show_elapsed/show_eta); re-apply the changes documented in "
        "ltool/pkgs/README_LTOOL.md or update the vendored pkgs/barkeep.h");
    barkeep::IterableBarConfig<std::size_t> out;
    out.out = cfg.out;
    out.format = cfg.format;
    out.message = cfg.message;
    out.speed = cfg.speed;
    out.speed_unit = cfg.speed_unit;
    out.style = cfg.style;
    out.interval = cfg.interval;
    out.no_tty = cfg.no_tty;
    if (cfg.width != 0) {
        out.width = cfg.width;
    } else {
        // 自适应宽度：每次渲染按当前 elapsed/speed 重新测量终端宽度。
        const std::size_t bar_ends =
            (cfg.style == ProgressBarStyle::Bars ||
             cfg.style == ProgressBarStyle::Blocks)
                ? 2
                : 0;
        out.width_provider =
            [max_width = cfg.max_width, no_tty = cfg.no_tty,
             message_len = cfg.message.size(), total_len = total_text_len(total),
             speed_enabled = cfg.speed.has_value(),
             stats_elapsed = cfg.show_elapsed,
             stats_eta = cfg.show_eta && cfg.speed.has_value(),
             speed_unit = cfg.speed_unit,
             bar_ends](
                Duration elapsed, double current_speed) -> std::size_t {
                return detail::resolve_bar_width(
                    0, max_width, no_tty ? 0 : detail::terminal_columns(),
                    no_tty, message_len, total_len, speed_enabled,
                    stats_elapsed, stats_eta, speed_unit, bar_ends,
                    elapsed.count(), current_speed);
            };
    }
    out.show_elapsed = cfg.show_elapsed;
    out.show_eta = cfg.show_eta;
    return out;
}

} // namespace detail

/**
 * @brief 所有显示句柄的公共基类。
 *
 * 持有底层 barkeep 显示对象的共享所有权；句柄析构时只释放引用，
 * 最后一个持有者析构时底层显示对象自动结束显示线程。
 * 默认构造的空句柄需要先通过构造函数/赋值持有对象，再调用其余方法。
 */
class Display {
protected:
    std::shared_ptr<barkeep::BaseDisplay> handle_; ///< 底层 barkeep 显示对象。

public:
    using native_type = barkeep::BaseDisplay; ///< 底层显示类型。

    /// 默认构造：空句柄。
    Display() = default;

    /// 从底层共享指针构造（派生类型可隐式转换）。
    explicit Display(std::shared_ptr<barkeep::BaseDisplay> handle)
        : handle_(std::move(handle)) {}

    /// 判断句柄是否持有底层显示对象。
    explicit operator bool() const noexcept {
        return static_cast<bool>(handle_);
    }

    /// 仅启动内部测量（如速度统计）而不显示；随后仍可调用 show() 显示。
    void start() {
        handle_->start();
    }

    /// 启动显示；已在运行时重复调用无效果。
    void show() {
        handle_->show();
    }

    /// 结束显示；未运行时重复调用无效果。
    void done() {
        handle_->done();
    }

    /// 判断显示线程是否正在运行。
    bool running() const {
        return handle_->running();
    }

    /// 返回显示使用的输出流。
    std::ostream& out() const {
        return handle_->out();
    }

    /// 访问底层共享指针的可写引用。
    std::shared_ptr<native_type>& native() noexcept {
        return handle_;
    }

    /// 访问底层共享指针的只读引用。
    const std::shared_ptr<native_type>& native() const noexcept {
        return handle_;
    }
};

/// 等待动画句柄。
class Animation : public Display {
public:
    using Style = AnimationStyle; ///< 动画风格枚举。

    /// 默认构造：空句柄。
    Animation() = default;

    /// 创建并（默认）立即开始显示等待动画。
    explicit Animation(const AnimationConfig& cfg = {})
        : Display(barkeep::Animation(detail::to_barkeep(cfg))) {}
};

/// 状态消息显示句柄（动画 + 可动态更新的消息）。
class Status : public Display {
private:
    std::shared_ptr<barkeep::StatusDisplay> status_; ///< 底层状态显示对象。

public:
    /// 默认构造：空句柄。
    Status() = default;

    /// 创建并（默认）立即开始显示状态消息。
    explicit Status(const AnimationConfig& cfg = {}) {
        status_ = barkeep::Status(detail::to_barkeep(cfg));
        handle_ = status_;
    }

    /// 更新显示的消息（与显示线程同步）。
    void message(const std::string& text) {
        status_->message(text);
    }

    /// 获取当前显示的消息。
    std::string message() const {
        return status_->message();
    }
};

/// 计数显示句柄。
class Counter : public Display {
public:
    /// 默认构造：空句柄。
    Counter() = default;

    /// 用可调用对象（lambda/函数指针/std::function）提供进度值。
    template<class Progress LTOOL_ENABLE_IF(barkeep::ReasonablyInvocableV<Progress>)>
        LTOOL_REQUIRES(barkeep::ReasonablyInvocableV<Progress>)
    Counter(Progress&& progress, const CounterConfig& cfg = {})
        : Display(barkeep::Counter(
              std::function<std::invoke_result_t<std::decay_t<Progress>>()>(
                  std::decay_t<Progress>(std::forward<Progress>(progress))),
              detail::to_barkeep(cfg))) {}

    /// 用指针监控变量（含 std::atomic 指针）；函数指针走可调用对象重载。
    template<class Progress LTOOL_ENABLE_IF(!std::is_function_v<Progress>)>
        LTOOL_REQUIRES(!std::is_function_v<Progress>)
    Counter(Progress* progress, const CounterConfig& cfg = {})
        : Display(barkeep::Counter(progress, detail::to_barkeep(cfg))) {}

    /// 用引用监控变量（等价于传入 &progress）。
    template<class Progress
             LTOOL_ENABLE_IF(!std::is_pointer_v<Progress> &&
                             !barkeep::ReasonablyInvocableV<Progress>)>
        LTOOL_REQUIRES(!std::is_pointer_v<Progress> &&
                       !barkeep::ReasonablyInvocableV<Progress>)
    Counter(Progress& progress, const CounterConfig& cfg = {})
        : Counter(&progress, cfg) {}
};

/// 进度条显示句柄。
class ProgressBar : public Display {
public:
    using Style = ProgressBarStyle; ///< 进度条风格枚举。

    /// 默认构造：空句柄。
    ProgressBar() = default;

    /// 用可调用对象（lambda/函数指针/std::function）提供进度值。
    template<class Progress LTOOL_ENABLE_IF(barkeep::ReasonablyInvocableV<Progress>)>
        LTOOL_REQUIRES(barkeep::ReasonablyInvocableV<Progress>)
    ProgressBar(Progress&& progress,
                const ProgressBarConfig<
                    barkeep::value_t<barkeep::provider_t<Progress>>>& cfg = {})
        : Display(barkeep::ProgressBar(
              std::function<std::invoke_result_t<std::decay_t<Progress>>()>(
                  std::decay_t<Progress>(std::forward<Progress>(progress))),
              detail::to_barkeep(cfg))) {}

    /// 用指针监控变量（含 std::atomic 指针）；函数指针走可调用对象重载。
    template<class Progress LTOOL_ENABLE_IF(!std::is_function_v<Progress>)>
        LTOOL_REQUIRES(!std::is_function_v<Progress>)
    ProgressBar(Progress* progress,
                const ProgressBarConfig<
                    barkeep::value_t<barkeep::provider_t<Progress>>>& cfg = {})
        : Display(barkeep::ProgressBar(progress, detail::to_barkeep(cfg))) {}

    /// 用引用监控变量（等价于传入 &progress）。
    template<class Progress
             LTOOL_ENABLE_IF(!std::is_pointer_v<Progress> &&
                             !barkeep::ReasonablyInvocableV<Progress>)>
        LTOOL_REQUIRES(!std::is_pointer_v<Progress> &&
                       !barkeep::ReasonablyInvocableV<Progress>)
    ProgressBar(Progress& progress,
                const ProgressBarConfig<
                    barkeep::value_t<barkeep::provider_t<Progress>>>& cfg = {})
        : ProgressBar(&progress, cfg) {}
};

/// 组合显示句柄。
class Composite : public Display {
private:
    /// 把 ltool 句柄列表转换为底层共享指针列表；空句柄抛出异常。
    static std::vector<std::shared_ptr<barkeep::BaseDisplay>> collect(
        const std::vector<Display>& displays) {
        std::vector<std::shared_ptr<barkeep::BaseDisplay>> out;
        out.reserve(displays.size());
        for (const auto& display : displays) {
            if (!display) {
                throw std::runtime_error(
                    "LProgress::Composite cannot combine empty display handles");
            }
            out.push_back(display.native());
        }
        return out;
    }

public:
    /// 默认构造：空句柄。
    Composite() = default;

    /// 从多个显示句柄构造；各组件需为未运行状态（.show = false）。
    explicit Composite(const std::vector<Display>& displays,
                       std::string delim = " ")
        : Display(barkeep::Composite(collect(displays), std::move(delim))) {}

    /// 从显示句柄列表构造；各组件需为未运行状态（.show = false）。
    Composite(std::initializer_list<Display> displays, std::string delim = " ")
        : Composite(std::vector<Display>(displays), std::move(delim)) {}

    /// 从两个显示句柄构造；各组件需为未运行状态（.show = false）。
    Composite(const Display& left, const Display& right,
              std::string delim = " ")
        : Composite(std::vector<Display>{left, right}, std::move(delim)) {}
};

/// 用 `|` 组合两个显示句柄（等价于 Composite(left, right)）。
inline Composite operator|(const Display& left, const Display& right) {
    return Composite(left, right);
}

/**
 * @brief 支持 range-for 的自动进度条。
 *
 * 显示在首次 begin() 时启动，迭代越过末尾时结束，因此对象生命周期
 * 可以与循环执行解耦（可提前构造、延后析构）。注意：空容器不触发
 * 结束逻辑，显示线程会一直运行到对象析构；迭代器只能在对象存活
 * 期间使用。
 */
template<class Container>
class IterableBar {
private:
    barkeep::IterableBar<Container> bar_; ///< 底层 barkeep 迭代进度条。

public:
    using native_type = barkeep::IterableBar<Container>; ///< 底层类型。

    /// 用容器和配置构造；显示在首次 begin() 时启动。
    IterableBar(Container& container, const IterableBarConfig& cfg = {})
        : bar_(container, detail::to_barkeep(cfg, container.size())) {}

    /// 返回起始迭代器并启动显示。
    auto begin() {
        return bar_.begin();
    }

    /// 返回终止迭代器。
    auto end() {
        return bar_.end();
    }

    /// 访问底层对象的可写引用。
    native_type& native() noexcept {
        return bar_;
    }

    /// 访问底层对象的只读引用。
    const native_type& native() const noexcept {
        return bar_;
    }
};

} // namespace LProgress

#endif // LTOOL_LPROGRESS_INCLUDE

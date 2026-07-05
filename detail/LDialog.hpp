/**
 * @file detail/LDialog.hpp
 * @brief portable-file-dialogs 的 LTool facade。
 */

#ifndef LTOOL_DETAIL_LDIALOG_INCLUDE
/// detail/LDialog.hpp 的头文件包含保护宏。
#define LTOOL_DETAIL_LDIALOG_INCLUDE

#include "LToolConfig.hpp"

#if !LTOOL_HAS_PORTABLE_FILE_DIALOGS
#error "LDialog requires bundled or external portable-file-dialogs support"
#endif

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#if LTOOL_HAS_BUNDLED_PORTABLE_FILE_DIALOGS
#include "../pkgs/portable-file-dialogs/portable-file-dialogs.h"
#else
#include <portable-file-dialogs.h>
#endif

/// 原生文件、目录、消息和通知对话框辅助命名空间。
namespace LDialog {

/// 描述当前编译环境里可用的 LDialog 能力。
struct Capabilities {
    /// 是否检测到 portable-file-dialogs 头文件。
    static constexpr bool portable_file_dialogs = LTOOL_HAS_PORTABLE_FILE_DIALOGS != 0;
    /// 是否使用 ltool 内置的 portable-file-dialogs 头文件。
    static constexpr bool bundled = LTOOL_HAS_BUNDLED_PORTABLE_FILE_DIALOGS != 0;
};

/// 消息框按钮返回值。
enum class Button {
    /// 用户取消或关闭了对话框。
    Cancel = -1,
    /// 用户点击 OK。
    Ok,
    /// 用户点击 Yes。
    Yes,
    /// 用户点击 No。
    No,
    /// 用户点击 Abort。
    Abort,
    /// 用户点击 Retry。
    Retry,
    /// 用户点击 Ignore。
    Ignore
};

/// 消息框按钮组合。
enum class Choice {
    /// 只显示 OK。
    Ok = 0,
    /// 显示 OK 和 Cancel。
    OkCancel,
    /// 显示 Yes 和 No。
    YesNo,
    /// 显示 Yes、No 和 Cancel。
    YesNoCancel,
    /// 显示 Retry 和 Cancel。
    RetryCancel,
    /// 显示 Abort、Retry 和 Ignore。
    AbortRetryIgnore
};

/// 消息框和通知图标。
enum class Icon {
    /// 信息图标。
    Info = 0,
    /// 警告图标。
    Warning,
    /// 错误图标。
    Error,
    /// 问题图标。
    Question
};

/// 文件选择过滤器，例如 `{"Images", {"*.png", "*.jpg"}}`。
struct FileFilter {
    /// 过滤器显示名称。
    std::string name;
    /// 过滤器模式列表，会以空格合并传给 portable-file-dialogs。
    std::vector<std::string> patterns;

    /// 创建空过滤器。
    FileFilter() = default;

    /// 创建一个单模式过滤器。
    FileFilter(std::string label, std::string pattern)
        : name(std::move(label)), patterns(1, std::move(pattern)) {}

    /// 创建一个多模式过滤器。
    FileFilter(std::string label, std::initializer_list<std::string> values)
        : name(std::move(label)), patterns(values) {}

    /// 创建一个多模式过滤器。
    FileFilter(std::string label, std::vector<std::string> values)
        : name(std::move(label)), patterns(std::move(values)) {}
};

/// 文件选择过滤器列表。
using Filters = std::vector<FileFilter>;

/// 返回默认的全部文件过滤器。
inline Filters all_files() {
    return Filters {FileFilter("All Files", "*")};
}

/// 打开文件对话框参数。
struct OpenFileOptions {
    /// 对话框标题。
    std::string title;
    /// 默认文件或目录路径。
    std::string default_path;
    /// 文件类型过滤器。
    Filters filters;
    /// 是否允许多选。
    bool multiselect;

    /// 创建默认打开文件参数。
    OpenFileOptions()
        : title("Open file"), filters(all_files()), multiselect(false) {}
};

/// 保存文件对话框参数。
struct SaveFileOptions {
    /// 对话框标题。
    std::string title;
    /// 默认文件或目录路径。
    std::string default_path;
    /// 文件类型过滤器。
    Filters filters;
    /// 是否跳过覆盖确认。
    bool force_overwrite;

    /// 创建默认保存文件参数。
    SaveFileOptions()
        : title("Save file"), filters(all_files()), force_overwrite(false) {}
};

/// 选择目录对话框参数。
struct SelectFolderOptions {
    /// 对话框标题。
    std::string title;
    /// 默认目录路径。
    std::string default_path;
    /// Windows 下是否强制使用 default_path 而不是上次打开目录。
    bool force_path;

    /// 创建默认选择目录参数。
    SelectFolderOptions()
        : title("Select folder"), force_path(false) {}
};

/// 消息框参数。
struct MessageOptions {
    /// 对话框标题。
    std::string title;
    /// 消息正文。
    std::string text;
    /// 按钮组合。
    Choice choice;
    /// 图标。
    Icon icon;

    /// 创建默认消息框参数。
    MessageOptions()
        : title("Message"), choice(Choice::OkCancel), icon(Icon::Info) {}
};

/// 通知参数。
struct NotifyOptions {
    /// 通知标题。
    std::string title;
    /// 通知正文。
    std::string text;
    /// 通知图标。
    Icon icon;

    /// 创建默认通知参数。
    NotifyOptions()
        : title("Notification"), icon(Icon::Info) {}
};

namespace detail {

inline std::string join_patterns(const std::vector<std::string>& patterns) {
    if (patterns.empty()) {
        return "*";
    }

    std::string out;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += patterns[i].empty() ? "*" : patterns[i];
    }
    return out;
}

inline std::vector<std::string> to_native_filters(const Filters& filters) {
    const Filters effective = filters.empty() ? all_files() : filters;
    std::vector<std::string> out;
    out.reserve(effective.size() * 2);
    for (const auto& filter : effective) {
        out.push_back(filter.name.empty() ? "Files" : filter.name);
        out.push_back(join_patterns(filter.patterns));
    }
    return out;
}

inline pfd::button to_native(Button value) {
    switch (value) {
    case Button::Cancel:
        return pfd::button::cancel;
    case Button::Ok:
        return pfd::button::ok;
    case Button::Yes:
        return pfd::button::yes;
    case Button::No:
        return pfd::button::no;
    case Button::Abort:
        return pfd::button::abort;
    case Button::Retry:
        return pfd::button::retry;
    case Button::Ignore:
        return pfd::button::ignore;
    }
    return pfd::button::cancel;
}

inline Button from_native(pfd::button value) {
    switch (value) {
    case pfd::button::cancel:
        return Button::Cancel;
    case pfd::button::ok:
        return Button::Ok;
    case pfd::button::yes:
        return Button::Yes;
    case pfd::button::no:
        return Button::No;
    case pfd::button::abort:
        return Button::Abort;
    case pfd::button::retry:
        return Button::Retry;
    case pfd::button::ignore:
        return Button::Ignore;
    }
    return Button::Cancel;
}

inline pfd::choice to_native(Choice value) {
    switch (value) {
    case Choice::Ok:
        return pfd::choice::ok;
    case Choice::OkCancel:
        return pfd::choice::ok_cancel;
    case Choice::YesNo:
        return pfd::choice::yes_no;
    case Choice::YesNoCancel:
        return pfd::choice::yes_no_cancel;
    case Choice::RetryCancel:
        return pfd::choice::retry_cancel;
    case Choice::AbortRetryIgnore:
        return pfd::choice::abort_retry_ignore;
    }
    return pfd::choice::ok_cancel;
}

inline pfd::icon to_native(Icon value) {
    switch (value) {
    case Icon::Info:
        return pfd::icon::info;
    case Icon::Warning:
        return pfd::icon::warning;
    case Icon::Error:
        return pfd::icon::error;
    case Icon::Question:
        return pfd::icon::question;
    }
    return pfd::icon::info;
}

inline pfd::opt open_options(bool multiselect) {
    return multiselect ? pfd::opt::multiselect : pfd::opt::none;
}

inline pfd::opt save_options(bool force_overwrite) {
    return force_overwrite ? pfd::opt::force_overwrite : pfd::opt::none;
}

inline pfd::opt folder_options(bool force_path) {
    return force_path ? pfd::opt::force_path : pfd::opt::none;
}

} // namespace detail

/// 返回当前平台是否能找到可用的原生对话框实现或辅助程序。
inline bool available() {
    return pfd::settings::available();
}

/// 开启或关闭 portable-file-dialogs 的诊断输出。
inline void verbose(bool value) {
    pfd::settings::verbose(value);
}

/// 重新扫描 Linux/BSD 桌面辅助程序。
inline void rescan() {
    pfd::settings::rescan();
}

/// 返回当前用户主目录。
inline std::string home() {
    return pfd::path::home();
}

/// 返回当前平台路径分隔符。
inline std::string separator() {
    return pfd::path::separator();
}

/// 打开文件选择对话框；取消时返回空列表。
inline std::vector<std::string> open_file(const OpenFileOptions& options) {
    return pfd::open_file(options.title,
                          options.default_path,
                          detail::to_native_filters(options.filters),
                          detail::open_options(options.multiselect)).result();
}

/// 打开文件选择对话框；取消时返回空列表。
inline std::vector<std::string> open_file(const std::string& title,
                                          const std::string& default_path = "",
                                          const Filters& filters = all_files(),
                                          bool multiselect = false) {
    OpenFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    options.multiselect = multiselect;
    return open_file(options);
}

/// 打开单文件选择对话框；取消时返回空字符串。
inline std::string open_single_file(const OpenFileOptions& options) {
    OpenFileOptions single = options;
    single.multiselect = false;
    const auto files = open_file(single);
    return files.empty() ? std::string() : files.front();
}

/// 打开单文件选择对话框；取消时返回空字符串。
inline std::string open_single_file(const std::string& title,
                                    const std::string& default_path = "",
                                    const Filters& filters = all_files()) {
    OpenFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    return open_single_file(options);
}

/// 打开保存文件对话框；取消时返回空字符串。
inline std::string save_file(const SaveFileOptions& options) {
    return pfd::save_file(options.title,
                          options.default_path,
                          detail::to_native_filters(options.filters),
                          detail::save_options(options.force_overwrite)).result();
}

/// 打开保存文件对话框；取消时返回空字符串。
inline std::string save_file(const std::string& title,
                             const std::string& default_path = "",
                             const Filters& filters = all_files(),
                             bool force_overwrite = false) {
    SaveFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    options.force_overwrite = force_overwrite;
    return save_file(options);
}

/// 打开目录选择对话框；取消时返回空字符串。
inline std::string select_folder(const SelectFolderOptions& options) {
    return pfd::select_folder(options.title,
                              options.default_path,
                              detail::folder_options(options.force_path)).result();
}

/// 打开目录选择对话框；取消时返回空字符串。
inline std::string select_folder(const std::string& title,
                                 const std::string& default_path = "",
                                 bool force_path = false) {
    SelectFolderOptions options;
    options.title = title;
    options.default_path = default_path;
    options.force_path = force_path;
    return select_folder(options);
}

/// 打开消息框并返回用户选择。
inline Button message(const MessageOptions& options) {
    return detail::from_native(pfd::message(options.title,
                                            options.text,
                                            detail::to_native(options.choice),
                                            detail::to_native(options.icon)).result());
}

/// 打开消息框并返回用户选择。
inline Button message(const std::string& title,
                      const std::string& text,
                      Choice choice = Choice::OkCancel,
                      Icon icon = Icon::Info) {
    MessageOptions options;
    options.title = title;
    options.text = text;
    options.choice = choice;
    options.icon = icon;
    return message(options);
}

/// 打开 Yes/No 确认框；只有 Yes 返回 true。
inline bool confirm(const std::string& title, const std::string& text,
                    Icon icon = Icon::Question) {
    return message(title, text, Choice::YesNo, icon) == Button::Yes;
}

/// 发送桌面通知。
inline void notify(const NotifyOptions& options) {
    pfd::notify(options.title, options.text, detail::to_native(options.icon));
}

/// 发送桌面通知。
inline void notify(const std::string& title,
                   const std::string& text,
                   Icon icon = Icon::Info) {
    NotifyOptions options;
    options.title = title;
    options.text = text;
    options.icon = icon;
    notify(options);
}

/// 原生 portable-file-dialogs 命名空间别名，便于必要时访问底层高级能力。
namespace native = pfd;

} // namespace LDialog

#endif // LTOOL_DETAIL_LDIALOG_INCLUDE

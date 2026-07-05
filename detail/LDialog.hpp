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

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
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

template<class T>
class AsyncResult;

namespace detail {

template<class T>
struct AsyncState {
    std::shared_future<T> future;
    std::function<bool()> kill;
    std::function<bool(int)> native_ready;
};

template<class T>
AsyncResult<T> make_async_result(std::shared_ptr<AsyncState<T> > state);

template<class T>
inline bool future_ready(const std::shared_future<T>& future, int timeout_ms) {
    return future.valid() &&
           future.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::ready;
}

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

/// 异步对话框结果句柄，可查询、等待、取消并获取最终结果。
template<class T>
class AsyncResult {
public:
    /// 创建空异步结果。
    AsyncResult() = default;

    /// 返回当前句柄是否持有异步任务。
    bool valid() const {
        return state_ && state_->future.valid();
    }

    /// 判断异步任务是否已完成。
    bool ready(int timeout_ms = 0) const {
        return valid() && detail::future_ready(state_->future, timeout_ms);
    }

    /// 判断底层原生对话框是否已完成；回调可能尚未执行完。
    bool native_ready(int timeout_ms = 0) const {
        const auto& state = require_state();
        return state.native_ready ? state.native_ready(timeout_ms) : ready(timeout_ms);
    }

    /// 等待异步任务完成。
    void wait() const {
        require_state().future.wait();
    }

    /// 尝试关闭底层对话框。
    bool kill() const {
        const auto& state = require_state();
        return state.kill ? state.kill() : false;
    }

    /// 等待并返回结果副本；后台异常会在这里重新抛出。
    T get() const {
        return require_state().future.get();
    }

    /// 等待并返回结果引用；引用生命周期受本句柄和共享异步状态约束。
    const T& result() const {
        return require_state().future.get();
    }

private:
    template<class U>
    friend AsyncResult<U> detail::make_async_result(std::shared_ptr<detail::AsyncState<U> > state);

    explicit AsyncResult(std::shared_ptr<detail::AsyncState<T> > state)
        : state_(std::move(state)) {}

    const detail::AsyncState<T>& require_state() const {
        if (!valid()) {
            throw std::runtime_error("invalid LDialog async result");
        }
        return *state_;
    }

    std::shared_ptr<detail::AsyncState<T> > state_;
};

/// 异步 void 结果句柄，可查询、等待和取消。
template<>
class AsyncResult<void> {
public:
    /// 创建空异步结果。
    AsyncResult() = default;

    /// 返回当前句柄是否持有异步任务。
    bool valid() const {
        return state_ && state_->future.valid();
    }

    /// 判断异步任务是否已完成。
    bool ready(int timeout_ms = 0) const {
        return valid() && detail::future_ready(state_->future, timeout_ms);
    }

    /// 判断底层原生对话框是否已完成；回调可能尚未执行完。
    bool native_ready(int timeout_ms = 0) const {
        const auto& state = require_state();
        return state.native_ready ? state.native_ready(timeout_ms) : ready(timeout_ms);
    }

    /// 等待异步任务完成。
    void wait() const {
        require_state().future.wait();
    }

    /// 尝试关闭底层对话框。
    bool kill() const {
        const auto& state = require_state();
        return state.kill ? state.kill() : false;
    }

    /// 等待异步任务完成；后台异常会在这里重新抛出。
    void get() const {
        require_state().future.get();
    }

private:
    friend AsyncResult<void> detail::make_async_result(std::shared_ptr<detail::AsyncState<void> > state);

    explicit AsyncResult(std::shared_ptr<detail::AsyncState<void> > state)
        : state_(std::move(state)) {}

    const detail::AsyncState<void>& require_state() const {
        if (!valid()) {
            throw std::runtime_error("invalid LDialog async result");
        }
        return *state_;
    }

    std::shared_ptr<detail::AsyncState<void> > state_;
};

/// 打开文件异步回调类型。
using OpenFileCallback = std::function<void(const std::vector<std::string>&)>;
/// 单路径异步回调类型。
using PathCallback = std::function<void(const std::string&)>;
/// 消息框异步回调类型。
using MessageCallback = std::function<void(Button)>;
/// Yes/No 确认框异步回调类型。
using ConfirmCallback = std::function<void(bool)>;
/// 无返回值异步回调类型。
using VoidCallback = std::function<void()>;

/// 打开文件异步结果类型。
using OpenFileAsync = AsyncResult<std::vector<std::string> >;
/// 单路径异步结果类型。
using PathAsync = AsyncResult<std::string>;
/// 消息框异步结果类型。
using MessageAsync = AsyncResult<Button>;
/// 无返回值异步结果类型。
using VoidAsync = AsyncResult<void>;

namespace detail {

template<class T>
inline AsyncResult<T> make_async_result(std::shared_ptr<AsyncState<T> > state) {
    return AsyncResult<T>(std::move(state));
}

template<>
inline AsyncResult<void> make_async_result(std::shared_ptr<AsyncState<void> > state) {
    return AsyncResult<void>(std::move(state));
}

template<class Result, class NativeDialog>
inline AsyncResult<Result> run_async_dialog(
        const std::shared_ptr<NativeDialog>& native,
        const std::function<Result(NativeDialog&)>& resolver,
        const std::function<void(const Result&)>& callback) {
    auto promise = std::make_shared<std::promise<Result> >();
    auto state = std::make_shared<AsyncState<Result> >();
    state->future = promise->get_future().share();
    state->kill = [native]() {
        return native->kill();
    };
    state->native_ready = [native](int timeout_ms) {
        return native->ready(timeout_ms);
    };

    std::thread([native, resolver, callback, promise]() {
        try {
            Result result = resolver(*native);
            if (callback) {
                callback(result);
            }
            promise->set_value(result);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    }).detach();

    return make_async_result<Result>(state);
}

template<class NativeDialog>
inline AsyncResult<void> run_async_void_dialog(
        const std::shared_ptr<NativeDialog>& native,
        const std::function<void(NativeDialog&)>& waiter,
        const VoidCallback& callback) {
    auto promise = std::make_shared<std::promise<void> >();
    auto state = std::make_shared<AsyncState<void> >();
    state->future = promise->get_future().share();
    state->kill = [native]() {
        return native->kill();
    };
    state->native_ready = [native](int timeout_ms) {
        return native->ready(timeout_ms);
    };

    std::thread([native, waiter, callback, promise]() {
        try {
            waiter(*native);
            if (callback) {
                callback();
            }
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    }).detach();

    return make_async_result(state);
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

/// 异步打开文件选择对话框；完成时可通过返回句柄或回调取得结果。
inline OpenFileAsync open_file_async(const OpenFileOptions& options,
                                     OpenFileCallback callback = OpenFileCallback()) {
    auto native = std::make_shared<pfd::open_file>(
        options.title,
        options.default_path,
        detail::to_native_filters(options.filters),
        detail::open_options(options.multiselect));

    return detail::run_async_dialog<std::vector<std::string> >(
        native,
        std::function<std::vector<std::string>(pfd::open_file&)>(
            [](pfd::open_file& dialog) {
                return dialog.result();
            }),
        callback);
}

/// 异步打开文件选择对话框；完成时可通过返回句柄或回调取得结果。
inline OpenFileAsync open_file_async(const std::string& title,
                                     const std::string& default_path,
                                     const Filters& filters,
                                     bool multiselect,
                                     OpenFileCallback callback = OpenFileCallback()) {
    OpenFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    options.multiselect = multiselect;
    return open_file_async(options, callback);
}

/// 异步打开文件选择对话框；完成时可通过返回句柄或回调取得结果。
inline OpenFileAsync open_file_async(const std::string& title,
                                     const std::string& default_path,
                                     const Filters& filters,
                                     OpenFileCallback callback) {
    OpenFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    return open_file_async(options, callback);
}

/// 异步打开文件选择对话框；完成时可通过返回句柄或回调取得结果。
inline OpenFileAsync open_file_async(const std::string& title,
                                     OpenFileCallback callback) {
    OpenFileOptions options;
    options.title = title;
    return open_file_async(options, callback);
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

/// 异步打开单文件选择对话框；取消时结果为空字符串。
inline PathAsync open_single_file_async(const OpenFileOptions& options,
                                        PathCallback callback = PathCallback()) {
    OpenFileOptions single = options;
    single.multiselect = false;
    auto native = std::make_shared<pfd::open_file>(
        single.title,
        single.default_path,
        detail::to_native_filters(single.filters),
        detail::open_options(false));

    return detail::run_async_dialog<std::string>(
        native,
        std::function<std::string(pfd::open_file&)>(
            [](pfd::open_file& dialog) {
                const auto files = dialog.result();
                return files.empty() ? std::string() : files.front();
            }),
        callback);
}

/// 异步打开单文件选择对话框；取消时结果为空字符串。
inline PathAsync open_single_file_async(const std::string& title,
                                        const std::string& default_path,
                                        const Filters& filters,
                                        PathCallback callback = PathCallback()) {
    OpenFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    return open_single_file_async(options, callback);
}

/// 异步打开单文件选择对话框；取消时结果为空字符串。
inline PathAsync open_single_file_async(const std::string& title,
                                        PathCallback callback) {
    OpenFileOptions options;
    options.title = title;
    return open_single_file_async(options, callback);
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

/// 异步打开保存文件对话框；取消时结果为空字符串。
inline PathAsync save_file_async(const SaveFileOptions& options,
                                 PathCallback callback = PathCallback()) {
    auto native = std::make_shared<pfd::save_file>(
        options.title,
        options.default_path,
        detail::to_native_filters(options.filters),
        detail::save_options(options.force_overwrite));

    return detail::run_async_dialog<std::string>(
        native,
        std::function<std::string(pfd::save_file&)>(
            [](pfd::save_file& dialog) {
                return dialog.result();
            }),
        callback);
}

/// 异步打开保存文件对话框；取消时结果为空字符串。
inline PathAsync save_file_async(const std::string& title,
                                 const std::string& default_path,
                                 const Filters& filters,
                                 bool force_overwrite,
                                 PathCallback callback = PathCallback()) {
    SaveFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    options.force_overwrite = force_overwrite;
    return save_file_async(options, callback);
}

/// 异步打开保存文件对话框；取消时结果为空字符串。
inline PathAsync save_file_async(const std::string& title,
                                 const std::string& default_path,
                                 const Filters& filters,
                                 PathCallback callback) {
    SaveFileOptions options;
    options.title = title;
    options.default_path = default_path;
    options.filters = filters;
    return save_file_async(options, callback);
}

/// 异步打开保存文件对话框；取消时结果为空字符串。
inline PathAsync save_file_async(const std::string& title,
                                 PathCallback callback) {
    SaveFileOptions options;
    options.title = title;
    return save_file_async(options, callback);
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

/// 异步打开目录选择对话框；取消时结果为空字符串。
inline PathAsync select_folder_async(const SelectFolderOptions& options,
                                     PathCallback callback = PathCallback()) {
    auto native = std::make_shared<pfd::select_folder>(
        options.title,
        options.default_path,
        detail::folder_options(options.force_path));

    return detail::run_async_dialog<std::string>(
        native,
        std::function<std::string(pfd::select_folder&)>(
            [](pfd::select_folder& dialog) {
                return dialog.result();
            }),
        callback);
}

/// 异步打开目录选择对话框；取消时结果为空字符串。
inline PathAsync select_folder_async(const std::string& title,
                                     const std::string& default_path,
                                     bool force_path,
                                     PathCallback callback = PathCallback()) {
    SelectFolderOptions options;
    options.title = title;
    options.default_path = default_path;
    options.force_path = force_path;
    return select_folder_async(options, callback);
}

/// 异步打开目录选择对话框；取消时结果为空字符串。
inline PathAsync select_folder_async(const std::string& title,
                                     const std::string& default_path,
                                     PathCallback callback) {
    SelectFolderOptions options;
    options.title = title;
    options.default_path = default_path;
    return select_folder_async(options, callback);
}

/// 异步打开目录选择对话框；取消时结果为空字符串。
inline PathAsync select_folder_async(const std::string& title,
                                     PathCallback callback) {
    SelectFolderOptions options;
    options.title = title;
    return select_folder_async(options, callback);
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

/// 异步打开消息框；完成时可通过返回句柄或回调取得用户选择。
inline MessageAsync message_async(const MessageOptions& options,
                                  MessageCallback callback = MessageCallback()) {
    auto native = std::make_shared<pfd::message>(
        options.title,
        options.text,
        detail::to_native(options.choice),
        detail::to_native(options.icon));

    return detail::run_async_dialog<Button>(
        native,
        std::function<Button(pfd::message&)>(
            [](pfd::message& dialog) {
                return detail::from_native(dialog.result());
            }),
        std::function<void(const Button&)>(
            [callback](const Button& button) {
                if (callback) {
                    callback(button);
                }
            }));
}

/// 异步打开消息框；完成时可通过返回句柄或回调取得用户选择。
inline MessageAsync message_async(const std::string& title,
                                  const std::string& text,
                                  Choice choice,
                                  Icon icon,
                                  MessageCallback callback = MessageCallback()) {
    MessageOptions options;
    options.title = title;
    options.text = text;
    options.choice = choice;
    options.icon = icon;
    return message_async(options, callback);
}

/// 异步打开消息框；完成时可通过返回句柄或回调取得用户选择。
inline MessageAsync message_async(const std::string& title,
                                  const std::string& text,
                                  Choice choice,
                                  MessageCallback callback) {
    return message_async(title, text, choice, Icon::Info, callback);
}

/// 异步打开消息框；完成时可通过返回句柄或回调取得用户选择。
inline MessageAsync message_async(const std::string& title,
                                  const std::string& text,
                                  MessageCallback callback) {
    return message_async(title, text, Choice::OkCancel, Icon::Info, callback);
}

/// 异步打开 Yes/No 确认框；完成时回调参数只有 Yes 为 true。
inline MessageAsync confirm_async(const std::string& title,
                                  const std::string& text,
                                  ConfirmCallback callback = ConfirmCallback()) {
    return message_async(
        title,
        text,
        Choice::YesNo,
        Icon::Question,
        MessageCallback([callback](Button button) {
            if (callback) {
                callback(button == Button::Yes);
            }
        }));
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

/// 异步发送桌面通知并等待底层通知命令完成。
inline VoidAsync notify_async(const NotifyOptions& options,
                              VoidCallback callback = VoidCallback()) {
    auto native = std::make_shared<pfd::notify>(
        options.title,
        options.text,
        detail::to_native(options.icon));

    return detail::run_async_void_dialog<pfd::notify>(
        native,
        std::function<void(pfd::notify&)>(
            [](pfd::notify& dialog) {
                while (!dialog.ready(20)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }),
        callback);
}

/// 异步发送桌面通知并等待底层通知命令完成。
inline VoidAsync notify_async(const std::string& title,
                              const std::string& text,
                              Icon icon = Icon::Info,
                              VoidCallback callback = VoidCallback()) {
    NotifyOptions options;
    options.title = title;
    options.text = text;
    options.icon = icon;
    return notify_async(options, callback);
}

/// 异步发送桌面通知并等待底层通知命令完成。
inline VoidAsync notify_async(const std::string& title,
                              const std::string& text,
                              VoidCallback callback) {
    return notify_async(title, text, Icon::Info, callback);
}

/// 原生 portable-file-dialogs 命名空间别名，便于必要时访问底层高级能力。
namespace native = pfd;

} // namespace LDialog

#endif // LTOOL_DETAIL_LDIALOG_INCLUDE

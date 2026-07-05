# LDialog

`LDialog.hpp` 是 portable-file-dialogs 的轻量 facade，提供原生文件、目录、消息框和桌面通知能力。它是纯头文件封装，默认使用 ltool 内置的 `pkgs/portable-file-dialogs/portable-file-dialogs.h`。

## 常用入口

- `LDialog::available()`：检查当前平台是否有可用的对话框实现或辅助程序。
- `LDialog::open_file(...)`：打开文件选择对话框，取消时返回空列表。
- `LDialog::open_single_file(...)`：打开单文件选择对话框，取消时返回空字符串。
- `LDialog::save_file(...)`：打开保存文件对话框，取消时返回空字符串。
- `LDialog::select_folder(...)`：打开目录选择对话框，取消时返回空字符串。
- `LDialog::message(...)` / `LDialog::confirm(...)`：打开消息框。
- `LDialog::notify(...)`：发送桌面通知。
- `LDialog::*_async(...)`：异步版本，返回 `AsyncResult<T>` 句柄，并可传入回调。

异步句柄支持：

- `valid()`：是否持有任务。
- `ready(timeout_ms)`：异步结果是否已完成。
- `native_ready(timeout_ms)`：底层原生对话框是否已完成。
- `wait()` / `get()` / `result()`：等待并获取结果。
- `kill()`：尝试关闭底层对话框。

回调在后台等待线程里执行；在 ImGui 或其它 UI 代码里使用时，建议只在回调里更新线程安全状态，再由主线程读取并绘制。

## 示例

```cpp
#include "LDialog.hpp"

int main() {
    auto image = LDialog::open_single_file(
        "Open image",
        "",
        {{"Images", {"*.png", "*.jpg", "*.jpeg"}}, {"All Files", "*"}}
    );

    if (!image.empty()) {
        LDialog::message("Selected", image, LDialog::Choice::Ok, LDialog::Icon::Info);
    }
}
```

需要更完整参数时可以使用 options 结构体：

```cpp
LDialog::SaveFileOptions options;
options.title = "Export report";
options.default_path = "report.txt";
options.filters = {{"Text", "*.txt"}, {"All Files", "*"}};
options.force_overwrite = false;

auto path = LDialog::save_file(options);
```

## 异步示例

```cpp
auto handle = LDialog::open_single_file_async(
    "Open config",
    [](const std::string& path) {
        if (!path.empty()) {
            // 回调运行在后台线程；这里适合写入线程安全状态。
        }
    }
);

// 也可以稍后主动查询：
if (handle.ready()) {
    auto path = handle.get();
}
```

确认框可以直接使用 bool 回调：

```cpp
LDialog::confirm_async("Overwrite", "Replace existing file?", [](bool yes) {
    if (yes) {
        // 用户选择 Yes。
    }
});
```

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

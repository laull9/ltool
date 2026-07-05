# LPath

`LPath` 是 `std::filesystem::path` 的薄封装，专注路径、目录遍历和文件系统路径操作。查询类函数通常吞掉 `filesystem_error` 并返回保守结果；变更类函数失败时会抛出异常。

## 常用能力

- 目录操作：`create_directories()`、`files()`、`recursive_files()`、`directories()`、`list()`。
- 路径辅助：`filename()`、`stem()`、`extension()`、`parent_path()`、`absolute()`、`canonical()`、`relative_to()`、`normalized()`。
- 文件系统操作：`copy_to()`、`copy_file_to()`、`move_to()`、`remove()`、`remove_all()`、`permissions()`。
- 临时路径：`temp_directory()`、`temp_path()`。
- 简单通配：`glob()` / `glob_path()` 支持文件名中的 `*` 和 `?`。

## 示例

```cpp
#include "LPath.hpp"

#include <iostream>

void path_example() {
    LPath src = "src";
    for (const auto& cpp : src.recursive_files(".cpp")) {
        std::cout << cpp << "\n";
    }

    auto header = src / "ltool" / "LPath.hpp";
    std::cout << header.normalized() << "\n";
}
```

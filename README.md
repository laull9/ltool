# ltool

`ltool` 是一组 C++ 纯头文件小工具，目标是把日常项目里高频但琐碎的字符串、文件路径和加锁访问写得更顺手。仓库不需要单独编译库文件，直接把需要的头文件加入 include 路径即可使用。

## 特性

- 纯头文件：复制到项目中或通过 `-I` 指向本目录即可。
- 中文文本友好：`lstring` 内部按 UTF-8 字节保存，边界 API 支持 GBK/GB2312、UTF-16、UTF-32、Latin1 等编码转换。
- 常用能力打包：字符串处理、文件读写、目录遍历、路径辅助、Base64、MD5、枚举名转换和线程安全包装。
- 尽量贴近标准库：类型和接口围绕 `std::string`、`std::filesystem::path`、`std::mutex` / `std::shared_mutex` 展开。
- 外部依赖随仓库提供：`fmt/`、`magic_enum/`、`BS_thread_pool.hpp` 已放在仓库内。

## 快速开始

```cpp
#include "lstring.hpp"
#include "lfile.hpp"
#include "locked.hpp"

#include <iostream>
#include <vector>

int main() {
    lstring name = " ltool ";
    auto title = name.trimmed().upper_ascii();

    lfile out = "build/demo.txt";
    out.write_lines(std::vector<lstring>{
        lstring::format("name = {}", title),
        lstring("abc").md5(),
    });

    Locked<int> counter(0);
    counter.with_write([](int& value) {
        ++value;
    });

    std::cout << out.read_text() << "\n";
}
```

编译示例：

```bash
c++ -std=c++20 -I. demo.cpp -o demo -liconv
```

如果平台把 `iconv` 放在 C 标准库里，例如多数 Linux/glibc 环境，可以省略 `-liconv`：

```bash
c++ -std=c++20 -I. demo.cpp -o demo
```

如果只使用 `lstring.hpp`，核心能力可在较低标准下工作；`lfile.hpp` 需要 C++17 `std::filesystem`，`locked.hpp` 使用了 C++20 concepts。

## 头文件一览

### `lstring.hpp`

`lstring` 是 `std::string` 的薄封装。它不试图把字符串变成复杂的 Unicode 容器，内部始终保存 `std::string` 字节；`size()`、`find()`、`substr()`、`split()` 等基础操作保持字节语义。编码转换只在 `from_encoding()` / `to_encoding()` 这类边界函数发生。

常用能力：

- 字符串基础操作：`trimmed()`、`lower_ascii()`、`upper_ascii()`、`split()`、`join()`、`lines()`、`replace_all()`。
- 格式化：内置支持 `fmt::format` 风格的 `lstring::format()` 和 `append_format()`。
- 编码转换：`from_gbk()`、`to_gbk()`、`from_encoding()`、`to_encoding()`、`from_wstring()`、`to_wstring()`。
- 正则：`regex_contains()`、`regex_find()`、`regex_find_all()`、`regex_replace()`；检测到 RE2 时优先使用 RE2，否则使用 `std::regex`。
- 数字转换：C++17 起提供 `to_int()`、`to_i64()`、`to_double()`。
- 枚举辅助：检测到 `magic_enum` 时，可把枚举值转成名称，也可用 `to_enum<T>()` 从文本解析。
- 工具函数：Base64 编解码、MD5、路径名辅助、`std::hash<lstring>`。

示例：

```cpp
#include "lstring.hpp"

enum class Color { Red, Blue };

void string_example() {
    auto parts = lstring(" a, b, c ").trimmed().split(',');
    auto joined = lstring::join(parts, "/");        // "a/ b/ c"

    std::string gbk_bytes = /* 从外部读取的 GBK 字节 */ "";
    auto utf8 = lstring::from_gbk(gbk_bytes);
    auto gbk = utf8.to_gbk();

    auto b64 = lstring("hello").base64_encoded();   // "aGVsbG8="
    auto md5 = lstring("abc").md5();                // "900150983cd24fb0d6963f7d28e17f72"

    auto color = lstring("blue").to_enum<Color>(false);
}
```

### `lfile.hpp`

`lfile` 是 `std::filesystem::path` 的薄封装。对象本身只保存路径，不缓存文件内容；读写函数会在调用时访问文件系统。查询类函数通常吞掉 `filesystem_error` 并返回保守结果，读写和变更类函数失败时会抛出异常。

常用能力：

- 文件读写：`read_bytes()`、`read_text()`、`read_lines()`、`write_bytes()`、`write_text()`、`write_lines()`、`append_text()`。
- 目录操作：`create_directories()`、`files()`、`recursive_files()`、`directories()`、`list()`。
- 路径辅助：`filename()`、`stem()`、`extension()`、`parent_path()`、`absolute()`、`canonical()`、`relative_to()`、`normalized()`。
- 文件系统操作：`touch()`、`copy_to()`、`copy_file_to()`、`move_to()`、`remove()`、`remove_all()`、`permissions()`。
- 临时路径：`temp_directory()`、`temp_file()`、`create_temp_file()`。
- 简单通配：`glob()` / `glob_path()` 支持文件名中的 `*` 和 `?`。

示例：

```cpp
#include "lfile.hpp"

#include <iostream>

void file_example() {
    lfile file = "data/hello.txt";
    file.write_text("你好\n");

    lstring text = file.read_text();                // 默认自动检测输入编码
    auto lines = file.read_lines();

    for (const auto& cpp : lfile::recursive_files(std::filesystem::path("src"), ".cpp")) {
        std::cout << cpp << "\n";
    }

    auto tmp = lfile::create_temp_file("ltool-", ".txt");
    tmp.write_text("temporary text");
}
```

### `locked.hpp`

`Locked<T, Mutex>` 用一个互斥量保护一个对象，适合把“拿锁、访问数据、释放锁”的重复代码集中起来。默认互斥量是 `std::shared_mutex`：读访问使用共享锁，写访问使用独占锁；如果传入的互斥量不支持共享锁，读访问也会退化为独占锁。

常用能力：

- `wlock()`：返回可写 guard，可通过 `*guard` 或 `guard->` 访问数据。
- `rlock()`：返回只读 guard。
- `with_write(fn)`：加写锁后调用函数。
- `with_read(fn)`：加读锁后调用函数。
- `operator()(fn)`：非常短的读写访问语法；非常量对象走写锁，常量对象走读锁。
- `copy()`、`assign()`、`emplace()`：常见值语义操作。

示例：

```cpp
#include "locked.hpp"

#include <string>
#include <vector>

void locked_example() {
    Locked<std::vector<std::string>> messages;

    messages.with_write([](auto& list) {
        list.push_back("hello");
    });

    auto size = messages.with_read([](const auto& list) {
        return list.size();
    });

    auto guard = messages.wlock();
    guard->push_back("world");
}
```

## 外部依赖与开关

仓库包含以下第三方头文件，默认直接从本仓库引用：

- `fmt/`：`lstring` 格式化能力依赖它，默认以 header-only 方式使用。
- `magic_enum/`：C++17 起用于枚举名和枚举值互转。
- `BS_thread_pool.hpp`：线程池头文件，当前 README 只列出依赖，具体接口请参考该头文件本身。

可用宏：

- `LSTRING_USE_EXTERNAL_FMT=1`：改为包含系统或包管理器提供的 `<fmt/...>`。
- `LSTRING_USE_MAGIC_ENUM=0`：关闭 `magic_enum` 相关能力。

GBK/GB2312 转换在 Windows 上使用系统代码页 API；在 Unix-like 平台上使用 `iconv`。如果平台没有可用 `iconv`，相关转换函数会抛出异常；如果链接时报 `iconv`、`iconv_open` 或 `iconv_close` 未定义，请在编译命令中追加 `-liconv`。

## 编码约定

`lstring` 的设计重点是清晰的边界：

- 内部存储：始终是 `std::string` 字节，通常按 UTF-8 文本理解。
- 基础字符串操作：按字节或 ASCII 分隔符处理，不自动按 Unicode 码点切分。
- 文件文本读写：`lfile` 通过 `lstring` 的编码 API 把外部字节转换为内部 UTF-8。
- 严格模式：大多数编码转换默认 `strict = true`，遇到非法字节会抛异常；设置为 `false` 时会尽量用替换字符继续。

## 目录结构

```text
.
├── lstring.hpp
├── lfile.hpp
├── locked.hpp
├── BS_thread_pool.hpp
├── fmt/
└── magic_enum/
```

## 建议用法

- 小项目可以直接复制需要的 `.hpp` 和对应依赖目录。
- 大项目建议把本仓库作为子模块或 vendored 目录，并通过 `target_include_directories()` 暴露 include 路径。
- 如果项目已有自己的 `fmt` 版本，建议定义 `LSTRING_USE_EXTERNAL_FMT=1`，避免重复携带多份 fmt。

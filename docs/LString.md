# LString

`LString` 是 `std::string` 的薄封装。它不试图把字符串变成复杂的 Unicode 容器，内部始终保存 `std::string` 字节；`size()`、`find()`、`substr()`、`split()` 等基础操作保持字节语义。编码转换只在 `from_encoding()` / `to_encoding()` 这类边界函数发生。

## 常用能力

- 字符串基础操作：`trimmed()`、`lower_ascii()`、`upper_ascii()`、`split()`、`join()`、`lines()`、`replace_all()`。
- 格式化：内置支持 `fmt::format` 风格的 `LString::format()` 和 `append_format()`。
- 编码转换：`from_gbk()`、`to_gbk()`、`from_encoding()`、`to_encoding()`、`from_wstring()`、`to_wstring()`。
- 正则：`regex_contains()`、`regex_find()`、`regex_find_all()`、`regex_replace()`；检测到 RE2 时优先使用 RE2，否则使用 `std::regex`。
- 数字转换：C++17 起提供 `to_int()`、`to_i64()`、`to_double()`。
- 枚举辅助：检测到 `magic_enum` 时，可把枚举值转成名称，也可用 `to_enum<T>()` 从文本解析。
- 工具函数：Base64 编解码、MD5、SHA-256、路径名辅助、`std::hash<LString>`。

## 编码约定

- 内部存储：始终是 `std::string` 字节，通常按 UTF-8 文本理解。
- 基础字符串操作：按字节或 ASCII 分隔符处理，不自动按 Unicode 码点切分。
- 文件文本读写：`LFile` 通过 `LString` 的编码 API 把外部字节转换为内部 UTF-8。
- 严格模式：大多数编码转换默认 `strict = true`，遇到非法字节会抛异常；设置为 `false` 时会尽量用替换字符继续。

GBK/GB2312 转换在 Windows 上使用系统代码页 API；在 Unix-like 平台上使用 `iconv`。如果平台没有可用 `iconv`，相关转换函数会抛出异常；如果链接时报 `iconv`、`iconv_open` 或 `iconv_close` 未定义，请在编译命令中追加 `-liconv`。

## 示例

```cpp
#include "LString.hpp"

enum class Color { Red, Blue };

void string_example() {
    auto parts = LString(" a, b, c ").trimmed().split(',');
    auto joined = LString::join(parts, "/");        // "a/ b/ c"

    std::string gbk_bytes = /* 从外部读取的 GBK 字节 */ "";
    auto utf8 = LString::from_gbk(gbk_bytes);
    auto gbk = utf8.to_gbk();

    auto b64 = LString("hello").base64_encoded();   // "aGVsbG8="
    auto md5 = LString("abc").md5();                // "900150983cd24fb0d6963f7d28e17f72"
    auto sha = LString("abc").sha256();             // "ba7816bf..."

    auto color = LString("blue").to_enum<Color>(false);
}
```

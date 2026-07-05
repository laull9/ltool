# LFile

`LFile` 保存一个 `LPath`，但职责集中在普通文件内容读写。路径拼接、目录枚举、glob、路径规范化等能力由 `LPath` 提供。

## 常用能力

- 文件读写：`read_bytes()`、`read_text()`、`read_lines()`、`write_bytes()`、`write_text()`、`write_lines()`、`append_text()`。
- 内容查找替换：`contains()`、`find()`、`replace_all()`、`regex_contains()`、`regex_find_all()`、`regex_replace()`；原始字节可用 `bytes_find()`、`replace_bytes_all()`。
- 流和随机访问：`open_input()`、`open_output()`、`open_stream()`、`read_bytes_at()`、`read_bytes_from()`、`write_bytes_at()`、`seekg()`、`seekp()`、`tellg()`、`tellp()`。
- 文件操作：`touch()`、`copy_to()`、`move_to()`、`remove()`。
- 哈希：`path_hash()` 按路径求标准库哈希，`sha256()` 按文件内容返回 SHA-256 十六进制摘要。
- 临时文件：`create_temp_file()`。

## 示例

```cpp
#include "LFile.hpp"

void file_example() {
    LFile file = "data/hello.txt";
    file.write_text("你好\n");

    LString text = file.read_text();                // 默认自动检测输入编码
    auto lines = file.read_lines();

    auto tmp = LFile::create_temp_file("ltool-", ".txt");
    tmp.write_text("temporary text");
    auto digest = tmp.sha256();
}
```

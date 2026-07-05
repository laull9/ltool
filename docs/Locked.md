# Locked

`Locked<T, Mutex>` 用一个互斥量保护一个对象，适合把“拿锁、访问数据、释放锁”的重复代码集中起来。默认互斥量是 `std::shared_mutex`：读访问使用共享锁，写访问使用独占锁；如果传入的互斥量不支持共享锁，读访问也会退化为独占锁。

## 常用能力

- `wlock()`：返回可写 guard，可通过 `*guard` 或 `guard->` 访问数据。
- `rlock()`：返回只读 guard。
- `with_write(fn)`：加写锁后调用函数。
- `with_read(fn)`：加读锁后调用函数。
- `operator()(fn)`：非常短的读写访问语法；非常量对象走写锁，常量对象走读锁。
- `copy()`、`assign()`、`emplace()`：常见值语义操作。

## 示例

```cpp
#include "Locked.hpp"

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

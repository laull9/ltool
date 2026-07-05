# LRandom

`LRandom` 是 `std::random` 的轻量封装，默认使用 `std::mt19937_64`。对象可以用固定种子构造，适合测试、模拟和可复现实验；也可以使用 `LRandom::shared()` 或 `LRandom::rand_*()` 静态快捷函数获取线程局部默认实例，少写样板代码。需要标准库分布器时，可以通过 `engine()` 直接访问底层引擎。

## 常用能力

- 数值随机：`integer()`、`real()`、`normal()`、`exponential()`、`boolean()`、`chance()`、`roll()`。
- 容器随机：`choice()`、`weighted_choice()`、`shuffle()`、`shuffled()`、`sample()`。
- 字节和文本：`bytes()`、`fill_bytes()`、`string()`、`hex()`、`uuid_v4()`、`uuid_v7()`。
- 安全 UUID：`safe_uuid_v4()`、`safe_uuid_v7()` 使用操作系统安全随机源；`uuid_v4()`、`uuid_v7()` 使用当前 `LRandom` 引擎，适合可复现流程。
- 无上下文快捷函数：`rand_int()`、`rand_real()`、`rand_choice()`、`rand_shuffle()`、`rand_string()`、`rand_uuid_v4()`、`rand_uuid_v7()` 等；它们使用 `thread_local` 默认实例，多线程调用时不会共享同一个随机引擎。

## 示例

```cpp
#include "LRandom.hpp"

#include <vector>

void random_example() {
    LRandom rng(42);

    auto dice = rng.roll();
    auto score = rng.real(0.0, 100.0);
    auto token = rng.string(12);

    std::vector<int> values {1, 2, 3, 4};
    rng.shuffle(values);
    auto one = rng.choice(values);

    auto id = LRandom::shared().uuid_v4();
    auto quick = LRandom::rand_int(1, 100);
    auto quick_one = LRandom::rand_choice(values);
    auto quick_id = LRandom::rand_uuid_v7();
    auto safe_id = LRandom::safe_uuid_v4();
    auto safe_time_id = LRandom::safe_uuid_v7();
}
```

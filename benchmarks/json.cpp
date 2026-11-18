// 在 Linux/glibc 上测量 arena 的存活分配，以及只读遍历的平均耗时。
#include "LJson.hpp"
#include <chrono>
#include <iostream>
#include <malloc.h>
#include <string>

static std::size_t live_bytes() {
    const auto info = mallinfo2();
    return info.uordblks + info.hblkhd;
}

int main() {
    auto root = LJson::parse(R"({"n":0,"s":""})");
    const auto initial = live_bytes();
    for (int i = 0; i < 1000000; ++i) root["n"] = i;
    const auto after_numbers = live_bytes();
    for (int i = 0; i < 100000; ++i) root["s"] = std::string(256, char('a' + i % 26));
    const auto after_strings = live_bytes();
    root.compact();
    const auto after_compact = live_bytes();
    if (root["n"].as_int() != 999999 || root["s"].as_string().size() != 256) return 1;
    std::cout << "live_bytes initial/scalars/strings/compact: " << initial << ' '
              << after_numbers << ' ' << after_strings << ' ' << after_compact << '\n';
    using Clock = std::chrono::steady_clock;
    constexpr int repeats = 16;
    std::cout << "depth view_us snapshot_us\n";
    for (int depth : {64, 128, 256, 512}) {
        const auto tree = LJson::parse(std::string(depth, '[') + "1" + std::string(depth, ']'));
        int visits = 0;
        const auto visit = [&](const LJson::Json& node) { visits += node.exists(); };
        const auto start = Clock::now();
        for (int i = 0; i < repeats; ++i) tree.for_each_view(visit);
        const auto middle = Clock::now();
        for (int i = 0; i < repeats; ++i) tree.for_each_recursive(visit);
        const auto end = Clock::now();
        using Microseconds = std::chrono::duration<double, std::micro>;
        if (visits != 2 * repeats * (depth + 1)) return 1;
        std::cout << depth << ' ' << Microseconds(middle - start).count() / repeats
                  << ' ' << Microseconds(end - middle).count() / repeats << '\n';
    }
}

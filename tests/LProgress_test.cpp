#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "LProgress.hpp"

namespace {

/// 短暂等待，让显示线程有机会渲染一次。
inline void wait_a_moment() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

/// 无参函数，用于验证函数指针走可调用对象重载。
inline int progress_fn() {
    return 42;
}

/// 去掉 ANSI CSI 转义序列（\033[...m），便于检查纯文本内容。
inline std::string strip_ansi(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && text[i] != 'm') {
                ++i;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

TEST_CASE("LProgress exposes capabilities and aliases", "[LProgress]") {
    REQUIRE(LProgress::Capabilities::barkeep);
    REQUIRE(LProgress::Capabilities::bundled);
    static_assert(std::is_same_v<LProgress::AnimationStyle,
                                 barkeep::AnimationStyle>);
    static_assert(std::is_same_v<LProgress::ProgressBarStyle,
                                 barkeep::ProgressBarStyle>);
    static_assert(std::is_same_v<LProgress::Duration, barkeep::Duration>);
}

TEST_CASE("LProgress::Animation constructs with style and custom stills",
          "[LProgress]") {
    std::ostringstream ss;
    {
        LProgress::Animation anim({.out = &ss, .message = "Working",
                                   .style = LProgress::AnimationStyle::Earth,
                                   .interval = 0.001, .no_tty = true});
        REQUIRE(anim.running());
        wait_a_moment();
        anim.done();
        REQUIRE_FALSE(anim.running());
    }
    REQUIRE(!ss.str().empty());

    {
        LProgress::Animation anim({.style = LProgress::Strings{".", "..", "..."},
                                   .show = false});
        REQUIRE_FALSE(anim.running());
    }
}

TEST_CASE("LProgress::Status updates message", "[LProgress]") {
    std::ostringstream ss;
    {
        LProgress::Status status({.out = &ss, .message = "Working",
                                  .interval = 0.001, .no_tty = true,
                                  .show = false});
        REQUIRE_FALSE(status.running());
        status.show();
        REQUIRE(status.running());
        status.message("Still working");
        wait_a_moment();
        REQUIRE(status.message() == "Still working");
        status.done();
        REQUIRE_FALSE(status.running());
    }
    REQUIRE(!ss.str().empty());
}

TEST_CASE("LProgress::Counter monitors pointer, reference and lambda",
          "[LProgress]") {
    std::ostringstream ss;

    {
        std::atomic<int> work{0};
        LProgress::Counter counter(&work, {.out = &ss, .message = "Lines",
                                           .interval = 0.001, .no_tty = true,
                                           .show = false});
        counter.show();
        for (int i = 0; i < 100; i++) {
            work++;
            wait_a_moment();
        }
        counter.done();
    }

    {
        int work = 0;
        LProgress::Counter counter(work, {.show = false});
        REQUIRE_FALSE(counter.running());
    }

    {
        int n = 0;
        std::ostringstream out;
        auto progress = [&n] { return n; };
        LProgress::Counter counter(progress, {.out = &out, .interval = 0.001,
                                              .no_tty = true, .show = false});
        counter.show();
        n += 5;
        wait_a_moment();
        counter.done();
        REQUIRE(!out.str().empty());
    }
}

TEST_CASE("LProgress::Counter accepts function pointers via callable overload",
          "[LProgress]") {
    LProgress::Counter counter(&progress_fn, {.show = false});
    REQUIRE(static_cast<bool>(counter));
    REQUIRE_FALSE(counter.running());
}

TEST_CASE("LProgress::ProgressBar monitors pointer, reference and lambda",
          "[LProgress]") {
    std::ostringstream ss;

    {
        std::atomic<int> work{0};
        LProgress::ProgressBar bar(&work,
                                   {.out = &ss, .total = 505,
                                    .message = "Reading", .interval = 0.001,
                                    .no_tty = true, .show = false});
        REQUIRE_FALSE(bar.running());
        bar.show();
        REQUIRE(bar.running());
        for (int i = 0; i < 505; i++) {
            work++;
            if (i % 10 == 0) {
                wait_a_moment();
            }
        }
        bar.done();
        REQUIRE_FALSE(bar.running());
    }
    REQUIRE(!ss.str().empty());

    {
        int work = 0;
        LProgress::ProgressBar bar(work, {.total = 10, .show = false});
        REQUIRE_FALSE(bar.running());
    }

    {
        std::ostringstream out;
        int width = 0, height = 0;
        auto area = [&] { return width * height; };
        LProgress::ProgressBar bar(area, {.out = &out, .total = 10000,
                                          .interval = 0.001, .no_tty = true,
                                          .show = false});
        bar.show();
        width = 50;
        height = 50;
        wait_a_moment();
        bar.done();
        REQUIRE(!out.str().empty());
    }
}

TEST_CASE("LProgress::start() measures without showing", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> work{0};
    LProgress::Counter counter(&work, {.out = &ss, .interval = 0.001,
                                       .no_tty = true, .show = false});
    counter.start();
    REQUIRE_FALSE(counter.running());
    work += 3;
    wait_a_moment();
    counter.show();
    REQUIRE(counter.running());
    counter.done();
    REQUIRE_FALSE(counter.running());
}

TEST_CASE("LProgress::Composite combines displays with |", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> sents{0}, toks{0};

    {
        LProgress::ProgressBar bar(&sents,
                                   {.out = &ss, .total = 50, .message = "Sents",
                                    .interval = 0.001, .no_tty = true,
                                    .show = false});
        LProgress::Counter counter(&toks, {.out = &ss, .message = "Toks",
                                           .interval = 0.001, .no_tty = true,
                                           .show = false});
        auto composite = bar | counter;
        REQUIRE_FALSE(composite.running());
        composite.show();
        REQUIRE(composite.running());
        for (int i = 0; i < 50; i++) {
            sents++;
            toks += 2;
            wait_a_moment();
        }
        composite.done();
        REQUIRE_FALSE(composite.running());
    }
    REQUIRE(!ss.str().empty());
}

TEST_CASE("LProgress::Composite from a list with delimiter", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> a{0}, b{0}, c{0};

    {
        LProgress::ProgressBar p1(&a, {.out = &ss, .total = 10,
                                       .interval = 0.001, .no_tty = true,
                                       .show = false});
        LProgress::ProgressBar p2(&b, {.out = &ss, .total = 10,
                                       .interval = 0.001, .no_tty = true,
                                       .show = false});
        LProgress::ProgressBar p3(&c, {.out = &ss, .total = 10,
                                       .interval = 0.001, .no_tty = true,
                                       .show = false});
        LProgress::Composite bars({p1, p2, p3}, "\n");
        bars.show();
        for (int i = 0; i < 10; i++) {
            a++;
            b += 2;
            c += 3;
            wait_a_moment();
        }
        bars.done();
    }
    REQUIRE(!ss.str().empty());
}

TEST_CASE("LProgress::Composite accepts a single-argument list", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> a{0}, b{0};
    LProgress::ProgressBar p1(&a, {.out = &ss, .total = 10, .interval = 0.001,
                                   .no_tty = true, .show = false});
    LProgress::ProgressBar p2(&b, {.out = &ss, .total = 10, .interval = 0.001,
                                   .no_tty = true, .show = false});
    LProgress::Composite bars({p1, p2});
    REQUIRE(static_cast<bool>(bars));
    bars.show();
    wait_a_moment();
    bars.done();
}

TEST_CASE("LProgress::Composite rejects empty handles", "[LProgress]") {
    LProgress::Counter empty_a, empty_b;
    REQUIRE_THROWS_AS(LProgress::Composite({empty_a, empty_b}),
                      std::runtime_error);
    REQUIRE_THROWS_AS(empty_a | empty_b, std::runtime_error);
}

TEST_CASE("LProgress handles outlive the composite display", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> a{0}, b{0};

    LProgress::ProgressBar bar(&a, {.out = &ss, .total = 10, .interval = 0.001,
                                    .no_tty = true, .show = false});
    LProgress::Counter counter(&b, {.out = &ss, .interval = 0.001,
                                    .no_tty = true, .show = false});
    {
        auto composite = bar | counter;
        composite.show();
        a += 5;
        b += 5;
        wait_a_moment();
        composite.done();
    }
    // 组合显示结束后，子句柄仍持有对象但显示线程已停止。
    REQUIRE_FALSE(bar.running());
    REQUIRE_FALSE(counter.running());

    // 子句柄先于组合析构时，组合仍可正常工作。
    std::shared_ptr<LProgress::Composite> composite;
    {
        LProgress::ProgressBar p(&a, {.out = &ss, .total = 10,
                                      .interval = 0.001, .no_tty = true,
                                      .show = false});
        composite = std::make_shared<LProgress::Composite>(
            std::vector<LProgress::Display>{p});
    }
    REQUIRE(static_cast<bool>(*composite));
    composite->show();
    wait_a_moment();
    composite->done();
}

TEST_CASE("LProgress::IterableBar iterates a container", "[LProgress]") {
    std::ostringstream ss;
    std::vector<int> v(30, 1);
    long sum = 0;

    {
        LProgress::IterableBar bar(v, {.out = &ss, .message = "Summing",
                                       .interval = 0.001, .no_tty = true});
        for (auto x : bar) {
            sum += x;
            wait_a_moment();
        }
    }
    REQUIRE(sum == 30);
    REQUIRE(!ss.str().empty());
}

TEST_CASE("LProgress resolves adaptive bar width", "[LProgress][width]") {
    using LProgress::detail::resolve_bar_width;
    // 显式宽度优先于一切。
    REQUIRE(resolve_bar_width(20, 60, 200, false, 5, 3, true, true, true,
                              "it/s", 2, 50.0, 2.18) == 20);
    // 非 TTY 或列数未知：返回 0（交给 barkeep 默认宽度）。
    REQUIRE(resolve_bar_width(0, 60, 0, true, 5, 3, true, true, true, "it/s",
                              2, 50.0, 2.18) == 0);
    REQUIRE(resolve_bar_width(0, 60, 0, false, 5, 3, true, true, true, "it/s",
                              2, 50.0, 2.18) == 0);
    // 自适应：列数充足时不超过 max_width，且不低于下限。
    auto w = resolve_bar_width(0, 60, 200, false, 5, 3, true, true, true,
                               "it/s", 2, 50.0, 2.18);
    REQUIRE(w <= 60);
    REQUIRE(w >= 5);
    // 窄终端时回落下限而不是 0。
    auto w2 = resolve_bar_width(0, 60, 20, false, 5, 3, true, true, true,
                                "it/s", 2, 50.0, 2.18);
    REQUIRE(w2 >= 5);
    // max_width 很小时固定为 max_width。
    REQUIRE(resolve_bar_width(0, 3, 200, false, 0, 1, false, false, false, "",
                              0, 50.0, 0.0) == 3);
    // max_width 为 0 时视为默认值 60。
    REQUIRE(resolve_bar_width(0, 0, 200, false, 0, 1, false, false, false, "",
                              0, 50.0, 0.0) <= 60);
    REQUIRE(resolve_bar_width(0, 0, 200, false, 0, 1, false, false, false, "",
                              0, 50.0, 0.0) >= 5);
    // 关闭统计块时预留更少，条宽更大。
    const auto with_stats =
        resolve_bar_width(0, 60, 80, false, 5, 3, true, true, true, "it/s",
                          0, 50.0, 2.18);
    const auto without_stats =
        resolve_bar_width(0, 60, 80, false, 5, 3, true, false, false, "it/s",
                          0, 50.0, 2.18);
    REQUIRE(without_stats > with_stats);
}

TEST_CASE("LProgress adaptive width fits the terminal precisely",
          "[LProgress][width]") {
    using LProgress::detail::resolve_bar_width;
    // 80 列、无消息、Rich（条端 0）、速度 "(2.18 it/s)"、计时 "MM:SS"：
    // 固定内容 18 + 速度 11 + 计时块 18 = 47，条宽应为 80 - 47 = 33。
    REQUIRE(resolve_bar_width(0, 60, 80, false, 0, 3, true, true, true,
                              "it/s", 0, 50.0, 2.18) == 33);
    // 运行超过 1 小时：计时变 "H:MM:SS"，条宽相应变窄（80 - 51 = 29）。
    REQUIRE(resolve_bar_width(0, 60, 80, false, 0, 3, true, true, true,
                              "it/s", 0, 4000.0, 2.18) == 29);
    // 高速：速度文本变长，条宽变窄（"12345.67" 8 字符，80 - 51 = 29）。
    REQUIRE(resolve_bar_width(0, 60, 80, false, 0, 3, true, true, true,
                              "it/s", 0, 50.0, 12345.67) == 29);
    // 只显示已运行时间：计时块更短，条宽更大。
    REQUIRE(resolve_bar_width(0, 60, 80, false, 0, 3, true, true, false,
                              "it/s", 0, 50.0, 2.18) > 33);
    // Bars 风格（条端 2 字符括号）时条宽相应减 2。
    REQUIRE(resolve_bar_width(0, 60, 80, false, 0, 3, true, true, true,
                              "it/s", 2, 50.0, 2.18) == 31);
}

TEST_CASE("LProgress::ProgressBar shows stats by default", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> work{0};
    {
        LProgress::ProgressBar bar(&work, {.out = &ss, .total = 50,
                                           .interval = 0.001, .no_tty = true,
                                           .show = false});
        bar.show();
        for (int i = 0; i < 50; i++) {
            work++;
            wait_a_moment();
        }
        bar.done();
    }
    const auto text = strip_ansi(ss.str());
    REQUIRE(text.find("it/s") != std::string::npos); // 速度
    // 计时块 `[mm:ss - mm:ss]`：含括号与分隔符，不再使用 elapsed/ETA 单词。
    REQUIRE(text.find("[") != std::string::npos);
    REQUIRE(text.find("]") != std::string::npos);
    REQUIRE(text.find(" - ") != std::string::npos);
    REQUIRE(text.find("elapsed") == std::string::npos);
    REQUIRE(text.find("ETA") == std::string::npos);
}

TEST_CASE("LProgress::ProgressBar honors custom speed unit", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> work{0};
    {
        LProgress::ProgressBar bar(&work, {.out = &ss, .total = 50,
                                           .speed_unit = "line/s",
                                           .interval = 0.001, .no_tty = true,
                                           .show = false});
        bar.show();
        for (int i = 0; i < 50; i++) {
            work++;
            wait_a_moment();
        }
        bar.done();
    }
    REQUIRE(ss.str().find("line/s") != std::string::npos);
}

TEST_CASE("LProgress::ProgressBar honors explicit width", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> work{0};
    {
        // 自定义单字节样式，便于精确计量条内宽度。
        LProgress::ProgressBar bar(
            &work, {.out = &ss, .total = 10,
                    .style = LProgress::BarParts{"[", "]", {"#"}, {"."}},
                    .interval = 0.001, .no_tty = true, .show = false,
                    .width = 10});
        bar.show();
        work = 10;
        wait_a_moment();
        bar.done();
    }
    // 条内字符数应等于 width，与渲染状态（填充/空）无关。
    const auto text = ss.str();
    const auto left = text.find('[');
    REQUIRE(left != std::string::npos);
    const auto right = text.find(']', left + 1);
    REQUIRE(right != std::string::npos);
    REQUIRE(right - left - 1 == 10);
}

TEST_CASE("LProgress::ProgressBar stats can be disabled", "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> work{0};
    {
        LProgress::ProgressBar bar(
            &work, {.out = &ss, .total = 50, .speed = std::nullopt,
                    .interval = 0.001, .no_tty = true, .show = false,
                    .show_elapsed = false, .show_eta = false});
        bar.show();
        for (int i = 0; i < 50; i++) {
            work++;
            wait_a_moment();
        }
        bar.done();
    }
    const auto text = strip_ansi(ss.str());
    // 统计完全关闭：不渲染计时块。
    REQUIRE(text.find("[") == std::string::npos);
    REQUIRE(text.find(" - ") == std::string::npos);
}

TEST_CASE("LProgress::ProgressBar hides ETA when speed is disabled",
          "[LProgress]") {
    std::ostringstream ss;
    std::atomic<int> work{0};
    {
        // 只关速度：elapsed 保留，ETA 依赖速度故不再显示。
        LProgress::ProgressBar bar(&work, {.out = &ss, .total = 50,
                                           .speed = std::nullopt,
                                           .interval = 0.001, .no_tty = true,
                                           .show = false});
        bar.show();
        for (int i = 0; i < 50; i++) {
            work++;
            wait_a_moment();
        }
        bar.done();
    }
    const auto text = strip_ansi(ss.str());
    // 只有 elapsed：计时块为 `[mm:ss]`，无 " - " 分隔符。
    REQUIRE(text.find("[") != std::string::npos);
    REQUIRE(text.find(" - ") == std::string::npos);
}

TEST_CASE("LProgress::ProgressBar re-adapts width at runtime", "[LProgress][width]") {
    std::ostringstream ss;
    std::atomic<int> work{0};
    // 模拟终端宽度在 10 与 30 之间来回变化：provider 每次渲染交替返回。
    std::size_t calls = 0;
    {
        barkeep::ProgressBarConfig<int> cfg;
        cfg.out = &ss;
        cfg.total = 100;
        cfg.width = 0;
        cfg.no_tty = true;
        cfg.interval = barkeep::Duration{0.001};
        cfg.style = barkeep::BarParts{"[", "]", {"#"}, {"."}};
        cfg.width_provider = [&calls](barkeep::Duration, double) -> std::size_t {
            return (calls++ % 2 == 0) ? 10 : 30;
        };
        auto bar = barkeep::ProgressBar(&work, cfg);
        bar->show();
        for (int i = 0; i < 100; i++) {
            work++;
            wait_a_moment();
        }
        bar->done();
    }
    // 每次渲染都重新测量宽度。
    REQUIRE(calls >= 2);
    // 输出中应同时出现 10 与 30 宽的条。
    bool saw_10 = false, saw_30 = false;
    std::istringstream lines(ss.str());
    std::string line;
    while (std::getline(lines, line)) {
        const auto left = line.find('[');
        if (left == std::string::npos) { continue; }
        const auto right = line.find(']', left + 1);
        if (right == std::string::npos) { continue; }
        const auto width = right - left - 1;
        if (width == 10) { saw_10 = true; }
        if (width == 30) { saw_30 = true; }
    }
    REQUIRE(saw_10);
    REQUIRE(saw_30);
}

TEST_CASE("LProgress::Display exposes native access and bool conversion",
          "[LProgress]") {
    int work = 0;
    LProgress::ProgressBar bar(&work, {.total = 10, .show = false});
    REQUIRE(static_cast<bool>(bar));
    REQUIRE(bar.native() != nullptr);
    REQUIRE(&bar.out() == &std::cout);

    LProgress::ProgressBar empty;
    REQUIRE_FALSE(static_cast<bool>(empty));
}

TEST_CASE("LProgress empty handles throw logic_error on member calls",
          "[LProgress]") {
    // 默认构造的句柄为空，调用任何依赖底层对象的成员都应抛 std::logic_error，
    // 而不是空指针解引用（UB）。这些用例不启动显示线程。
    LProgress::Animation anim;
    REQUIRE_FALSE(static_cast<bool>(anim));
    REQUIRE_THROWS_AS(anim.start(), std::logic_error);
    REQUIRE_THROWS_AS(anim.show(), std::logic_error);
    REQUIRE_THROWS_AS(anim.done(), std::logic_error);
    REQUIRE_THROWS_AS(anim.running(), std::logic_error);
    REQUIRE_THROWS_AS(anim.out(), std::logic_error);

    LProgress::Status status;
    REQUIRE_THROWS_AS(status.message("hi"), std::logic_error);
    REQUIRE_THROWS_AS(status.message(), std::logic_error);

    LProgress::Counter counter;
    REQUIRE_THROWS_AS(counter.show(), std::logic_error);
    REQUIRE_THROWS_AS(counter.done(), std::logic_error);

    LProgress::ProgressBar bar;
    REQUIRE_THROWS_AS(bar.start(), std::logic_error);
    REQUIRE_THROWS_AS(bar.show(), std::logic_error);
    REQUIRE_THROWS_AS(bar.running(), std::logic_error);
    REQUIRE_THROWS_AS(bar.out(), std::logic_error);

    LProgress::Composite composite;
    REQUIRE_THROWS_AS(composite.show(), std::logic_error);
    REQUIRE_THROWS_AS(composite.done(), std::logic_error);

    // 低层访问器保持原行为：operator bool 返回句柄是否有效，
    // native() 返回底层共享指针（空句柄下为空 shared_ptr，不抛异常）。
    REQUIRE_FALSE(static_cast<bool>(anim));
    REQUIRE(anim.native() == nullptr);
    REQUIRE_FALSE(static_cast<bool>(composite));
    REQUIRE(composite.native() == nullptr);
}

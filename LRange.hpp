/**
 * @file LRange.hpp
 * @brief 基于 range-v3 的现代 range/view 工具命名空间。
 *
 * LRange 的核心定位：
 * - 直接装载 range-v3 常用 view/action，保持管道式写法。
 * - 补充 LTool 风格的收集、查询和聚合小工具。
 * - 默认惰性处理；只有 to_vector()/to_set()/sum() 等终端函数会真正遍历。
 *
 * 常用示例：
 * @code
 * std::vector<int> values {1, 2, 3, 4, 5};
 *
 * auto squares = LRange::range(1, 6)
 *              | LRange::filter([](int x) { return x % 2 == 1; })
 *              | LRange::transform([](int x) { return x * x; })
 *              | LRange::to_vector();
 *
 * for (auto [index, value] : LRange::indexed(values, 1)) {
 *     fmt::println("{} -> {}", index, value);
 * }
 * @endcode
 */

#ifndef LRANGE_INCLUDE
#define LRANGE_INCLUDE

#include "detail/LToolConfig.hpp"

#if !LTOOL_HAS_RANGE_V3
#error "LRange requires range-v3. Add the range-v3 package/include path before including LRange.hpp."
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <range/v3/all.hpp>

namespace LRange {

namespace views = ranges::views;
namespace actions = ranges::actions;
namespace algorithms = ranges;

using ranges::begin;
using ranges::end;
using ranges::range_reference_t;
using ranges::range_value_t;

template<class Container>
class ContainerRange;

template<class Container, class Range>
Container to(Range&& range);

template<class T, class Range>
std::vector<T> to_vector(Range&& range);

template<class Range>
auto to_vector(Range&& range);

template<class T, class Range>
std::set<T> to_set(Range&& range);

template<class Range>
auto to_set(Range&& range);

template<class T, class Range>
std::unordered_set<T> to_unordered_set(Range&& range);

template<class Range>
auto to_unordered_set(Range&& range);

template<class Key, class Value, class Range>
std::map<Key, Value> to_map(Range&& range);

template<class Key, class Value, class Range>
std::unordered_map<Key, Value> to_unordered_map(Range&& range);

namespace Detail {

/// 范围迭代器的哨兵类型（标记结束）。
template<class T>
class RangeSentinel;

/**
 * @brief 算术范围的自定义迭代器；处理正向/负向步长与溢出安全。
 */
template<class T>
class RangeIterator {
public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using reference = T;
    using pointer = void;

    /// 默认构造（无意义迭代器）。
    RangeIterator() = default;

    /// 用当前值、终点和步长构造迭代器。
    RangeIterator(T current, T stop, T step)
        : current_(current), stop_(stop), step_(step) {}

    /// 解引用返回当前值。
    T operator*() const {
        return current_;
    }

    /// 前进步长；越过终点时收敛到 stop_ 防止溢出。
    RangeIterator& operator++() {
        if (step_ > T {}) {
            if (current_ >= stop_ || current_ > std::numeric_limits<T>::max() - step_) {
                current_ = stop_;
            } else {
                const T next = current_ + step_;
                current_ = next <= current_ || next >= stop_ ? stop_ : next;
            }
        } else {
            if (current_ <= stop_ || current_ < std::numeric_limits<T>::lowest() - step_) {
                current_ = stop_;
            } else {
                const T next = current_ + step_;
                current_ = next >= current_ || next <= stop_ ? stop_ : next;
            }
        }
        return *this;
    }

    /// 后缀自增。
    RangeIterator operator++(int) {
        auto copy = *this;
        ++(*this);
        return copy;
    }

    /// 与哨兵比较是否结束。
    friend bool operator==(const RangeIterator& it, RangeSentinel<T>) {
        return it.done();
    }

    /// 与哨兵比较是否结束（反向）。
    friend bool operator==(RangeSentinel<T> sentinel, const RangeIterator& it) {
        return it == sentinel;
    }

    /// 与哨兵比较是否未结束。
    friend bool operator!=(const RangeIterator& it, RangeSentinel<T> sentinel) {
        return !(it == sentinel);
    }

    /// 与哨兵比较是否未结束（反向）。
    friend bool operator!=(RangeSentinel<T> sentinel, const RangeIterator& it) {
        return !(it == sentinel);
    }

private:
    /// 判断是否已越过终点。
    bool done() const {
        return step_ > T {} ? !(current_ < stop_) : !(current_ > stop_);
    }

    T current_ {}; ///< 当前值。
    T stop_ {};    ///< 终点（不含）。
    T step_ {1};   ///< 步长。
};

/// 范围视图的结束哨兵。
template<class T>
class RangeSentinel {};

/**
 * @brief 算术步进范围视图：range(start, stop, step)。
 *
 * 只对算术类型开放；step 为零或非有限浮点步长会抛异常。
 */
template<class T>
class RangeView : public ranges::view_base {
    static_assert(std::is_arithmetic_v<T>, "LRange::range requires arithmetic values");

public:
    using value_type = T;                ///< 元素类型。
    using iterator = RangeIterator<T>;   ///< 迭代器类型。
    using sentinel = RangeSentinel<T>;   ///< 哨兵类型。

    /// 默认构造空范围。
    RangeView() = default;

    /// 构造 [start, stop) 步长 step 的范围；step 必须非零且有限。
    RangeView(T start, T stop, T step)
        : start_(start), stop_(stop), step_(step) {
        if (step_ == T {}) {
            throw std::invalid_argument("LRange::range step cannot be zero");
        }
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(step_)) {
                throw std::invalid_argument("LRange::range step must be finite");
            }
        }
    }

    /// 起始迭代器。
    iterator begin() const {
        return iterator(start_, stop_, step_);
    }

    /// 结束哨兵。
    sentinel end() const {
        return {};
    }

private:
    T start_ {}; ///< 起始值（含）。
    T stop_ {};  ///< 终点值（不含）。
    T step_ {1}; ///< 步长。
};

/// 管道式 to<Container>() 闭包。
template<class Container>
struct ToClosure {
    /// 把 range 收集进 Container。
    template<class Range>
    friend Container operator|(Range&& range, ToClosure) {
        return LRange::to<Container>(std::forward<Range>(range));
    }
};

/// 管道式 to_vector() 闭包。
template<class T>
struct ToVectorClosure {
    /// 把 range 收集进 vector。
    template<class Range>
    friend auto operator|(Range&& range, ToVectorClosure) {
        if constexpr (std::is_void_v<T>) {
            return LRange::to_vector(std::forward<Range>(range));
        } else {
            return LRange::to_vector<T>(std::forward<Range>(range));
        }
    }
};

/// 管道式 to_set() 闭包。
template<class T>
struct ToSetClosure {
    /// 把 range 收集进 set。
    template<class Range>
    friend auto operator|(Range&& range, ToSetClosure) {
        if constexpr (std::is_void_v<T>) {
            return LRange::to_set(std::forward<Range>(range));
        } else {
            return LRange::to_set<T>(std::forward<Range>(range));
        }
    }
};

/// 管道式 to_unordered_set() 闭包。
template<class T>
struct ToUnorderedSetClosure {
    /// 把 range 收集进 unordered_set。
    template<class Range>
    friend auto operator|(Range&& range, ToUnorderedSetClosure) {
        if constexpr (std::is_void_v<T>) {
            return LRange::to_unordered_set(std::forward<Range>(range));
        } else {
            return LRange::to_unordered_set<T>(std::forward<Range>(range));
        }
    }
};

/// 管道式 to_map() 闭包。
template<class Key, class Value>
struct ToMapClosure {
    /// 把 pair range 收集进 map。
    template<class Range>
    friend auto operator|(Range&& range, ToMapClosure) {
        return LRange::to_map<Key, Value>(std::forward<Range>(range));
    }
};

/// 管道式 to_unordered_map() 闭包。
template<class Key, class Value>
struct ToUnorderedMapClosure {
    /// 把 pair range 收集进 unordered_map。
    template<class Range>
    friend auto operator|(Range&& range, ToUnorderedMapClosure) {
        return LRange::to_unordered_map<Key, Value>(std::forward<Range>(range));
    }
};

} // namespace Detail

/**
 * @brief Owns a standard container while also modeling a range-v3 view.
 *
 * range-v3 intentionally rejects piping temporary containers into lazy views,
 * because saving the view could otherwise dangle. This wrapper gives ltool APIs
 * a way to return owned, vector-like results that are safe to pipe as
 * temporaries: the view stores the container by value.
 */
template<class Container>
class ContainerRange : public ranges::view_base {
private:
    Container container_; ///< 持有的容器。

public:
    using container_type = Container;     ///< 容器类型。
    using value_type = typename Container::value_type;       ///< 元素类型。
    using size_type = typename Container::size_type;         ///< 大小类型。
    using difference_type = typename Container::difference_type; ///< 差值类型。
    using reference = typename Container::reference;         ///< 引用类型。
    using const_reference = typename Container::const_reference; ///< 只读引用类型。
    using iterator = typename Container::iterator;           ///< 迭代器类型。
    using const_iterator = typename Container::const_iterator;   ///< 只读迭代器类型。

    /// 默认构造空容器范围。
    ContainerRange() = default;

    /// 复制容器构造。
    ContainerRange(const Container& container)
        : container_(container) {}

    /// 移动容器构造。
    ContainerRange(Container&& container)
        : container_(std::move(container)) {}

    /// 返回内部容器引用。
    Container& container() & noexcept {
        return container_;
    }

    /// 返回内部容器只读引用。
    const Container& container() const& noexcept {
        return container_;
    }

    /// 移出内部容器。
    Container&& container() && noexcept {
        return std::move(container_);
    }

    /// 隐式转换为容器引用。
    operator Container&() & noexcept {
        return container_;
    }

    /// 隐式转换为容器只读引用。
    operator const Container&() const& noexcept {
        return container_;
    }

    /// 隐式移出容器。
    operator Container() && noexcept {
        return std::move(container_);
    }

    /// 起始可写迭代器。
    iterator begin() noexcept {
        return container_.begin();
    }

    /// 起始只读迭代器。
    const_iterator begin() const noexcept {
        return container_.begin();
    }

    /// cbegin()。
    const_iterator cbegin() const noexcept {
        return container_.cbegin();
    }

    /// 末尾可写迭代器。
    iterator end() noexcept {
        return container_.end();
    }

    /// 末尾只读迭代器。
    const_iterator end() const noexcept {
        return container_.end();
    }

    /// cend()。
    const_iterator cend() const noexcept {
        return container_.cend();
    }

    /// 判断是否为空。
    bool empty() const noexcept {
        return container_.empty();
    }

    /// 返回元素数量。
    size_type size() const noexcept {
        return container_.size();
    }

    /// 下标访问（可写）。
    reference operator[](size_type pos) {
        return container_[pos];
    }

    /// 下标访问（只读）。
    const_reference operator[](size_type pos) const {
        return container_[pos];
    }

    /// 首元素引用。
    reference front() {
        return container_.front();
    }

    /// 首元素只读引用。
    const_reference front() const {
        return container_.front();
    }

    /// 尾元素引用。
    reference back() {
        return container_.back();
    }

    /// 尾元素只读引用。
    const_reference back() const {
        return container_.back();
    }

    /// 删除指定位置元素。
    iterator erase(const_iterator pos) {
        return container_.erase(pos);
    }

    /// 删除区间 [first, last) 内元素。
    iterator erase(const_iterator first, const_iterator last) {
        return container_.erase(first, last);
    }
};

/// 常用 range-v3 惰性 view 别名（可按管道式组合）。
inline constexpr auto all = ranges::views::all;
inline constexpr auto cache1 = ranges::views::cache1;
inline constexpr auto chunk = ranges::views::chunk;
inline constexpr auto concat = ranges::views::concat;
inline constexpr auto cycle = ranges::views::cycle;
inline constexpr auto drop = ranges::views::drop;
inline constexpr auto drop_while = ranges::views::drop_while;
inline constexpr auto filter = ranges::views::filter;
inline constexpr auto generate = ranges::views::generate;
inline constexpr auto generate_n = ranges::views::generate_n;
inline constexpr auto group_by = ranges::views::group_by;
inline constexpr auto iota = ranges::views::iota;
inline constexpr auto ints = ranges::views::ints;
inline constexpr auto join = ranges::views::join;
inline constexpr auto keys = ranges::views::keys;
inline constexpr auto partial_sum = ranges::views::partial_sum;
inline constexpr auto repeat = ranges::views::repeat;
inline constexpr auto repeat_n = ranges::views::repeat_n;
inline constexpr auto replace = ranges::views::replace;
inline constexpr auto replace_if = ranges::views::replace_if;
inline constexpr auto reverse = ranges::views::reverse;
inline constexpr auto single = ranges::views::single;
inline constexpr auto slice = ranges::views::slice;
inline constexpr auto sliding = ranges::views::sliding;
inline constexpr auto split = ranges::views::split;
inline constexpr auto split_when = ranges::views::split_when;
inline constexpr auto stride = ranges::views::stride;
inline constexpr auto tail = ranges::views::tail;
inline constexpr auto take = ranges::views::take;
inline constexpr auto take_while = ranges::views::take_while;
inline constexpr auto transform = ranges::views::transform;
inline constexpr auto trim = ranges::views::trim;
inline constexpr auto unique = ranges::views::unique;
inline constexpr auto values = ranges::views::values;
inline constexpr auto zip = ranges::views::zip;
inline constexpr auto zip_with = ranges::views::zip_with;

/// 常用 range-v3 急切 action 别名（修改容器本身）。
inline constexpr auto action_drop = ranges::actions::drop;
inline constexpr auto action_drop_while = ranges::actions::drop_while;
inline constexpr auto action_join = ranges::actions::join;
inline constexpr auto action_reverse = ranges::actions::reverse;
inline constexpr auto action_shuffle = ranges::actions::shuffle;
inline constexpr auto action_sort = ranges::actions::sort;
inline constexpr auto action_stable_sort = ranges::actions::stable_sort;
inline constexpr auto action_stride = ranges::actions::stride;
inline constexpr auto action_take = ranges::actions::take;
inline constexpr auto action_take_while = ranges::actions::take_while;
inline constexpr auto action_unique = ranges::actions::unique;

/// 构造 [0, stop) 的步进范围。
template<class Stop>
auto range(Stop stop) {
    using value_type = std::decay_t<Stop>;
    return Detail::RangeView<value_type>(value_type {}, static_cast<value_type>(stop), value_type {1});
}

/// 构造 [start, stop) 的步进范围（步长 1）。
template<class Start, class Stop>
auto range(Start start, Stop stop) {
    using value_type = std::common_type_t<std::decay_t<Start>, std::decay_t<Stop>>;
    return Detail::RangeView<value_type>(
        static_cast<value_type>(start),
        static_cast<value_type>(stop),
        value_type {1});
}

/// 构造 [start, stop) 指定步长的范围；无符号类型不接受负步长。
template<class Start, class Stop, class Step>
auto range(Start start, Stop stop, Step step) {
    using value_type = std::common_type_t<std::decay_t<Start>, std::decay_t<Stop>, std::decay_t<Step>>;
    if constexpr (std::is_unsigned_v<value_type> && std::is_signed_v<std::decay_t<Step>>) {
        if (step < 0) {
            throw std::invalid_argument("LRange::range negative step requires a signed value type");
        }
    }
    return Detail::RangeView<value_type>(
        static_cast<value_type>(start),
        static_cast<value_type>(stop),
        static_cast<value_type>(step));
}

/// 把 range 收集进指定 Container。
template<class Container, class Range>
Container to(Range&& range) {
    Container out;
    if constexpr (ranges::sized_range<Range> &&
                  requires { out.reserve(static_cast<std::size_t>(ranges::size(range))); }) {
        out.reserve(static_cast<std::size_t>(ranges::size(range)));
    }
    ranges::copy(std::forward<Range>(range), ranges::inserter(out, out.end()));
    return out;
}

/// 返回可管道使用的 to<Container>() 闭包。
template<class Container>
auto to() {
    return Detail::ToClosure<Container> {};
}

/// 把 range 收集进 std::vector<T>。
template<class T, class Range>
std::vector<T> to_vector(Range&& range) {
    return to<std::vector<T>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_vector<T>() 闭包。
template<class T>
auto to_vector() {
    return Detail::ToVectorClosure<T> {};
}

/// 把 range 收集进 std::vector（元素类型自动推断）。
template<class Range>
auto to_vector(Range&& range) {
    using value_type = std::decay_t<ranges::range_value_t<Range>>;
    return to<std::vector<value_type>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_vector() 闭包（元素类型自动推断）。
inline auto to_vector() {
    return Detail::ToVectorClosure<void> {};
}

/// 把 range 收集进 std::set<T>。
template<class T, class Range>
std::set<T> to_set(Range&& range) {
    return to<std::set<T>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_set<T>() 闭包。
template<class T>
auto to_set() {
    return Detail::ToSetClosure<T> {};
}

/// 把 range 收集进 std::set（元素类型自动推断）。
template<class Range>
auto to_set(Range&& range) {
    using value_type = std::decay_t<ranges::range_value_t<Range>>;
    return to<std::set<value_type>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_set() 闭包（元素类型自动推断）。
inline auto to_set() {
    return Detail::ToSetClosure<void> {};
}

/// 把 range 收集进 std::unordered_set<T>。
template<class T, class Range>
std::unordered_set<T> to_unordered_set(Range&& range) {
    return to<std::unordered_set<T>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_unordered_set<T>() 闭包。
template<class T>
auto to_unordered_set() {
    return Detail::ToUnorderedSetClosure<T> {};
}

/// 把 range 收集进 std::unordered_set（元素类型自动推断）。
template<class Range>
auto to_unordered_set(Range&& range) {
    using value_type = std::decay_t<ranges::range_value_t<Range>>;
    return to<std::unordered_set<value_type>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_unordered_set() 闭包（元素类型自动推断）。
inline auto to_unordered_set() {
    return Detail::ToUnorderedSetClosure<void> {};
}

/// 把 pair range 收集进 std::map<Key, Value>。
template<class Key, class Value, class Range>
std::map<Key, Value> to_map(Range&& range) {
    return to<std::map<Key, Value>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_map<Key, Value>() 闭包。
template<class Key, class Value>
auto to_map() {
    return Detail::ToMapClosure<Key, Value> {};
}

/// 把 pair range 收集进 std::unordered_map<Key, Value>。
template<class Key, class Value, class Range>
std::unordered_map<Key, Value> to_unordered_map(Range&& range) {
    return to<std::unordered_map<Key, Value>>(std::forward<Range>(range));
}

/// 返回可管道使用的 to_unordered_map<Key, Value>() 闭包。
template<class Key, class Value>
auto to_unordered_map() {
    return Detail::ToUnorderedMapClosure<Key, Value> {};
}

/// 把 range 收集为 vector（collect 是 to_vector 的语义化别名）。
template<class Range>
auto collect(Range&& range) {
    return to_vector(std::forward<Range>(range));
}

/// 返回可管道使用的 collect() 闭包。
inline auto collect() {
    return Detail::ToVectorClosure<void> {};
}

/// 给 range 的每个元素附加从 start 开始的序号，产出 (index, value) 对。
template<class Range>
auto indexed(Range&& range, std::ptrdiff_t start = 0) {
    if constexpr (std::is_lvalue_reference_v<Range&&>) {
        return ranges::views::zip(ranges::views::ints(start, ranges::unreachable),
                                  ranges::views::all(range));
    } else {
        using container_type = std::decay_t<Range>;
        return ranges::views::zip(ranges::views::ints(start, ranges::unreachable),
                                  ContainerRange<container_type>(std::forward<Range>(range)));
    }
}

/// 对每个元素执行回调；返回回调本身以支持有状态回调。
template<class Range, class Fn>
Fn each(Range&& range, Fn fn) {
    for (auto&& item : range) {
        std::invoke(fn, item);
    }
    return fn;
}

/// 判断 range 是否包含指定值。
template<class Range, class T>
bool contains(Range&& range, const T& value) {
    return ranges::find(range, value) != ranges::end(range);
}

/// 判断 range 是否存在满足谓词的元素。
template<class Range, class Pred>
bool contains_if(Range&& range, Pred pred) {
    return ranges::find_if(range, std::move(pred)) != ranges::end(range);
}

/// 统计指定值的出现次数。
template<class Range, class T>
auto count(Range&& range, const T& value) {
    return ranges::count(range, value);
}

/// 统计满足谓词的元素数量。
template<class Range, class Pred>
auto count_if(Range&& range, Pred pred) {
    return ranges::count_if(range, std::move(pred));
}

/// 判断是否存在满足谓词的元素。
template<class Range, class Pred>
bool any(Range&& range, Pred pred) {
    return ranges::any_of(range, std::move(pred));
}

/// 判断是否所有元素都满足谓词。
template<class Range, class Pred>
bool all_of(Range&& range, Pred pred) {
    return ranges::all_of(range, std::move(pred));
}

/// 判断是否没有任何元素满足谓词。
template<class Range, class Pred>
bool none(Range&& range, Pred pred) {
    return ranges::none_of(range, std::move(pred));
}

/// 判断 range 是否为空。
template<class Range>
bool empty(Range&& range) {
    return ranges::begin(range) == ranges::end(range);
}

/// 返回 range 的元素数量。
template<class Range>
auto size(Range&& range) {
    return ranges::distance(range);
}

/// 返回首元素（拷贝）；空 range 返回 nullopt。
template<class Range>
auto first(Range&& range) -> std::optional<std::decay_t<ranges::range_reference_t<Range>>> {
    auto it = ranges::begin(range);
    if (it == ranges::end(range)) {
        return std::nullopt;
    }
    return *it;
}

/// 返回末元素（拷贝）；空 range 返回 nullopt。
template<class Range>
auto last(Range&& range) -> std::optional<std::decay_t<ranges::range_reference_t<Range>>> {
    auto it = ranges::begin(range);
    auto stop = ranges::end(range);
    if (it == stop) {
        return std::nullopt;
    }

    auto value = *it;
    for (++it; it != stop; ++it) {
        value = *it;
    }
    return value;
}

/// 对元素（可选投影）求和，初值 init 默认为 T{}。
template<class Range, class T = std::decay_t<ranges::range_value_t<Range>>, class Proj = ranges::identity>
T sum(Range&& range, T init = T {}, Proj proj = {}) {
    for (auto&& item : range) {
        init += std::invoke(proj, item);
    }
    return init;
}

/// 对元素（可选投影）求平均；空 range 返回 nullopt。
template<class Range, class Proj = ranges::identity>
std::optional<double> average(Range&& range, Proj proj = {}) {
    double total = 0.0;
    std::size_t n = 0;
    for (auto&& item : range) {
        total += static_cast<double>(std::invoke(proj, item));
        ++n;
    }
    if (n == 0) {
        return std::nullopt;
    }
    return total / static_cast<double>(n);
}

/// average() 的同义函数。
template<class Range, class Proj = ranges::identity>
std::optional<double> mean(Range&& range, Proj proj = {}) {
    return average(std::forward<Range>(range), std::move(proj));
}

/// 把可流式输出的元素用分隔符拼成字符串。
template<class Range>
std::string join_strings(Range&& range, std::string_view separator = {}) {
    std::ostringstream out;
    bool first_item = true;
    for (auto&& item : range) {
        if (!first_item) {
            out << separator;
        }
        first_item = false;
        out << item;
    }
    return out.str();
}

} // namespace LRange

#endif // LRANGE_INCLUDE

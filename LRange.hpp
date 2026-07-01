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
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
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

template<class T>
class RangeSentinel;

template<class T>
class RangeIterator {
public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using reference = T;
    using pointer = void;

    RangeIterator() = default;

    RangeIterator(T current, T stop, T step)
        : current_(current), stop_(stop), step_(step) {}

    T operator*() const {
        return current_;
    }

    RangeIterator& operator++() {
        current_ += step_;
        return *this;
    }

    void operator++(int) {
        ++(*this);
    }

    friend bool operator==(const RangeIterator& it, RangeSentinel<T>) {
        return it.done();
    }

    friend bool operator==(RangeSentinel<T> sentinel, const RangeIterator& it) {
        return it == sentinel;
    }

    friend bool operator!=(const RangeIterator& it, RangeSentinel<T> sentinel) {
        return !(it == sentinel);
    }

    friend bool operator!=(RangeSentinel<T> sentinel, const RangeIterator& it) {
        return !(it == sentinel);
    }

private:
    bool done() const {
        return step_ > T {} ? !(current_ < stop_) : !(current_ > stop_);
    }

    T current_ {};
    T stop_ {};
    T step_ {1};
};

template<class T>
class RangeSentinel {};

template<class T>
class RangeView : public ranges::view_base {
    static_assert(std::is_arithmetic_v<T>, "LRange::range requires arithmetic values");

public:
    using value_type = T;
    using iterator = RangeIterator<T>;
    using sentinel = RangeSentinel<T>;

    RangeView() = default;

    RangeView(T start, T stop, T step)
        : start_(start), stop_(stop), step_(step) {
        if (step_ == T {}) {
            throw std::invalid_argument("LRange::range step cannot be zero");
        }
    }

    iterator begin() const {
        return iterator(start_, stop_, step_);
    }

    sentinel end() const {
        return {};
    }

private:
    T start_ {};
    T stop_ {};
    T step_ {1};
};

template<class Container>
struct ToClosure {
    template<class Range>
    friend Container operator|(Range&& range, ToClosure) {
        return LRange::to<Container>(std::forward<Range>(range));
    }
};

template<class T>
struct ToVectorClosure {
    template<class Range>
    friend auto operator|(Range&& range, ToVectorClosure) {
        if constexpr (std::is_void_v<T>) {
            return LRange::to_vector(std::forward<Range>(range));
        } else {
            return LRange::to_vector<T>(std::forward<Range>(range));
        }
    }
};

template<class T>
struct ToSetClosure {
    template<class Range>
    friend auto operator|(Range&& range, ToSetClosure) {
        if constexpr (std::is_void_v<T>) {
            return LRange::to_set(std::forward<Range>(range));
        } else {
            return LRange::to_set<T>(std::forward<Range>(range));
        }
    }
};

template<class T>
struct ToUnorderedSetClosure {
    template<class Range>
    friend auto operator|(Range&& range, ToUnorderedSetClosure) {
        if constexpr (std::is_void_v<T>) {
            return LRange::to_unordered_set(std::forward<Range>(range));
        } else {
            return LRange::to_unordered_set<T>(std::forward<Range>(range));
        }
    }
};

template<class Key, class Value>
struct ToMapClosure {
    template<class Range>
    friend auto operator|(Range&& range, ToMapClosure) {
        return LRange::to_map<Key, Value>(std::forward<Range>(range));
    }
};

template<class Key, class Value>
struct ToUnorderedMapClosure {
    template<class Range>
    friend auto operator|(Range&& range, ToUnorderedMapClosure) {
        return LRange::to_unordered_map<Key, Value>(std::forward<Range>(range));
    }
};

} // namespace Detail

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

template<class Stop>
auto range(Stop stop) {
    using value_type = std::decay_t<Stop>;
    return Detail::RangeView<value_type>(value_type {}, static_cast<value_type>(stop), value_type {1});
}

template<class Start, class Stop>
auto range(Start start, Stop stop) {
    using value_type = std::common_type_t<std::decay_t<Start>, std::decay_t<Stop>>;
    return Detail::RangeView<value_type>(
        static_cast<value_type>(start),
        static_cast<value_type>(stop),
        value_type {1});
}

template<class Start, class Stop, class Step>
auto range(Start start, Stop stop, Step step) {
    using value_type = std::common_type_t<std::decay_t<Start>, std::decay_t<Stop>, std::decay_t<Step>>;
    return Detail::RangeView<value_type>(
        static_cast<value_type>(start),
        static_cast<value_type>(stop),
        static_cast<value_type>(step));
}

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

template<class Container>
auto to() {
    return Detail::ToClosure<Container> {};
}

template<class T, class Range>
std::vector<T> to_vector(Range&& range) {
    return to<std::vector<T>>(std::forward<Range>(range));
}

template<class T>
auto to_vector() {
    return Detail::ToVectorClosure<T> {};
}

template<class Range>
auto to_vector(Range&& range) {
    using value_type = std::decay_t<ranges::range_value_t<Range>>;
    return to<std::vector<value_type>>(std::forward<Range>(range));
}

inline auto to_vector() {
    return Detail::ToVectorClosure<void> {};
}

template<class T, class Range>
std::set<T> to_set(Range&& range) {
    return to<std::set<T>>(std::forward<Range>(range));
}

template<class T>
auto to_set() {
    return Detail::ToSetClosure<T> {};
}

template<class Range>
auto to_set(Range&& range) {
    using value_type = std::decay_t<ranges::range_value_t<Range>>;
    return to<std::set<value_type>>(std::forward<Range>(range));
}

inline auto to_set() {
    return Detail::ToSetClosure<void> {};
}

template<class T, class Range>
std::unordered_set<T> to_unordered_set(Range&& range) {
    return to<std::unordered_set<T>>(std::forward<Range>(range));
}

template<class T>
auto to_unordered_set() {
    return Detail::ToUnorderedSetClosure<T> {};
}

template<class Range>
auto to_unordered_set(Range&& range) {
    using value_type = std::decay_t<ranges::range_value_t<Range>>;
    return to<std::unordered_set<value_type>>(std::forward<Range>(range));
}

inline auto to_unordered_set() {
    return Detail::ToUnorderedSetClosure<void> {};
}

template<class Key, class Value, class Range>
std::map<Key, Value> to_map(Range&& range) {
    return to<std::map<Key, Value>>(std::forward<Range>(range));
}

template<class Key, class Value>
auto to_map() {
    return Detail::ToMapClosure<Key, Value> {};
}

template<class Key, class Value, class Range>
std::unordered_map<Key, Value> to_unordered_map(Range&& range) {
    return to<std::unordered_map<Key, Value>>(std::forward<Range>(range));
}

template<class Key, class Value>
auto to_unordered_map() {
    return Detail::ToUnorderedMapClosure<Key, Value> {};
}

template<class Range>
auto collect(Range&& range) {
    return to_vector(std::forward<Range>(range));
}

inline auto collect() {
    return Detail::ToVectorClosure<void> {};
}

template<class Range>
auto indexed(Range&& range, std::ptrdiff_t start = 0) {
    return ranges::views::zip(ranges::views::ints(start), ranges::views::all(std::forward<Range>(range)));
}

template<class Range, class Fn>
Fn each(Range&& range, Fn fn) {
    for (auto&& item : range) {
        std::invoke(fn, item);
    }
    return fn;
}

template<class Range, class T>
bool contains(Range&& range, const T& value) {
    return ranges::find(range, value) != ranges::end(range);
}

template<class Range, class Pred>
bool contains_if(Range&& range, Pred pred) {
    return ranges::find_if(range, std::move(pred)) != ranges::end(range);
}

template<class Range, class T>
auto count(Range&& range, const T& value) {
    return ranges::count(range, value);
}

template<class Range, class Pred>
auto count_if(Range&& range, Pred pred) {
    return ranges::count_if(range, std::move(pred));
}

template<class Range, class Pred>
bool any(Range&& range, Pred pred) {
    return ranges::any_of(range, std::move(pred));
}

template<class Range, class Pred>
bool all_of(Range&& range, Pred pred) {
    return ranges::all_of(range, std::move(pred));
}

template<class Range, class Pred>
bool none(Range&& range, Pred pred) {
    return ranges::none_of(range, std::move(pred));
}

template<class Range>
bool empty(Range&& range) {
    return ranges::begin(range) == ranges::end(range);
}

template<class Range>
auto size(Range&& range) {
    return ranges::distance(range);
}

template<class Range>
auto first(Range&& range) -> std::optional<std::decay_t<ranges::range_reference_t<Range>>> {
    auto it = ranges::begin(range);
    if (it == ranges::end(range)) {
        return std::nullopt;
    }
    return *it;
}

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

template<class Range, class T = std::decay_t<ranges::range_value_t<Range>>, class Proj = ranges::identity>
T sum(Range&& range, T init = T {}, Proj proj = {}) {
    for (auto&& item : range) {
        init += std::invoke(proj, item);
    }
    return init;
}

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

template<class Range, class Proj = ranges::identity>
std::optional<double> mean(Range&& range, Proj proj = {}) {
    return average(std::forward<Range>(range), std::move(proj));
}

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

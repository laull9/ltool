/**
 * @file LRandom.hpp
 * @brief std::random 的纯头文件封装，提供常用随机数、抽样、洗牌、
 *        随机文本和 UUID 工具。
 *
 * LRandom 的核心定位：
 * - 内部默认使用 std::mt19937_64，构造时从 std::random_device 和时间熵播种。
 * - 支持显式 seed，方便测试、模拟和可复现实验。
 * - 保留 engine() 入口，需要标准库分布器时可以直接复用底层引擎。
 * - 提供 thread_local 的 shared() 实例和 rand_* 静态快捷函数，适合一行调用
 *   不关心复现性的随机能力。
 *
 * 主要能力：
 * - 数值：integer、real、normal、exponential、boolean、chance、roll。
 * - 容器：choice、weighted_choice、shuffle、shuffled、sample。
 * - 字节与文本：bytes、fill_bytes、string、hex、uuid_v4、uuid_v7。
 * - 安全 UUID：safe_uuid_v4、safe_uuid_v7 使用操作系统安全随机源。
 *
 * 常用示例：
 * @code
 * LRandom rng(42);                         // 可复现随机序列
 * auto dice = rng.roll();
 * auto score = rng.real(0.0, 100.0);
 * auto name = rng.string(8);
 *
 * std::vector<int> xs {1, 2, 3, 4};
 * rng.shuffle(xs);
 * auto one = rng.choice(xs);
 *
 * auto id = LRandom::shared().uuid_v4();   // 线程局部快捷实例
 * auto sid = LRandom::safe_uuid_v7();      // 系统安全随机 UUIDv7
 * auto n = LRandom::rand_int(1, 100);      // 线程安全快捷函数
 * @endcode
 */

#ifndef LTOOL_LRANDOM_INCLUDE
#define LTOOL_LRANDOM_INCLUDE

#include "detail/LConfig.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX
#include <windows.h>
#elif defined(__linux__) && LTOOL_HAS_INCLUDE(<sys/random.h>)
#include <sys/random.h>
#include <unistd.h>
#endif // defined(_WIN32)

namespace LRandomDetail {

inline std::runtime_error empty_range_error(const char* action) {
    return std::runtime_error(std::string("LRandom cannot ") + action + " from an empty range");
}

template<class T>
inline void require_ordered_range(T min_value, T max_value, const char* name) {
    if (max_value < min_value) {
        throw std::invalid_argument(std::string("LRandom invalid ") + name + " range");
    }
}

inline void require_probability(double value) {
    if (value < 0.0 || value > 1.0) {
        throw std::invalid_argument("LRandom probability must be in [0, 1]");
    }
}

inline std::uint64_t mix_seed(std::uint64_t seed, std::uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

inline std::uint64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return ms < 0 ? 0 : static_cast<std::uint64_t>(ms);
}

inline std::string format_uuid(const std::array<std::uint8_t, 16>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(digits[(data[i] >> 4) & 0x0f]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

inline std::string uuid_v4_from_bytes(std::array<std::uint8_t, 16> data) {
    data[6] = static_cast<std::uint8_t>((data[6] & 0x0f) | 0x40);
    data[8] = static_cast<std::uint8_t>((data[8] & 0x3f) | 0x80);
    return format_uuid(data);
}

inline std::string uuid_v7_from_bytes(std::array<std::uint8_t, 16> data,
                                      std::uint64_t timestamp_ms) {
    data[0] = static_cast<std::uint8_t>((timestamp_ms >> 40) & 0xff);
    data[1] = static_cast<std::uint8_t>((timestamp_ms >> 32) & 0xff);
    data[2] = static_cast<std::uint8_t>((timestamp_ms >> 24) & 0xff);
    data[3] = static_cast<std::uint8_t>((timestamp_ms >> 16) & 0xff);
    data[4] = static_cast<std::uint8_t>((timestamp_ms >> 8) & 0xff);
    data[5] = static_cast<std::uint8_t>(timestamp_ms & 0xff);
    data[6] = static_cast<std::uint8_t>((data[6] & 0x0f) | 0x70);
    data[8] = static_cast<std::uint8_t>((data[8] & 0x3f) | 0x80);
    return format_uuid(data);
}

inline void urandom_fill(void* data, std::size_t size) {
    if (size == 0) {
        return;
    }

    std::ifstream in("/dev/urandom", std::ios::binary);
    if (!in) {
        throw std::runtime_error("LRandom cannot open /dev/urandom");
    }
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!in) {
        throw std::runtime_error("LRandom cannot read /dev/urandom");
    }
}

inline void system_random_fill(void* data, std::size_t size) {
    if (size == 0) {
        return;
    }

#if defined(_WIN32)
    typedef LONG(WINAPI * BCryptGenRandomFn)(void*, unsigned char*, unsigned long, unsigned long);
    static HMODULE module = LoadLibraryA("bcrypt.dll");
    static BCryptGenRandomFn bcrypt_gen_random =
        module ? reinterpret_cast<BCryptGenRandomFn>(GetProcAddress(module, "BCryptGenRandom"))
               : nullptr;
    if (!bcrypt_gen_random) {
        throw std::runtime_error("LRandom cannot load BCryptGenRandom");
    }

    auto* out = static_cast<unsigned char*>(data);
    std::size_t done = 0;
    while (done < size) {
        const auto remaining = size - done;
        const auto chunk =
            remaining > static_cast<std::size_t>((std::numeric_limits<unsigned long>::max)())
                ? (std::numeric_limits<unsigned long>::max)()
                : static_cast<unsigned long>(remaining);
        const auto status = bcrypt_gen_random(nullptr, out + done, chunk, 0x00000002UL);
        if (status < 0) {
            throw std::runtime_error("LRandom BCryptGenRandom failed");
        }
        done += static_cast<std::size_t>(chunk);
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__)
    arc4random_buf(data, size);
#elif defined(__linux__) && LTOOL_HAS_INCLUDE(<sys/random.h>)
    auto* out = static_cast<unsigned char*>(data);
    std::size_t done = 0;
    while (done < size) {
        const auto result = getrandom(out + done, size - done, 0);
        if (result > 0) {
            done += static_cast<std::size_t>(result);
            continue;
        }
        if (result == -1 && errno == EINTR) {
            continue;
        }
        if (result == -1 && errno == ENOSYS) {
            urandom_fill(out + done, size - done);
            return;
        }
        throw std::runtime_error("LRandom getrandom failed");
    }
#elif !defined(_WIN32)
    urandom_fill(data, size);
#endif
}

inline std::array<std::uint8_t, 16> system_random_16() {
    std::array<std::uint8_t, 16> data {};
    system_random_fill(data.data(), data.size());
    return data;
}

} // namespace LRandomDetail

/**
 * @brief std::mt19937_64 随机引擎的便捷封装。
 *
 * LRandom 实例本身不加锁；多个线程需要共享同一个实例时请在外部同步。
 * 如果只需要快捷随机值，使用 shared() 可获得每个线程独立的 LRandom 实例。
 */
class LRandom {
public:
    using engine_type = std::mt19937_64;
    using result_type = engine_type::result_type;

    /**
     * @brief 使用系统熵和当前时间构造随机引擎。
     */
    LRandom()
        : engine_(make_engine()) {}

    /**
     * @brief 使用固定种子构造随机引擎，适合可复现测试。
     */
    explicit LRandom(result_type seed)
        : engine_(seed) {}

    /**
     * @brief 返回线程局部的默认随机实例。
     */
    static LRandom& shared() {
        static thread_local LRandom rng;
        return rng;
    }

    /**
     * @brief 使用线程局部默认实例返回一个原始随机值。
     */
    static result_type rand_raw() {
        return shared().raw();
    }

    /**
     * @brief 使用线程局部默认实例返回闭区间 [min_value, max_value] 内的整数。
     */
    template<class T>
    static typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value,
                                   T>::type
    rand_int(T min_value, T max_value) {
        return shared().integer(min_value, max_value);
    }

    /**
     * @brief 使用线程局部默认实例返回闭区间 [0, max_value] 内的整数。
     */
    template<class T>
    static typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value,
                                   T>::type
    rand_int(T max_value) {
        return shared().integer(max_value);
    }

    /**
     * @brief 使用线程局部默认实例返回半开区间 [min_value, max_value) 内的浮点数。
     */
    template<class T>
    static typename std::enable_if<std::is_floating_point<T>::value, T>::type rand_real(
        T min_value, T max_value) {
        return shared().real(min_value, max_value);
    }

    /**
     * @brief 使用线程局部默认实例返回半开区间 [0, 1) 内的 double。
     */
    static double rand_real() {
        return shared().real();
    }

    /**
     * @brief 使用线程局部默认实例返回正态分布随机数。
     */
    template<class T>
    static typename std::enable_if<std::is_floating_point<T>::value, T>::type rand_normal(
        T mean = T(0), T stddev = T(1)) {
        return shared().normal(mean, stddev);
    }

    /**
     * @brief 使用线程局部默认实例返回指数分布随机数。
     */
    template<class T>
    static typename std::enable_if<std::is_floating_point<T>::value, T>::type rand_exponential(
        T lambda = T(1)) {
        return shared().exponential(lambda);
    }

    /**
     * @brief 使用线程局部默认实例按指定概率返回 true。
     */
    static bool rand_bool(double true_probability = 0.5) {
        return shared().boolean(true_probability);
    }

    /**
     * @brief 使用线程局部默认实例判断一次概率事件是否发生。
     */
    static bool rand_chance(double probability) {
        return shared().chance(probability);
    }

    /**
     * @brief 使用线程局部默认实例掷一个 sides 面骰子。
     */
    static int rand_roll(int sides = 6) {
        return shared().roll(sides);
    }

    /**
     * @brief 使用线程局部默认实例返回 [0, size) 内的索引。
     */
    static std::size_t rand_index(std::size_t size) {
        return shared().index(size);
    }

    /**
     * @brief 使用线程局部默认实例从迭代器区间随机选中一个位置。
     */
    template<class It>
    static It rand_pick(It first, It last) {
        return shared().pick(first, last);
    }

    /**
     * @brief 使用线程局部默认实例从容器中随机选一个元素。
     */
    template<class Container>
    static auto rand_choice(Container& values) -> decltype(*std::begin(values)) {
        return shared().choice(values);
    }

    template<class Container>
    static auto rand_choice(const Container& values) -> decltype(*std::begin(values)) {
        return shared().choice(values);
    }

    /**
     * @brief 使用线程局部默认实例从初始化列表中随机选一个值。
     */
    template<class T>
    static T rand_choice(std::initializer_list<T> values) {
        return shared().choice(values);
    }

    /**
     * @brief 使用线程局部默认实例根据权重返回一个索引。
     */
    template<class It>
    static std::size_t rand_weighted_index(It first, It last) {
        return shared().weighted_index(first, last);
    }

    template<class Container>
    static std::size_t rand_weighted_index(const Container& weights) {
        return shared().weighted_index(weights);
    }

    /**
     * @brief 使用线程局部默认实例按权重从容器中随机选一个元素。
     */
    template<class Container, class Weights>
    static auto rand_weighted_choice(Container& values, const Weights& weights)
        -> decltype(*std::begin(values)) {
        return shared().weighted_choice(values, weights);
    }

    template<class Container, class Weights>
    static auto rand_weighted_choice(const Container& values, const Weights& weights)
        -> decltype(*std::begin(values)) {
        return shared().weighted_choice(values, weights);
    }

    /**
     * @brief 使用线程局部默认实例原地打乱迭代器区间。
     */
    template<class RandomIt>
    static void rand_shuffle(RandomIt first, RandomIt last) {
        shared().shuffle(first, last);
    }

    /**
     * @brief 使用线程局部默认实例原地打乱容器。
     */
    template<class Container>
    static void rand_shuffle(Container& values) {
        shared().shuffle(values);
    }

    /**
     * @brief 使用线程局部默认实例返回打乱后的容器副本。
     */
    template<class Container>
    static Container rand_shuffled(Container values) {
        return shared().shuffled(std::move(values));
    }

    /**
     * @brief 使用线程局部默认实例从区间中随机抽取最多 count 个元素。
     */
    template<class It>
    static std::vector<typename std::iterator_traits<It>::value_type> rand_sample(It first,
                                                                                  It last,
                                                                                  std::size_t count) {
        return shared().sample(first, last, count);
    }

    template<class Container>
    static std::vector<typename Container::value_type> rand_sample(const Container& values,
                                                                   std::size_t count) {
        return shared().sample(values, count);
    }

    /**
     * @brief 使用线程局部默认实例生成随机字节。
     */
    static std::vector<std::uint8_t> rand_bytes(std::size_t size) {
        return shared().bytes(size);
    }

    /**
     * @brief 使用线程局部默认实例用随机字节填充可写迭代器区间。
     */
    template<class It>
    static void rand_fill_bytes(It first, It last) {
        shared().fill_bytes(first, last);
    }

    /**
     * @brief 使用线程局部默认实例生成随机文本。
     */
    static std::string rand_string(std::size_t size) {
        return shared().string(size);
    }

    static std::string rand_string(std::size_t size, const std::string& alphabet) {
        return shared().string(size, alphabet);
    }

    static std::string rand_string(std::size_t size, const char* alphabet) {
        return shared().string(size, alphabet);
    }

    /**
     * @brief 使用线程局部默认实例生成十六进制文本。
     */
    static std::string rand_hex(std::size_t bytes_count) {
        return shared().hex(bytes_count);
    }

    /**
     * @brief 使用线程局部默认实例生成 RFC 4122 version 4 UUID 字符串。
     */
    static std::string rand_uuid_v4() {
        return shared().uuid_v4();
    }

    /**
     * @brief 使用线程局部默认实例生成 RFC 9562 version 7 UUID 字符串。
     */
    static std::string rand_uuid_v7() {
        return shared().uuid_v7();
    }

    /**
     * @brief 使用操作系统安全随机源生成 RFC 4122 version 4 UUID 字符串。
     */
    static std::string safe_uuid_v4() {
        return LRandomDetail::uuid_v4_from_bytes(LRandomDetail::system_random_16());
    }

    /**
     * @brief 使用操作系统安全随机源生成 RFC 9562 version 7 UUID 字符串。
     */
    static std::string safe_uuid_v7() {
        return LRandomDetail::uuid_v7_from_bytes(LRandomDetail::system_random_16(),
                                                 LRandomDetail::now_ms());
    }

    /**
     * @brief 用固定种子重置当前引擎。
     */
    void seed(result_type value) {
        engine_.seed(value);
    }

    /**
     * @brief 重新使用系统熵和当前时间播种。
     */
    void seed_system() {
        engine_ = make_engine();
    }

    /**
     * @brief 访问底层标准库随机引擎。
     */
    engine_type& engine() noexcept {
        return engine_;
    }

    const engine_type& engine() const noexcept {
        return engine_;
    }

    /**
     * @brief 返回底层引擎的一个原始随机值。
     */
    result_type raw() {
        return engine_();
    }

    /**
     * @brief 返回闭区间 [min_value, max_value] 内的整数。
     */
    template<class T>
    typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, T>::type
    integer(T min_value, T max_value) {
        LRandomDetail::require_ordered_range(min_value, max_value, "integer");
        using distribution_type = typename std::conditional<std::is_signed<T>::value,
                                                            long long,
                                                            unsigned long long>::type;
        std::uniform_int_distribution<distribution_type> dist(
            static_cast<distribution_type>(min_value),
            static_cast<distribution_type>(max_value));
        return static_cast<T>(dist(engine_));
    }

    /**
     * @brief 返回闭区间 [0, max_value] 内的整数。
     */
    template<class T>
    typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, T>::type
    integer(T max_value) {
        return integer<T>(0, max_value);
    }

    /**
     * @brief 返回半开区间 [min_value, max_value) 内的浮点数。
     */
    template<class T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type real(T min_value,
                                                                            T max_value) {
        LRandomDetail::require_ordered_range(min_value, max_value, "real");
        std::uniform_real_distribution<T> dist(min_value, max_value);
        return dist(engine_);
    }

    /**
     * @brief 返回半开区间 [0, 1) 内的 double。
     */
    double real() {
        return real(0.0, 1.0);
    }

    /**
     * @brief 返回均值 mean、标准差 stddev 的正态分布随机数。
     */
    template<class T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type normal(T mean = T(0),
                                                                              T stddev = T(1)) {
        if (stddev <= T(0)) {
            throw std::invalid_argument("LRandom normal stddev must be positive");
        }
        std::normal_distribution<T> dist(mean, stddev);
        return dist(engine_);
    }

    /**
     * @brief 返回 lambda 参数下的指数分布随机数。
     */
    template<class T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type exponential(T lambda = T(1)) {
        if (lambda <= T(0)) {
            throw std::invalid_argument("LRandom exponential lambda must be positive");
        }
        std::exponential_distribution<T> dist(lambda);
        return dist(engine_);
    }

    /**
     * @brief 按指定概率返回 true。
     */
    bool boolean(double true_probability = 0.5) {
        LRandomDetail::require_probability(true_probability);
        std::bernoulli_distribution dist(true_probability);
        return dist(engine_);
    }

    /**
     * @brief 判断一次概率事件是否发生。
     */
    bool chance(double probability) {
        return boolean(probability);
    }

    /**
     * @brief 掷一个 sides 面骰子，返回 [1, sides]。
     */
    int roll(int sides = 6) {
        if (sides <= 0) {
            throw std::invalid_argument("LRandom roll sides must be positive");
        }
        return integer(1, sides);
    }

    /**
     * @brief 返回 [0, size) 内的索引。
     */
    std::size_t index(std::size_t size) {
        if (size == 0) {
            throw LRandomDetail::empty_range_error("pick index");
        }
        return integer<std::size_t>(0, size - 1);
    }

    /**
     * @brief 从迭代器区间随机选中一个位置。
     */
    template<class It>
    It pick(It first, It last) {
        const auto count = std::distance(first, last);
        if (count <= 0) {
            throw LRandomDetail::empty_range_error("pick");
        }
        auto offset = integer<typename std::iterator_traits<It>::difference_type>(0, count - 1);
        std::advance(first, offset);
        return first;
    }

    /**
     * @brief 从容器中随机选一个元素。
     */
    template<class Container>
    auto choice(Container& values) -> decltype(*std::begin(values)) {
        return *pick(std::begin(values), std::end(values));
    }

    template<class Container>
    auto choice(const Container& values) -> decltype(*std::begin(values)) {
        return *pick(std::begin(values), std::end(values));
    }

    /**
     * @brief 从初始化列表中随机选一个值。
     */
    template<class T>
    T choice(std::initializer_list<T> values) {
        return *pick(values.begin(), values.end());
    }

    /**
     * @brief 根据权重返回一个索引；权重为 0 的项不会被优先选中。
     */
    template<class It>
    std::size_t weighted_index(It first, It last) {
        std::vector<double> weights;
        for (; first != last; ++first) {
            const double weight = static_cast<double>(*first);
            if (weight < 0.0) {
                throw std::invalid_argument("LRandom weights must be non-negative");
            }
            weights.push_back(weight);
        }
        if (weights.empty()) {
            throw LRandomDetail::empty_range_error("pick weighted index");
        }
        std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
        return dist(engine_);
    }

    template<class Container>
    std::size_t weighted_index(const Container& weights) {
        return weighted_index(std::begin(weights), std::end(weights));
    }

    /**
     * @brief 按权重从容器中随机选一个元素。
     */
    template<class Container, class Weights>
    auto weighted_choice(Container& values, const Weights& weights) -> decltype(*std::begin(values)) {
        const auto value_count = std::distance(std::begin(values), std::end(values));
        const auto weight_count = std::distance(std::begin(weights), std::end(weights));
        if (value_count <= 0) {
            throw LRandomDetail::empty_range_error("pick weighted value");
        }
        if (value_count != weight_count) {
            throw std::invalid_argument("LRandom values and weights must have the same size");
        }
        auto it = std::begin(values);
        std::advance(it, static_cast<typename std::iterator_traits<decltype(it)>::difference_type>(
                             weighted_index(weights)));
        return *it;
    }

    template<class Container, class Weights>
    auto weighted_choice(const Container& values, const Weights& weights)
        -> decltype(*std::begin(values)) {
        const auto value_count = std::distance(std::begin(values), std::end(values));
        const auto weight_count = std::distance(std::begin(weights), std::end(weights));
        if (value_count <= 0) {
            throw LRandomDetail::empty_range_error("pick weighted value");
        }
        if (value_count != weight_count) {
            throw std::invalid_argument("LRandom values and weights must have the same size");
        }
        auto it = std::begin(values);
        std::advance(it, static_cast<typename std::iterator_traits<decltype(it)>::difference_type>(
                             weighted_index(weights)));
        return *it;
    }

    /**
     * @brief 原地打乱迭代器区间。
     */
    template<class RandomIt>
    void shuffle(RandomIt first, RandomIt last) {
        std::shuffle(first, last, engine_);
    }

    /**
     * @brief 原地打乱容器。
     */
    template<class Container>
    void shuffle(Container& values) {
        shuffle(std::begin(values), std::end(values));
    }

    /**
     * @brief 返回打乱后的容器副本。
     */
    template<class Container>
    Container shuffled(Container values) {
        shuffle(values);
        return values;
    }

    /**
     * @brief 从区间中随机抽取最多 count 个元素。
     */
    template<class It>
    std::vector<typename std::iterator_traits<It>::value_type> sample(It first, It last,
                                                                      std::size_t count) {
        using value_type = typename std::iterator_traits<It>::value_type;
        std::vector<value_type> pool(first, last);
        if (count >= pool.size()) {
            shuffle(pool);
            return pool;
        }
        shuffle(pool);
        pool.resize(count);
        return pool;
    }

    template<class Container>
    std::vector<typename Container::value_type> sample(const Container& values, std::size_t count) {
        return sample(std::begin(values), std::end(values), count);
    }

    /**
     * @brief 生成 size 个随机字节。
     */
    std::vector<std::uint8_t> bytes(std::size_t size) {
        std::vector<std::uint8_t> out(size);
        fill_bytes(out.begin(), out.end());
        return out;
    }

    /**
     * @brief 用随机字节填充可写迭代器区间。
     */
    template<class It>
    void fill_bytes(It first, It last) {
        for (; first != last; ++first) {
            *first = static_cast<std::uint8_t>(integer<int>(0, 255));
        }
    }

    /**
     * @brief 使用默认字母表生成随机文本。
     */
    std::string string(std::size_t size) {
        return string(size, alphanumeric_chars());
    }

    /**
     * @brief 使用指定字母表生成随机文本。
     */
    std::string string(std::size_t size, const std::string& alphabet) {
        if (alphabet.empty()) {
            throw LRandomDetail::empty_range_error("build string");
        }

        std::string out;
        out.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            out.push_back(alphabet[index(alphabet.size())]);
        }
        return out;
    }

    std::string string(std::size_t size, const char* alphabet) {
        return string(size, std::string(alphabet ? alphabet : ""));
    }

    /**
     * @brief 生成 bytes_count 个随机字节对应的十六进制文本。
     */
    std::string hex(std::size_t bytes_count) {
        static const char* digits = "0123456789abcdef";
        auto data = bytes(bytes_count);
        std::string out;
        out.reserve(bytes_count * 2);
        for (auto value : data) {
            out.push_back(digits[(value >> 4) & 0x0f]);
            out.push_back(digits[value & 0x0f]);
        }
        return out;
    }

    /**
     * @brief 生成 RFC 4122 version 4 UUID 字符串。
     */
    std::string uuid_v4() {
        std::array<std::uint8_t, 16> data {};
        fill_bytes(data.begin(), data.end());
        return LRandomDetail::uuid_v4_from_bytes(data);
    }

    /**
     * @brief 生成 RFC 9562 version 7 UUID 字符串。
     *
     * UUIDv7 的高 48 位使用当前 Unix 毫秒时间戳，剩余位使用当前 LRandom
     * 实例的随机引擎生成。需要系统安全随机时使用 safe_uuid_v7()。
     */
    std::string uuid_v7() {
        std::array<std::uint8_t, 16> data {};
        fill_bytes(data.begin(), data.end());
        return LRandomDetail::uuid_v7_from_bytes(data, LRandomDetail::now_ms());
    }

    /**
     * @brief 默认随机文本字母表：数字、大小写英文字母。
     */
    static const std::string& alphanumeric_chars() {
        static const std::string chars =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        return chars;
    }

    /**
     * @brief 十六进制文本字母表。
     */
    static const std::string& hex_chars() {
        static const std::string chars = "0123456789abcdef";
        return chars;
    }

private:
    static engine_type make_engine() {
        std::array<std::uint32_t, 8> seeds;
        std::uint64_t mixed =
            static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now()
                                           .time_since_epoch()
                                           .count());

        try {
            std::random_device device;
            for (std::size_t i = 0; i < seeds.size(); ++i) {
                mixed = LRandomDetail::mix_seed(mixed, static_cast<std::uint64_t>(device()));
                seeds[i] = static_cast<std::uint32_t>(mixed & 0xffffffffU);
            }
        } catch (...) {
            for (std::size_t i = 0; i < seeds.size(); ++i) {
                mixed = LRandomDetail::mix_seed(mixed, static_cast<std::uint64_t>(i + 1));
                seeds[i] = static_cast<std::uint32_t>(mixed & 0xffffffffU);
            }
        }

        std::seed_seq seq(seeds.begin(), seeds.end());
        return engine_type(seq);
    }

    engine_type engine_;
};

namespace LTool {
using Random = ::LRandom;
} // namespace LTool

#endif // LTOOL_LRANDOM_INCLUDE

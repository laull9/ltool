/**
 * @file detail/LConcepts.hpp
 * @brief ltool 的可选 C++20 concept 约束集合。
 */

#ifndef LTOOL_LCONCEPTS_INCLUDE
#define LTOOL_LCONCEPTS_INCLUDE

#include "LToolConfig.hpp"

#include <iterator>
#include <type_traits>
#include <utility>

namespace LTool {
namespace traits {

template<class...>
using void_t = void;

template<class T>
struct remove_cvref {
    using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<class T>
using remove_cvref_t = typename remove_cvref<T>::type;

template<class T>
struct is_arithmetic : std::is_arithmetic<remove_cvref_t<T>> {};

template<class T>
struct is_non_bool_integral
    : std::integral_constant<bool,
                             std::is_integral<remove_cvref_t<T>>::value &&
                                 !std::is_same<remove_cvref_t<T>, bool>::value> {};

template<class T>
struct is_floating_point : std::is_floating_point<remove_cvref_t<T>> {};

template<class T>
struct is_enum : std::is_enum<remove_cvref_t<T>> {};

template<class T>
struct is_member_object_pointer : std::is_member_object_pointer<remove_cvref_t<T>> {};

template<class...>
struct all_member_object_pointers : std::true_type {};

template<class T, class... Rest>
struct all_member_object_pointers<T, Rest...>
    : std::integral_constant<bool,
                             is_member_object_pointer<T>::value &&
                                 all_member_object_pointers<Rest...>::value> {};

template<class T, class = void>
struct is_iterator : std::false_type {};

template<class T>
struct is_iterator<T,
                   void_t<typename std::iterator_traits<T>::value_type,
                          typename std::iterator_traits<T>::difference_type>>
    : std::true_type {};

template<class T, class = void>
struct is_random_access_iterator : std::false_type {};

template<class T>
struct is_random_access_iterator<T,
                                 void_t<typename std::iterator_traits<T>::iterator_category>>
    : std::is_base_of<std::random_access_iterator_tag,
                      typename std::iterator_traits<T>::iterator_category> {};

template<class T, class = void>
struct is_range : std::false_type {};

template<class T>
struct is_range<T, void_t<decltype(std::begin(std::declval<T&>())),
                          decltype(std::end(std::declval<T&>()))>> : std::true_type {};

template<class T, class = void>
struct is_const_range : std::false_type {};

template<class T>
struct is_const_range<T, void_t<decltype(std::begin(std::declval<const T&>())),
                                decltype(std::end(std::declval<const T&>()))>> : std::true_type {};

template<class F, class... Args>
struct is_invocable {
private:
    template<class G>
    static auto test(int)
        -> decltype(std::declval<G>()(std::declval<Args>()...), std::true_type());

    template<class>
    static std::false_type test(...);

public:
    static const bool value = decltype(test<F>(0))::value;
};

} // namespace traits
} // namespace LTool

#if LTOOL_HAS_CONCEPTS
#include <concepts>

namespace LTool {
namespace concepts {

template<class T>
concept Arithmetic = traits::is_arithmetic<T>::value;

template<class T>
concept NonBoolIntegral = traits::is_non_bool_integral<T>::value;

template<class T>
concept FloatingPoint = traits::is_floating_point<T>::value;

template<class T>
concept Enum = traits::is_enum<T>::value;

template<class T>
concept MemberPointer = std::is_member_pointer_v<traits::remove_cvref_t<T>>;

template<class T>
concept MemberObjectPointer = std::is_member_object_pointer_v<traits::remove_cvref_t<T>>;

template<class... Ts>
concept MemberObjectPointerPack = traits::all_member_object_pointers<Ts...>::value;

template<class T>
concept Iterator = traits::is_iterator<T>::value;

template<class T>
concept Range = traits::is_range<T>::value;

template<class T>
concept ConstRange = traits::is_const_range<T>::value;

template<class T>
concept RandomAccessIterator = traits::is_random_access_iterator<T>::value;

template<class F, class... Args>
concept InvocableWith = traits::is_invocable<F, Args...>::value;

} // namespace concepts
} // namespace LTool

#endif // LTOOL_HAS_CONCEPTS

#if LTOOL_HAS_CONCEPTS
#define LTOOL_REQUIRES(...) requires (__VA_ARGS__)
#define LTOOL_ENABLE_IF(...)
#define LTOOL_CONSTRAINED_RETURN(ReturnType, ...) ReturnType
#else
#define LTOOL_REQUIRES(...)
#define LTOOL_ENABLE_IF(...) , typename std::enable_if<(__VA_ARGS__), int>::type = 0
#define LTOOL_CONSTRAINED_RETURN(ReturnType, ...) \
    typename std::enable_if<(__VA_ARGS__), ReturnType>::type
#endif

#endif // LTOOL_LCONCEPTS_INCLUDE

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#if __has_include(<format>)
#include <format>
#endif

#include "LString.hpp"

using namespace std::literals;

enum class LStringTestColor {
    Red,
    Green,
    Blue
};

TEST_CASE("LString behaves as a thin std::string wrapper", "[LString]") {
    LString value = "hello";
    std::string native = value;
    std::string_view view = value;

    REQUIRE(native == "hello");
    REQUIRE(view == "hello");
    REQUIRE(value == std::string("hello"));
    REQUIRE("hello" == value);

    value += ", ";
    value += "world";

    REQUIRE(value.str() == "hello, world");
    REQUIRE(value.view() == "hello, world"sv);
    REQUIRE(value.subview(7, 5) == "world"sv);
}

TEST_CASE("LString only exposes a readable std::string reference implicitly", "[LString]") {
    LString s = "alpha";

    // 隐式 const 引用转换仍然可用。
    const std::string& cref = s;
    REQUIRE(cref == "alpha");

    // 拷贝为 std::string 仍然可用。
    std::string copy = s;
    REQUIRE(copy == "alpha");

    // str() 仍是显式可写接口，写入会反映到底层缓冲区。
    s.str().append("-beta");
    REQUIRE(s == "alpha-beta");
    REQUIRE(s.str() == "alpha-beta");
}

TEST_CASE("LString provides basic string operations", "[LString]") {
    LString value = "  a,b,,c  ";

    REQUIRE(value.trimmed() == "a,b,,c");
    REQUIRE(value.ltrimmed() == "a,b,,c  ");
    REQUIRE(value.rtrimmed() == "  a,b,,c");
    REQUIRE(LString("abc").upper_ascii() == "ABC");
    REQUIRE(LString("ABC").lower_ascii() == "abc");
    REQUIRE(LString("ha").repeat(3) == "hahaha");

    auto parts = value.trimmed().split(',', true);
    REQUIRE(parts == std::vector<LString> {"a", "b", "", "c"});
    REQUIRE(LString::join(parts, "/") == "a/b//c");
    REQUIRE(LString("a\nb\r\n").lines(true) == std::vector<LString> {"a", "b", ""});
}

TEST_CASE("LString handles boundary inputs", "[LString]") {
    const char* null_data = nullptr;

    REQUIRE(LString(null_data).empty());
    REQUIRE(LString(null_data, 0).empty());
    REQUIRE_THROWS_AS(LString(null_data, 1), std::invalid_argument);

    REQUIRE(LString("").subview().empty());
    REQUIRE(LString("x").repeat(0).empty());
    REQUIRE_THROWS_AS(LString("x").repeat(static_cast<LString::size_type>(-1)),
                      std::length_error);

    REQUIRE(LString::from_base64("/w==") == LString(std::string("\xFF", 1)));
    REQUIRE_THROWS_AS(LString::from_base64("/x=="), std::runtime_error);
    REQUIRE_THROWS_AS(LString::from_base64("//9="), std::runtime_error);
    REQUIRE(LString::from_base64("/x==", false) == LString(std::string("\xFF", 1)));
}

TEST_CASE("LString supports assign overloads", "[LString][assign]") {
    LString value;

    REQUIRE(&value.assign("hello") == &value);
    REQUIRE(value == "hello");

    value.assign("abc\0def", 7);
    REQUIRE(value == LString(std::string("abc\0def", 7)));

    value.assign("abcdef", 1, 3);
    REQUIRE(value == "bcd");

    value.assign(4, 'x');
    REQUIRE(value == "xxxx");

    value.assign('q');
    REQUIRE(value == "q");

    const std::string native = "012345";
    value.assign(native, 2, 3);
    REQUIRE(value == "234");

    value.assign(std::string_view("view-data"), 5, 4);
    REQUIRE(value == "data");

    std::vector<char> bytes {'r', 'a', 'n', 'g', 'e'};
    value.assign(bytes.begin(), bytes.end());
    REQUIRE(value == "range");

    value.assign({'i', 'n', 'i', 't'});
    REQUIRE(value == "init");

    value.assign(std::string("moved"));
    REQUIRE(value == "moved");

    LString source = "source";
    value.assign(source, 1, 4);
    REQUIRE(value == "ourc");

    value.assign(LString("owned"));
    REQUIRE(value == "owned");

    value.assign(std::filesystem::path("root") / "child.txt");
    REQUIRE(value.ends_with("child.txt"));

    value.assign(LStringDetail::wstring_view(L"wide"));
    REQUIRE(value == "wide");

#if LSTRING_HAS_MAGIC_ENUM
    value.assign(LStringTestColor::Red);
    REQUIRE(value == "Red");
#endif // LSTRING_HAS_MAGIC_ENUM

    value.assign(42);
    REQUIRE(value == fmt::format("{}", 42));

    REQUIRE_NOTHROW(value.assign(static_cast<const char*>(nullptr)));
    REQUIRE(value.empty());
    REQUIRE_NOTHROW(value.assign(static_cast<const char*>(nullptr), 0));
    REQUIRE(value.empty());
    REQUIRE_THROWS_AS(value.assign(static_cast<const char*>(nullptr), 1), std::invalid_argument);
    REQUIRE_THROWS_AS(value.assign(std::string_view("abc"), 4), std::out_of_range);
}

TEST_CASE("LString supports common input styles", "[LString][input]") {
    {
        std::istringstream input("alpha beta");
        LString token;

        input >> token;
        REQUIRE(token == "alpha");

        input >> token;
        REQUIRE(token == "beta");
        REQUIRE_FALSE(input.fail());
    }

    {
        std::istringstream input("first line\nsecond line");
        LString line;

        getline(input, line);
        REQUIRE(line == "first line");

        line.read_line(input);
        REQUIRE(line == "second line");
    }

    {
        std::istringstream input("a,b,c");
        LString field;

        getline(input, field, ',');
        REQUIRE(field == "a");

        field.read_line(input, ',');
        REQUIRE(field == "b");

        REQUIRE(LString::line_from(input, ',') == "c");
    }

    {
        std::istringstream input("all\nremaining bytes");
        LString text;

        text.read_all(input);
        REQUIRE(text == "all\nremaining bytes");
    }

    {
        std::istringstream input("static all");
        REQUIRE(LString::all_from(input) == "static all");
    }
}

TEST_CASE("LString formats and parses numbers", "[LString]") {
    auto formatted = LString::format("{}:{}:{:.1f}", "id", 42, 3.5);

    REQUIRE(formatted == "id:42:3.5");
#if LSTRING_HAS_STD_FORMAT
    REQUIRE(std::format("{}:{}", LString("std"), 42) == "std:42");
    REQUIRE(LString::std_format("{}:{}", LString("std"), 42) == "std:42");

    LString appended = "prefix";
    appended.append_std_format("-{}", 7);
    REQUIRE(appended == "prefix-7");
#endif // LSTRING_HAS_STD_FORMAT
    REQUIRE(LString(" 2a ").to_int(16) == 42);
    REQUIRE(LString(" 3.25 ").to_double() == 3.25);
    REQUIRE_FALSE(LString("3.25x").to_double().has_value());
    REQUIRE(LString::from_number(1234) == "1234");
}

#if LSTRING_HAS_STD_OPTIONAL
namespace {
    // SFINAE 探测 to_number<T> 是否满足约束。
    template<class T, class = void>
    struct lstring_has_to_number : std::false_type {};

    template<class T>
    struct lstring_has_to_number<
        T,
        std::void_t<decltype(std::declval<const LString&>().template to_number<T>())>>
        : std::true_type {};
} // namespace

TEST_CASE("LString to_number rejects bool and parses integers", "[LString]") {
    // std::from_chars 不支持 bool，约束必须拒绝 bool。
    static_assert(!lstring_has_to_number<bool>::value,
                  "to_number<bool> must not be instantiable");
    static_assert(lstring_has_to_number<int>::value);
    static_assert(lstring_has_to_number<long long>::value);

    REQUIRE(LString("42").to_number<int>() == 42);
    REQUIRE(LString(" ff ").to_number<int>(16) == 255);
    REQUIRE(LString("-7").to_number<long long>() == -7);
    REQUIRE_FALSE(LString("42x").to_number<int>().has_value());
    REQUIRE_FALSE(LString("").to_number<int>().has_value());
}
#endif // LSTRING_HAS_STD_OPTIONAL

#if LSTRING_HAS_MAGIC_ENUM
TEST_CASE("LString serializes and deserializes enums with magic_enum", "[LString][enum]") {
    LString value = LStringTestColor::Green;

    REQUIRE(value == "Green");
    REQUIRE(LString::from_enum(LStringTestColor::Red) == "Red");
    REQUIRE(LString::enum_name(LStringTestColor::Blue) == "Blue");

    value = LStringTestColor::Blue;
    REQUIRE(value == "Blue");

    REQUIRE(LString(" Green ").to_enum<LStringTestColor>().value() == LStringTestColor::Green);
    REQUIRE(LString("green").to_enum<LStringTestColor>(false).value() == LStringTestColor::Green);
    REQUIRE_FALSE(LString("green").to_enum<LStringTestColor>().has_value());
    REQUIRE(LString("missing").to_enum_or<LStringTestColor>(LStringTestColor::Red) ==
            LStringTestColor::Red);
    REQUIRE(LString::to_enum_checked<LStringTestColor>("Blue") == LStringTestColor::Blue);
    REQUIRE_THROWS_AS(LString::to_enum_checked<LStringTestColor>("Missing"), std::runtime_error);

    REQUIRE(LString::is_enum_name<LStringTestColor>("red", false));
    REQUIRE(LString("Blue").is_enum_name<LStringTestColor>());
    REQUIRE(LString::enum_type_name<LStringTestColor>().contains("LStringTestColor"));
    REQUIRE(LString::enum_names<LStringTestColor>() ==
            std::vector<LString> {"Red", "Green", "Blue"});
    REQUIRE(LString::enum_values<LStringTestColor>() ==
            std::vector<LStringTestColor> {
                LStringTestColor::Red, LStringTestColor::Green, LStringTestColor::Blue});

    auto entries = LString::enum_entries<LStringTestColor>();
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[1].first == LStringTestColor::Green);
    REQUIRE(entries[1].second == "Green");
}
#endif // LSTRING_HAS_MAGIC_ENUM

TEST_CASE("LString stringifies fmt formattable objects", "[LString][fmt]") {
    std::vector<int> numbers {1, 2, 3};
    std::map<std::string, int> counts {{"one", 1}, {"two", 2}};
    std::optional<int> maybe {7};

    LString from_number = 42;
    LString from_vector = numbers;
    LString from_map(counts);
    LString from_optional = maybe;

    REQUIRE(from_number == fmt::format("{}", 42));
    REQUIRE(from_vector == fmt::format("{}", numbers));
    REQUIRE(from_map == fmt::format("{}", counts));
    REQUIRE(from_optional == fmt::format("{}", maybe));

    from_vector = std::tuple {1, std::string("two")};
    REQUIRE(from_vector == fmt::format("{}", std::tuple {1, std::string("two")}));
}

TEST_CASE("LString supports std ranges", "[LString][ranges]") {
    std::array<char, 3> abc {'a', 'b', 'c'};
    std::vector<char> de {'d', 'e'};

    LString value(abc);
    REQUIRE(value == "abc");

    value.append_range(de);
    REQUIRE(value == "abcde");
    REQUIRE(std::ranges::equal(value, std::string_view("abcde")));
    REQUIRE(std::ranges::equal(value.bytes(), std::string_view("abcde")));

    value.assign_range(std::views::single('z'));
    REQUIRE(value == "z");

    auto from_range = LString::from_range(std::views::single('x'));
    REQUIRE(from_range == "x");
    REQUIRE(std::ranges::equal(std::views::reverse(LString("abc")), std::string_view("cba")));
}

TEST_CASE("LString converts Unicode encodings and wide strings", "[LString][unicode]") {
    LString text = std::string("hello, \xE4\xB8\x96\xE7\x95\x8C", 13);

    auto utf8_bom = text.to_encoding(LEncoding::Utf8Bom, true);
    REQUIRE(LString::detect_encoding(utf8_bom) == LEncoding::Utf8Bom);
    REQUIRE(LString::from_encoding(utf8_bom, LEncoding::Utf8Bom) == text);

    auto utf16le = text.to_encoding(LEncoding::Utf16Le, true);
    REQUIRE(LString::detect_encoding(utf16le) == LEncoding::Utf16Le);
    REQUIRE(LString::from_encoding(utf16le, LEncoding::Utf16Le) == text);

    auto utf16be = text.to_encoding(LEncoding::Utf16Be, true);
    REQUIRE(LString::detect_encoding(utf16be) == LEncoding::Utf16Be);
    REQUIRE(LString::from_encoding(utf16be, LEncoding::Utf16Be) == text);

    auto utf32le = text.to_encoding(LEncoding::Utf32Le, true);
    REQUIRE(LString::detect_encoding(utf32le) == LEncoding::Utf32Le);
    REQUIRE(LString::from_encoding(utf32le, LEncoding::Utf32Le) == text);

    REQUIRE(LString::from_wstring(text.to_wstring()) == text);
    REQUIRE(LString::from_encoding(std::string_view("\xE9", 1), LEncoding::Latin1) ==
            LString(std::string("\xC3\xA9", 2)));
    REQUIRE_THROWS_AS(LString::from_encoding(std::string_view("\xFF", 1), LEncoding::Utf8),
                      std::runtime_error);
}

TEST_CASE("LString cleans invalid UTF-8 in non-strict output conversions", "[LString][unicode]") {
    const LString invalid(std::string("\xC3\x28", 2));
    const std::string replacement("\xEF\xBF\xBD\x28", 4);
    REQUIRE(invalid.to_encoding(LEncoding::Utf8, false, false) == replacement);
    REQUIRE(invalid.to_encoding(LEncoding::Unknown, false, false) == replacement);
    REQUIRE(invalid.to_encoding(LEncoding::Utf8Bom, true, false) ==
            std::string("\xEF\xBB\xBF") + replacement);
}

TEST_CASE("LString converts GBK and GB2312 through platform codec backend", "[LString][gbk]") {
    const std::string gb_hello = std::string("\xC4\xE3\xBA\xC3", 4);
    const LString utf8_hello = std::string("\xE4\xBD\xA0\xE5\xA5\xBD", 6);

    REQUIRE(LString::from_gbk(gb_hello) == utf8_hello);
    REQUIRE(LString::from_gb2312(gb_hello) == utf8_hello);
    REQUIRE(utf8_hello.to_gbk() == gb_hello);
    REQUIRE(utf8_hello.to_gb2312() == gb_hello);

    auto detected = LString::detect_encoding(gb_hello);
    REQUIRE((detected == LEncoding::Gb2312 || detected == LEncoding::Gbk));
    REQUIRE(LString::encoding_name(LEncoding::Gbk) == "gbk"sv);
    REQUIRE(LString::encoding_name(LEncoding::Gb2312) == "gb2312"sv);
}

TEST_CASE("LString supports regex find and replace", "[LString][regex]") {
    LString text = "abc123def456";

    REQUIRE(text.regex_contains("[0-9]+"));
    REQUIRE(text.regex_find("[0-9]+").value() == "123");
    REQUIRE(text.regex_find_all("[0-9]+") == std::vector<LString> {"123", "456"});
    REQUIRE(text.regex_replaced("[0-9]+", "#") == "abc#def#");
    REQUIRE(text.regex_replaced("[0-9]+", "#", false) == "abc#def456");
}

TEST_CASE("LString supports paths, hashes, and user literals", "[LString]") {
    LString path = std::filesystem::path("root") / "child" / "file.txt";

    REQUIRE(path.filename() == "file.txt");
    REQUIRE(path.stem() == "file");
    REQUIRE(path.extension() == ".txt");
    REQUIRE((LString("root") / "child").normalized_path().contains("child"));

    std::unordered_set<LString> values;
    values.insert("alpha"_ls);
    values.insert(LString("alpha"));
    values.insert("beta");

    REQUIRE(values.size() == 2);
    REQUIRE(values.contains("alpha"));
    REQUIRE(values.contains("beta"));
}

TEST_CASE("LString supports Base64, MD5, and SHA-256 helpers", "[LString][hash]") {
    REQUIRE(LString("hello").base64_encoded() == "aGVsbG8=");
    REQUIRE(LString::base64_encode("foobar") == "Zm9vYmFy");
    REQUIRE(LTool::detail::base64_encode("foobar", 6) == "Zm9vYmFy");
    REQUIRE(LString::from_base64("aGVsbG8=") == "hello");
    REQUIRE(LTool::detail::base64_decode("aGVsbG8=", 8) == "hello");
    REQUIRE(LString("YQBi").base64_decoded() == LString(std::string("a\0b", 3)));
    REQUIRE(LString::from_base64("aGVs bG8=\n") == "hello");
    REQUIRE_THROWS_AS(LString::from_base64("@@@"), std::runtime_error);

    REQUIRE(LString("").md5() == "d41d8cd98f00b204e9800998ecf8427e");
    REQUIRE(LString("abc").md5() == "900150983cd24fb0d6963f7d28e17f72");
    REQUIRE(LString::md5("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    REQUIRE(LTool::detail::md5_hex("message digest", 14) ==
            "f96b697d7cb7938d525a2f31aaf161d0");

    auto digest = LString("abc").md5_digest();
    REQUIRE(digest[0] == 0x90);
    REQUIRE(digest[15] == 0x72);

    REQUIRE(LString("").sha256() ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(LString("abc").sha256() ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(LString::sha256("message digest") ==
            "f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650");

    auto sha_digest = LString("abc").sha256_digest();
    REQUIRE(sha_digest[0] == 0xba);
    REQUIRE(sha_digest[31] == 0xad);
    REQUIRE_THROWS_AS(LTool::detail::sha256_hex(nullptr, 1), std::invalid_argument);
    REQUIRE_NOTHROW(LTool::detail::sha256_hex(nullptr, 0));
}

TEST_CASE("LString replace_all safely accepts views into itself", "[LString]") {
    LString whole = "x";
    whole.replace_all(whole.view(), "z");
    REQUIRE(whole == "z");

    LString replacement = "aba";
    replacement.replace_all("a", replacement.view());
    REQUIRE(replacement == "abababa");
}

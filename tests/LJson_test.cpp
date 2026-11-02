#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "LJson.hpp"
#include "LString.hpp"

namespace {

/// 生成唯一的临时文件路径，并在析构时删除（RAII 清理）。
struct TempFileGuard {
    std::string path;

    TempFileGuard(const char* suffix) {
        static std::atomic<unsigned long> counter{0};
        const auto stamp = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        const auto name = "ltool_" + std::string(suffix) + "_" +
                          std::to_string(stamp) + "_" +
                          std::to_string(counter.fetch_add(1));
        path = (std::filesystem::temp_directory_path() / name).string();
    }

    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

struct LJsonTestPerson {
    std::string name;
    int age = 0;
};

TEST_CASE("LJson exposes backend state without forcing a Json dependency", "[LJson]") {
    REQUIRE(LJson::backend_name() != nullptr);
    REQUIRE(LJson::has_static_reflection() == (LJSON_HAS_STATIC_REFLECTION != 0));

    LJson::Json text = "{\"name\":\"Ada\",\"age\":37}";
    LJson::JsonView view = text;

    REQUIRE(view.size() == text.size());
    REQUIRE(LJson::to_string(view) == text.str());
}

TEST_CASE("LJson accepts LString as JSON text", "[LJson][LString]") {
    LString text = "{\"name\":\"Ada\",\"age\":37}";
    LJson::Json json_text{text};
    LJson::JsonView view{text};

    REQUIRE(json_text.str() == text.str());
    REQUIRE(view.size() == text.size());
    REQUIRE(LJson::to_string(view) == text.str());
}

#if LJSON_HAS_NLOHMANN_JSON
TEST_CASE("LJson converts nlohmann Json", "[LJson][nlohmann]") {
    auto value = LJson::parse_nlohmann("{\"name\":\"Ada\",\"age\":37}");
    LJson::Json text = value;
    auto reparsed = LJson::to_nlohmann(text);

    REQUIRE(reparsed["name"] == "Ada");
    REQUIRE(reparsed["age"] == 37);
}
#endif

TEST_CASE("Json supports object and array CRUD without exposing backend types", "[LJson][dom]") {
    LJson::Json json = R"({"user":{"name":"Ada","tags":["math"]},"active":true})";

    json["user"]["age"] = 37;
    json["user"]["tags"].push_back("code");
    json["active"] = false;
    REQUIRE(json.contains("user"));
    REQUIRE(json.count("user") == 1);
    REQUIRE(json["user"]["name"] == "Ada");
    REQUIRE(json["user"]["age"] == 37);
    REQUIRE(json["user"]["tags"][1] == "code");
    REQUIRE(json.erase("active") == 1);
    REQUIRE_FALSE(json.contains("active"));

#if LJSON_HAS_NLOHMANN_JSON
    auto reparsed = LJson::to_nlohmann(json);
    REQUIRE(reparsed["user"]["age"] == 37);
    REQUIRE(reparsed["user"]["tags"].size() == 2);
    REQUIRE_FALSE(reparsed.contains("active"));
#endif

    static_assert(std::is_same<decltype(json["user"]), LJson::JsonRef>::value,
                  "LJson::Json::operator[] must return LJson::JsonRef, not backend JSON types");
    static_assert(std::is_base_of<LJson::Json, LJson::JsonRef>::value,
                  "LJson::JsonRef must be usable wherever LJson::Json is accepted");
}

TEST_CASE("Json supports recursive pointer-style nested values", "[LJson][dom]") {
    auto json = LJson::Json::object();

    json.set_pointer("/user/profile/name", "Grace");
    json.set_pointer("/user/profile/age", 41);
    REQUIRE(json.contains_pointer("/user/profile/name"));
    REQUIRE(json.value_pointer<std::string>("/user/profile/name", "") == "Grace");
    REQUIRE(json.at_pointer("/user/profile/age") == 41);

    REQUIRE(json.erase_pointer("/user/profile/age"));
    REQUIRE_FALSE(json.contains_pointer("/user/profile/age"));
    REQUIRE_FALSE(json.erase_pointer("/user/profile/missing"));
}

TEST_CASE("Json validates numeric conversions and compares integers exactly", "[LJson][safety]") {
    LJson::Json huge_real = 1e300;
    REQUIRE_THROWS_AS(huge_real.get_as<int>(), std::domain_error);
    REQUIRE_THROWS_AS(huge_real.get_as<unsigned long long>(), std::domain_error);
    REQUIRE_THROWS_AS(LJson::Json(-1).get_as<unsigned>(), std::domain_error);
    REQUIRE(LJson::Json(3.75).get_as<int>() == 3);

    constexpr long long large = 9007199254740993LL;
    REQUIRE(LJson::Json(large) == large);
    REQUIRE_FALSE(LJson::Json(large) == 9007199254740992LL);
    REQUIRE_FALSE(LJson::Json(-1) == std::numeric_limits<unsigned long long>::max());
}

TEST_CASE("Json at and at_pointer do not create missing values", "[LJson][safety]") {
    auto object = LJson::Json::object();
    REQUIRE_THROWS_AS(object.at("missing"), std::out_of_range);
    REQUIRE_FALSE(object.contains("missing"));

    auto array = LJson::Json::array();
    array.push_back(1);
    REQUIRE_THROWS_AS(array.at(1), std::out_of_range);
    REQUIRE(array.json_size() == 1);
    REQUIRE_THROWS_AS(array.at_pointer("/-"), std::out_of_range);
    REQUIRE(array.json_size() == 1);
    REQUIRE_THROWS_AS(array[std::numeric_limits<std::size_t>::max()], std::length_error);
}

TEST_CASE("Json to_string returns a validated owning snapshot", "[LJson][safety]") {
    LJson::Json invalid = "{invalid";
    REQUIRE_THROWS_AS(invalid.to_string(), std::runtime_error);
}

TEST_CASE("Json can find and erase keys recursively", "[LJson][dom]") {
    LJson::Json json = R"({
        "id": 1,
        "child": {"id": 2, "name": "child"},
        "items": [{"id": 3}, {"id": 4, "nested": {"id": 5}}]
    })";

    auto first_id = json.find_recursive("id");
    REQUIRE(first_id.exists());
    REQUIRE(first_id == 1);

    auto all_ids = json.find_all_recursive("id");
    REQUIRE(all_ids.size() == 5);

    REQUIRE(json.erase_recursive("id") == 5);
    REQUIRE_FALSE(json.find_recursive("id").exists());
    REQUIRE(json["child"]["name"] == "child");
}

TEST_CASE("Json formats through fmt, ostream, and std::format", "[LJson][format]") {
    LJson::Json json = R"({"id":1,"name":"Json"})";

    REQUIRE(fmt::format("{}", json["id"]) == "1");
    REQUIRE(fmt::format("{}", json["name"]) == "\"Json\"");
    REQUIRE(json["id"].to_string() == "1");

    std::ostringstream out;
    out << json["id"];
    REQUIRE(out.str() == "1");

#if LJSON_HAS_STD_FORMAT
    REQUIRE(std::format("{}", json["id"]) == "1");
#endif
}

TEST_CASE("Json dump uses the requested indentation width", "[LJson][format]") {
    LJson::Json json = R"({"user":{"name":"Ada","active":true},"tags":["math","code"],"empty":{}})";

    const auto compact = json.dump();
    const auto zero_indent = json.dump(0);
    const auto two_spaces = json.dump(2);
    const auto three_spaces = json.dump(3);
    const auto four_spaces = json.dump(4);

    REQUIRE(compact.find('\n') == std::string::npos);
    REQUIRE(zero_indent == compact);
    REQUIRE(two_spaces.find("\n  \"user\"") != std::string::npos);
    REQUIRE(two_spaces.find("\n    \"name\"") != std::string::npos);
    REQUIRE(three_spaces.find("\n   \"user\"") != std::string::npos);
    REQUIRE(three_spaces.find("\n      \"name\"") != std::string::npos);
    REQUIRE(four_spaces.find("\n    \"user\"") != std::string::npos);
    REQUIRE(four_spaces.find("\n        \"name\"") != std::string::npos);
    REQUIRE(two_spaces != four_spaces);
    REQUIRE(three_spaces != two_spaces);
    REQUIRE(three_spaces != four_spaces);
}

#if LJSON_HAS_JSONCPP
TEST_CASE("LJson converts JsonCpp values", "[LJson][jsoncpp][cppjson]") {
    auto value = LJson::parse_jsoncpp("{\"name\":\"Ada\",\"age\":37}");
    LJson::Json text = value;
    auto reparsed = LJson::parse_cppjson(text);

    REQUIRE(reparsed["name"].asString() == "Ada");
    REQUIRE(reparsed["age"].asInt() == 37);
}
#endif

#if LJSON_HAS_SIMDJSON
TEST_CASE("LJson converts simdjson documents", "[LJson][simdjson]") {
    auto doc = LJson::parse_simdjson("{\"name\":\"Ada\",\"age\":37}");
    LJson::Json text = doc;

    REQUIRE(text.str().find("Ada") != std::string::npos);
    REQUIRE(LJson::to_string(doc.root()).find("Ada") != std::string::npos);
}
#endif

#if LJSON_HAS_YYJSON
TEST_CASE("LJson converts yyjson documents", "[LJson][yyjson]") {
    auto doc = LJson::parse_yyjson("{\"name\":\"Ada\",\"age\":37}");
    LJson::Json text = doc;

    REQUIRE(doc.root() != nullptr);
    REQUIRE(text.str().find("Ada") != std::string::npos);
}
#endif

#if LJSON_HAS_STATIC_REFLECTION
TEST_CASE("LJson serializes through rfl Json when available", "[LJson][rfl]") {
    LJsonTestPerson person;
    person.name = "Ada";
    person.age = 37;

    auto json_text = LJson::write(person);
    auto parsed = LJson::read_or_throw<LJsonTestPerson>(json_text);

    REQUIRE(json_text.find("Ada") != std::string::npos);
    REQUIRE(parsed.name == "Ada");
    REQUIRE(parsed.age == 37);
}

TEST_CASE("Json exposes rfl from and to helpers", "[LJson][rfl]") {
    LJsonTestPerson person;
    person.name = "Grace";
    person.age = 41;

    auto text = LJson::Json::from(person);
    auto parsed = text.to<LJsonTestPerson>();
    auto parsed_from_free_function = LJson::to<LJsonTestPerson>(text);

    REQUIRE(text.str().find("Grace") != std::string::npos);
    REQUIRE(parsed.name == "Grace");
    REQUIRE(parsed.age == 41);
    REQUIRE(parsed_from_free_function.name == "Grace");
    REQUIRE(parsed_from_free_function.age == 41);
}

TEST_CASE("LJson from_file/to_file round-trips DOM and reflected structs", "[LJson][file]") {
    TempFileGuard guard("json");
    const std::string& tmp = guard.path;

    // Json DOM 往返。
    auto json = LJson::Json::object();
    json["name"] = "fileio";
    json["age"] = 33;
    LJson::to_file(tmp, json);
    const auto loaded = LJson::from_file(tmp);
    REQUIRE(loaded["name"].as_string() == "fileio");
    REQUIRE(loaded["age"].as_int() == 33);

    // 反射结构体往返。
    LJsonTestPerson person;
    person.name = "fileio";
    person.age = 33;
    LJson::to_file(tmp, person);
    const auto loaded_struct = LJson::from_file<LJsonTestPerson>(tmp);
    REQUIRE(loaded_struct.name == "fileio");
    REQUIRE(loaded_struct.age == 33);
}
#endif

TEST_CASE("Json copies are independent values", "[LJson][semantics]") {
    auto root = LJson::parse(R"({"x":1,"y":{"z":2},"arr":[1,2]})");

    // 显式声明为 Json：得到独立副本，修改不影响 root。
    LJson::Json y = root["y"];
    y["z"] = 7;
    REQUIRE(root["y"]["z"] == 2);
    REQUIRE(y["z"] == 7);

    // 赋值给已有 Json 同样是深拷贝。
    LJson::Json arr;
    arr = root["arr"];
    arr.push_back(3);
    REQUIRE(root["arr"].dump() == "[1,2]");
    REQUIRE(arr.dump() == "[1,2,3]");

    // 放入容器（移动构造路径）也不会变成别名。
    std::vector<LJson::Json> values;
    values.push_back(root["arr"]);
    values.emplace_back(root["y"]);
    values[0].push_back(9);
    values[1]["z"] = 0;
    REQUIRE(root.dump() == R"({"x":1,"y":{"z":2},"arr":[1,2]})");

    // 根节点之间的拷贝仍是深拷贝。
    LJson::Json copy = root;
    copy["x"] = 100;
    REQUIRE(root["x"] == 1);
}

TEST_CASE("JsonRef writes through to the owning document", "[LJson][semantics]") {
    auto root = LJson::parse(R"({"x":1,"y":{"z":2},"arr":[1,2]})");

    auto x = root["x"];
    static_assert(std::is_same<decltype(x), LJson::JsonRef>::value,
                  "auto 推导出 JsonRef，表明它是引用");
    x = 99;
    REQUIRE(root["x"] == 99);

    // 复制 JsonRef 得到指向同一节点的引用。
    auto x2 = x;
    x2 = 5;
    REQUIRE(root["x"] == 5);

    // JsonRef = JsonRef 写入值，不改变引用目标。
    root["y"]["n"] = root["x"];
    root["x"] = 6;
    REQUIRE(root["y"]["n"] == 5);
    REQUIRE(root["x"] == 6);

    // 把 Json 写入 JsonRef：深拷贝其值。
    LJson::Json patch = LJson::Json::object();
    patch["k"] = "v";
    root["patch"] = patch;
    patch["k"] = "changed";
    REQUIRE(root["patch"]["k"] == "v");

    // 可写遍历接口返回 JsonRef。
    root.find_recursive("z") = 20;
    REQUIRE(root["y"]["z"] == 20);
    root.at_pointer("/arr/0") = 10;
    REQUIRE(root["arr"][0] == 10);

    auto last = root["arr"].emplace_back(3);
    last = 30;
    REQUIRE(root["arr"].dump() == "[10,2,30]");

    root.for_each_recursive([](auto&& node) {
        if (node.is_number() && node == 2) {
            node = 200;
        }
    });
    REQUIRE(root["arr"][1] == 200);
}

TEST_CASE("Json const access returns copies and JsonRef formats like Json", "[LJson][semantics]") {
    auto root = LJson::parse(R"({"x":1})");
    const auto& view = root;
    auto copy = view["x"];
    static_assert(std::is_same<decltype(copy), LJson::Json>::value,
                  "const 访问返回独立 Json");
    copy = 2;
    REQUIRE(root["x"] == 1);

    REQUIRE(fmt::format("{}", root["x"]) == "1");
    REQUIRE(root["x"] == root["x"]);
}

TEST_CASE("JsonRef to the root replaces the owner's value", "[LJson][semantics]") {
    auto root = LJson::parse(R"({"a":1})");
    auto whole = root.pointer("");
    whole = 5;
    REQUIRE(root.dump() == "5");
}

TEST_CASE("Json accepts any integer type as array index", "[LJson][semantics]") {
    auto json = LJson::parse("[1,2,3]");
    unsigned u = 1;
    long l = 2;
    short sh = 2;
    json[0] = 7;   // 字面量 0 不再与 const char* 键歧义
    json[u] = 11;
    json[l] = 20;
    json[sh] = 22;
    REQUIRE(json.dump() == "[7,11,22]");

    const auto& view = json;
    REQUIRE(view[0] == 7);
    REQUIRE_THROWS_AS(view[-1], std::out_of_range);
    REQUIRE_THROWS_AS(json[-1], std::out_of_range);
}

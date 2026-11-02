#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <vector>

#include "LPath.hpp"
#include "LFile.hpp"
#include "LRange.hpp"

namespace {

struct TempTree {
    LPath root;

    TempTree()
        : root(LPath::temp_path("LPath-test-", "")) {
        root.create_directories();
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root.path(), ec);
    }
};

bool contains_name(const std::vector<LPath>& paths, LString name) {
    return std::any_of(paths.begin(), paths.end(), [&](const LPath& path) {
        return path.filename().str() == name;
    });
}

void write_file(const LPath& path, LStringDetail::string_view text) {
    LFile(path).write_text(text);
}

} // namespace

TEST_CASE("LPath handles path helpers", "[LPath]") {
    TempTree tmp;
    auto file = tmp.root / "notes.md";

    REQUIRE(file.filename() == LPath("notes.md"));
    REQUIRE(file.stem() == LPath("notes"));
    REQUIRE(file.extension() == LPath(".md"));
    REQUIRE(file.with_extension(".txt").filename() == LPath("notes.txt"));
    REQUIRE((tmp.root / "." / "child" / "..").normalized().str().contains("LPath-test-"));
}

TEST_CASE("LPath lists, filters, and globs directory entries", "[LPath]") {
    TempTree tmp;
    auto src = tmp.root / "src";
    (src / "sub").create_directories();
    write_file(src / "a.cpp", "a");
    write_file(src / "b.hpp", "b");
    write_file(src / ".hidden.cpp", "h");
    write_file(src / "sub" / "c.cpp", "c");

    auto direct_cpp = src.files(".cpp", false);
    REQUIRE(direct_cpp.size() == 1);
    REQUIRE(direct_cpp[0].filename() == LPath("a.cpp"));

    auto recursive_cpp = src.recursive_files(".cpp", false);
    REQUIRE(recursive_cpp.size() == 2);
    REQUIRE(contains_name(recursive_cpp, "a.cpp"));
    REQUIRE(contains_name(recursive_cpp, "c.cpp"));

    auto headers = src.glob("*.hpp");
    REQUIRE(headers.size() == 1);
    REQUIRE(headers[0].filename() == LPath("b.hpp"));

    auto dirs = src.directories();
    REQUIRE(dirs.size() == 1);
    REQUIRE(dirs[0].filename() == LPath("sub"));

    auto dir_entries = src.directories();
    auto dir_names = dir_entries
                   | LRange::transform([](auto& path) { return path.filename().str(); })
                   | LRange::to_vector();
    REQUIRE(dir_names == std::vector<LString> {"sub"});
}

TEST_CASE("LPath copies, moves, removes, formats, and hashes", "[LPath]") {
    TempTree tmp;
    auto from = tmp.root / "from.txt";
    auto copied = tmp.root / "copied.txt";
    auto moved = tmp.root / "moved.txt";

    write_file(from, "payload");
    REQUIRE(from.copy_file_to(copied.path()));
    REQUIRE(LFile(copied).read_text() == "payload");

    auto old_copied_path = copied.path();
    copied.move_to(moved.path());
    REQUIRE_FALSE(LPath::exists(old_copied_path));
    REQUIRE(moved.exists());
    REQUIRE(copied == moved);
    REQUIRE(LFile(moved).read_text() == "payload");

    REQUIRE(fmt::format("{}", moved).find("moved.txt") != std::string::npos);

    std::unordered_set<LPath> paths;
    paths.insert("a.txt"_lp);
    paths.insert(LPath("a.txt"));
    paths.insert(LPath("b.txt"));
    REQUIRE(paths.size() == 2);

    REQUIRE(moved.remove());
    REQUIRE_FALSE(moved.exists());
}

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
    (src / ".hidden_dir").create_directories();
    write_file(src / "a.cpp", "a");
    write_file(src / "b.hpp", "b");
    write_file(src / ".hidden.cpp", "h");
    write_file(src / ".hidden_dir" / "secret.cpp", "s");
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

    auto dirs = src.directories(false);
    REQUIRE(dirs.size() == 1);
    REQUIRE(dirs[0].filename() == LPath("sub"));

    auto all_dirs = src.directories();
    REQUIRE(all_dirs.size() == 2);

    auto dir_entries = src.directories(false);
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

TEST_CASE("LPath glob_recursive supports ** wildcards and LTempDir cleans up", "[LPath]") {
    std::filesystem::path scoped_path;
    {
        LTempDir tmp("lpath_glob_");
        scoped_path = tmp.path().path();
        auto a = tmp.path() / "src" / "core" / "a.cpp";
        auto b = tmp.path() / "src" / "b.cpp";
        auto h = tmp.path() / "src" / "core" / "a.hpp";
        auto hidden = tmp.path() / ".git" / "ignored.cpp";
        write_file(a, "a");
        write_file(b, "b");
        write_file(h, "h");
        write_file(hidden, "hidden");

        auto all_cpp = tmp.path().glob_recursive("src/**/*.cpp", LPathEntryKind::File);
        REQUIRE(all_cpp.size() == 2);

        auto deep_only = tmp.path().glob_recursive("src/*/*.cpp", LPathEntryKind::File);
        REQUIRE(deep_only.size() == 1);
        REQUIRE(deep_only[0].filename() == "a.cpp"_lp);

        auto no_hidden = LPath::glob_path_recursive(tmp.path().path() / "**/*.cpp", LPathEntryKind::File, false);
        REQUIRE(no_hidden.size() == 2);
    }
    REQUIRE_FALSE(std::filesystem::exists(scoped_path));
}

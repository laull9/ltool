#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "LFile.hpp"

#if !LTOOL_PLATFORM_WINDOWS
#include <sys/stat.h>
#endif

namespace {

struct TempTree {
    LPath root;

    TempTree()
        : root(LPath::temp_path("LFile-test-", "")) {
        root.create_directories();
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root.path(), ec);
    }
};

} // namespace

TEST_CASE("LFile reads and writes bytes and text", "[LFile]") {
    TempTree tmp;
    LFile file = tmp.root / "nested" / "hello.txt";

    file.write_text("hello");
    file.append_text(", world");
    REQUIRE(file.read_text() == "hello, world");
    REQUIRE(file.size() == 12);
    REQUIRE(file.exists());
    REQUIRE(file.is_file());
    REQUIRE(LPath(file.path()).parent_path().is_dir());

    file.write_bytes(LStringDetail::string_view("a\0b", 3));
    REQUIRE(file.read_bytes() == std::string("a\0b", 3));

    file.write_text("hello", LEncoding::Utf16Le, true);
    REQUIRE(file.read_text() == "hello");
}

TEST_CASE("LFile handles line helpers", "[LFile]") {
    TempTree tmp;
    LFile file = tmp.root / "notes.md";

    file.write_lines(std::vector<LString> {"alpha", "beta"}, "\n", true);

    REQUIRE(file.read_lines() == std::vector<LString> {"alpha", "beta", ""});
}

TEST_CASE("LFile finds and replaces text and bytes", "[LFile]") {
    TempTree tmp;
    LFile file = tmp.root / "replace.txt";

    file.write_text("foo 123 foo 456");
    REQUIRE(file.contains("123"));
    REQUIRE(file.find("foo") == 0);
    REQUIRE(file.rfind("foo") == 8);
    REQUIRE(file.replaced_all("foo", "bar") == "bar 123 bar 456");

    file.replace_all("foo", "bar");
    REQUIRE(file.read_text() == "bar 123 bar 456");

    REQUIRE(file.regex_contains("[0-9]+"));
#if LSTRING_HAS_STD_OPTIONAL
    REQUIRE(file.regex_find("[0-9]+").value() == "123");
#endif
    REQUIRE(file.regex_find_all("[0-9]+") == std::vector<LString> {"123", "456"});
    REQUIRE(file.regex_replaced("[0-9]+", "#") == "bar # bar #");

    file.regex_replace("[0-9]+", "#", false);
    REQUIRE(file.read_text() == "bar # bar 456");

    file.write_bytes("ababa");
    REQUIRE(file.bytes_contains("bab"));
    REQUIRE(file.bytes_find("ba") == 1);
    file.replace_bytes_all("ba", "XY");
    REQUIRE(file.read_bytes() == "aXYXY");
}

TEST_CASE("LFile supports fstream pointer helpers", "[LFile]") {
    TempTree tmp;
    LFile file = tmp.root / "seek.bin";

    file.write_bytes("abcdef");
    REQUIRE(file.read_bytes_at(2, 3) == "cde");
    REQUIRE(file.read_bytes_from(-2, std::ios::end) == "ef");

    file.write_bytes_at(1, "XYZ");
    REQUIRE(file.read_bytes() == "aXYZef");

    auto stream = file.open_stream();
    LFile::seekg(stream, 1);
    char ch = '\0';
    stream.get(ch);
    REQUIRE(ch == 'X');
    REQUIRE(LFile::tellg(stream) == std::streampos(2));

    LFile::seekp(stream, 0);
    stream.put('A');
    stream.flush();
    REQUIRE(file.read_bytes() == "AXYZef");
}

TEST_CASE("LFile touches, copies, moves, removes, formats, and hashes", "[LFile]") {
    TempTree tmp;
    LFile from = tmp.root / "from.txt";
    LFile copied = tmp.root / "copied.txt";
    LFile moved = tmp.root / "moved.txt";

    from.write_text("payload");
    REQUIRE(from.copy_to(copied.path()));
    REQUIRE(copied.read_text() == "payload");

    auto old_copied_path = copied.path();
    copied.move_to(moved.path());
    REQUIRE_FALSE(LPath::exists(old_copied_path));
    REQUIRE(moved.exists());
    REQUIRE(copied == moved);
    REQUIRE(moved.read_text() == "payload");

    REQUIRE(fmt::format("{}", moved).find("moved.txt") != std::string::npos);
    REQUIRE(moved.path_hash() == LPath(moved.path()).hash());
    REQUIRE(std::hash<LFile> {}(moved) == moved.path_hash());
    REQUIRE(moved.sha256() ==
            "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5");

    std::unordered_set<LFile> files;
    files.insert("a.txt"_lf);
    files.insert(LFile("a.txt"));
    files.insert(LFile("b.txt"));
    REQUIRE(files.size() == 2);

    REQUIRE(moved.remove());
    REQUIRE_FALSE(moved.exists());
}

TEST_CASE("LFile hashes file content with SHA-256", "[LFile]") {
    TempTree tmp;
    LFile empty = tmp.root / "empty.bin";
    LFile first = tmp.root / "first.bin";
    LFile second = tmp.root / "second.bin";

    empty.touch();
    first.write_text("abc");
    second.write_text("abc");

    REQUIRE(empty.sha256() ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(first.sha256() ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(LFile::sha256(second.path()) == first.sha256());
    REQUIRE(first.path() != second.path());
    REQUIRE(first.sha256() == second.sha256());
}

TEST_CASE("LFile creates temporary files", "[LFile]") {
    auto file = LFile::create_temp_file("LFile-test-", ".tmp");
    REQUIRE(file.exists());
    REQUIRE(file.is_file());

    auto path = file.path();
    REQUIRE(file.remove());
    REQUIRE_FALSE(LPath::exists(path));
}

TEST_CASE("LFile atomic writes replace content without leaving temp files", "[LFile]") {
    TempTree tmp;
    LFile file = tmp.root / "atomic.txt";

    // 成员函数：replace 系列通过原子写落盘
    file.write_text("foo 123 foo 456");
    file.replace_all("foo", "bar");
    REQUIRE(file.read_text() == "bar 123 bar 456");

    file.regex_replace("[0-9]+", "#");
    REQUIRE(file.read_text() == "bar # bar #");

    // 成员函数：显式原子写
    file.atomic_write_text("hello atomic");
    REQUIRE(file.read_text() == "hello atomic");

    file.atomic_write_bytes(LStringDetail::string_view("a\0b", 3));
    REQUIRE(file.read_bytes() == std::string("a\0b", 3));

    file.write_bytes("xyxy");
    file.replace_bytes_all("xy", "pq");
    REQUIRE(file.read_bytes() == "pqpq");

    // 静态函数版本
    const auto static_path = tmp.root / "static_atomic.txt";
    LFile::atomic_write_text(static_path, "static text");
    REQUIRE(LFile::read_text(static_path) == "static text");
    LFile::atomic_write_bytes(static_path, "static bytes");
    REQUIRE(LFile::read_bytes(static_path) == "static bytes");
    LFile::replace_all(static_path, "static", "dynamic");
    REQUIRE(LFile::read_text(static_path) == "dynamic bytes");

    // 父目录不存在且 create_parent=false 时抛异常，且不残留临时文件
    const auto missing = tmp.root / "no_such_dir" / "x.txt";
    REQUIRE_THROWS_AS(LFile::atomic_write_text(missing, "boom", LEncoding::Utf8, false, true, false),
                      std::runtime_error);
    LFile missing_member(tmp.root / "no_such_dir" / "m.txt");
    REQUIRE_THROWS_AS(missing_member.atomic_write_text("boom", LEncoding::Utf8, false, true, false),
                      std::runtime_error);

    // 原子写后目录中不应残留任何 *.tmp 临时文件
    bool has_temp = false;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(tmp.root.path(), ec),
                end = std::filesystem::directory_iterator();
         !ec && it != end; ++it) {
        if (it->path().extension() == ".tmp") {
            has_temp = true;
            break;
        }
    }
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(has_temp);
}

#if !LTOOL_PLATFORM_WINDOWS
TEST_CASE("LFile atomic write preserves original file permissions", "[LFile]") {
    TempTree tmp;
    LFile file = tmp.root / "secret.env";

    // 先创建敏感文件并显式收紧为 0600（仅所有者可读写）。
    file.write_text("SECRET=1\n");
    REQUIRE(::chmod(file.path().c_str(), S_IRUSR | S_IWUSR) == 0);

    struct stat st;

    // 显式原子写后权限应保持 0600，而不是按 umask 放宽为 0644。
    file.atomic_write_text("SECRET=2\n");
    REQUIRE(::stat(file.path().c_str(), &st) == 0);
    REQUIRE((st.st_mode & 07777) == (S_IRUSR | S_IWUSR));

    // replace_all / regex_replace 同样走原子写，权限不变。
    file.replace_all("SECRET=2", "SECRET=3");
    REQUIRE(::stat(file.path().c_str(), &st) == 0);
    REQUIRE((st.st_mode & 07777) == (S_IRUSR | S_IWUSR));

    file.regex_replace("[0-9]+", "4");
    REQUIRE(::stat(file.path().c_str(), &st) == 0);
    REQUIRE((st.st_mode & 07777) == (S_IRUSR | S_IWUSR));
}
#endif // !LTOOL_PLATFORM_WINDOWS

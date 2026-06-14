/**
 * @file lfile.hpp
 * @brief std::filesystem::path 的纯头文件薄封装，附带常用文件、目录、
 *        路径、文本编码和临时文件工具。
 *
 * lfile 的核心定位：
 * - 内部只保存 std::filesystem::path，文件内容不常驻内存。
 * - 与 std::filesystem::path / std::string / lstring / C 字符串尽量自然互转。
 * - 路径操作默认遵循 std::filesystem 语义；exists()、is_file() 等查询吞掉
 *   filesystem_error 并返回保守结果。
 * - 读写 API 默认抛异常，方便调用点尽早发现失败；需要静默判断时先用查询 API。
 * - 文本读写复用 lstring 的编码边界能力，内部文本仍按 UTF-8 lstring 表示。
 *
 * 主要能力：
 * - 文件读写：read_bytes/read_text/read_lines、write/append、touch。
 * - 目录操作：create_directories、list、files、directories、recursive_files。
 * - 文件系统操作：copy/copy_file、move、rename、remove、remove_all、permissions。
 * - 路径辅助：filename/stem/extension/parent、absolute、canonical、relative_to、
 *   normalized、operator/。
 * - 临时路径：temp_directory、temp_file、create_temp_file。
 * - 通配匹配：简单文件名 glob，支持 * 和 ?。
 *
 * 常用示例：
 * @code
 * lfile file = "data/hello.txt";
 * file.write_text("你好\n");
 *
 * auto text = file.read_text();                 // lstring, 自动检测输入编码
 * auto lines = file.read_lines();
 *
 * for (auto& cpp : lfile("src").recursive_files(".cpp")) {
 *     fmt::println("{}", cpp);
 * }
 * @endcode
 */

#ifndef LFILE_INCLUDE
#define LFILE_INCLUDE

#include "lstring.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if LTOOL_HAS_FILESYSTEM
#include <filesystem>
#endif // LTOOL_HAS_FILESYSTEM
#define LFILE_HAS_FILESYSTEM LTOOL_HAS_FILESYSTEM

#if !LFILE_HAS_FILESYSTEM
#error "lfile requires C++17 std::filesystem support"
#endif // !LFILE_HAS_FILESYSTEM

/// list()/glob() 使用的目录项过滤类型。
enum class LFileEntryKind {
    /// 文件、目录、符号链接和其它目录项都保留。
    Any,
    /// 只保留普通文件。
    File,
    /// 只保留目录。
    Directory
};

/// 写文件时使用的打开模式。
enum class LFileWriteMode {
    /// 覆盖已有内容。
    Truncate,
    /// 追加到已有内容末尾。
    Append
};

class lfile;

namespace fmt {
template<typename Char>
struct formatter<lfile, Char, void> : formatter<std::filesystem::path, Char> {
    template<class FormatContext>
    typename FormatContext::iterator format(const lfile& value, FormatContext& ctx) const;
};
} // namespace fmt

namespace lfile_detail {

inline std::string path_display(const std::filesystem::path& path) {
    auto text = path.string();
    return text.empty() ? std::string(".") : text;
}

inline std::runtime_error make_error(const char* action, const std::filesystem::path& path) {
    return std::runtime_error(std::string(action) + ": " + path_display(path));
}

inline bool is_hidden_name(const std::filesystem::path& path) {
    auto name = path.filename().string();
    return !name.empty() && name[0] == '.';
}

inline bool matches_kind(const std::filesystem::directory_entry& entry, LFileEntryKind kind) {
    std::error_code ec;
    switch (kind) {
    case LFileEntryKind::File:
        return entry.is_regular_file(ec);
    case LFileEntryKind::Directory:
        return entry.is_directory(ec);
    case LFileEntryKind::Any:
    default:
        return true;
    }
}

inline void sort_paths(std::vector<std::filesystem::path>& paths) {
    std::sort(paths.begin(), paths.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.string() < rhs.string();
    });
}

inline void ensure_parent_directory(const std::filesystem::path& path) {
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw std::runtime_error("cannot create parent directory: " + path_display(parent) +
                                     " (" + ec.message() + ")");
        }
    }
}

inline std::ios::openmode write_open_mode(LFileWriteMode mode) {
    auto flags = std::ios::binary | std::ios::out;
    if (mode == LFileWriteMode::Append) {
        flags |= std::ios::app;
    } else {
        flags |= std::ios::trunc;
    }
    return flags;
}

inline std::uint64_t random_u64() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<std::uint64_t> dist;
    return dist(gen);
}

inline std::string unique_token() {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return fmt::format("{:x}{:x}", static_cast<std::uint64_t>(now), random_u64());
}

inline bool wildcard_match(lstring_detail::string_view text, lstring_detail::string_view pattern) {
    std::size_t ti = 0;
    std::size_t pi = 0;
    std::size_t star = lstring_detail::string_view::npos;
    std::size_t match = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++ti;
            ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            match = ti;
        } else if (star != lstring_detail::string_view::npos) {
            pi = star + 1;
            ti = ++match;
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }
    return pi == pattern.size();
}

inline std::string read_all_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw make_error("cannot open file for reading", path);
    }

    file.seekg(0, std::ios::end);
    auto end = file.tellg();
    if (end < 0) {
        throw make_error("cannot determine file size", path);
    }

    std::string out;
    out.resize(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!out.empty()) {
        file.read(&out[0], static_cast<std::streamsize>(out.size()));
        if (!file) {
            throw make_error("cannot read file", path);
        }
    }
    return out;
}

inline void write_all_bytes(const std::filesystem::path& path, lstring_detail::string_view bytes,
                            LFileWriteMode mode, bool create_parent) {
    if (create_parent) {
        ensure_parent_directory(path);
    }

    std::ofstream file(path, write_open_mode(mode));
    if (!file) {
        throw make_error("cannot open file for writing", path);
    }

    if (!bytes.empty()) {
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    if (!file) {
        throw make_error("cannot write file", path);
    }
}

template<class Range>
inline lstring join_lines(const Range& lines, lstring_detail::string_view newline,
                          bool final_newline) {
    lstring out;
    bool first = true;
    for (auto it = std::begin(lines); it != std::end(lines); ++it) {
        if (!first) {
            out += newline;
        }
        first = false;
        out += *it;
    }
    if (final_newline && !first) {
        out += newline;
    }
    return out;
}

} // namespace lfile_detail

/**
 * @brief 围绕 std::filesystem::path 的纯头文件薄封装。
 *
 * lfile 只保存路径本身。成员函数以当前 path_ 为目标，静态函数则适合在不想创建
 * lfile 对象时直接使用。除状态查询外，失败通常抛出 std::runtime_error 或
 * std::filesystem::filesystem_error。
 */
class lfile {
private:
    std::filesystem::path path_;

public:
    using path_type = std::filesystem::path;
    using size_type = std::uintmax_t;
    using file_time_type = std::filesystem::file_time_type;

    /// 创建空路径。
    lfile() = default;

    /// 复制另一个 lfile。
    lfile(const lfile&) = default;

    /// 移动另一个 lfile。
    lfile(lfile&&) noexcept = default;

    /// 从另一个 lfile 复制赋值。
    lfile& operator=(const lfile&) = default;

    /// 从另一个 lfile 移动赋值。
    lfile& operator=(lfile&&) noexcept = default;

    /// 从 std::filesystem::path 构造。
    lfile(std::filesystem::path path)
        : path_(std::move(path)) {}

    /// 从 C 字符串路径构造；nullptr 视为空路径。
    lfile(const char* path)
        : path_(path ? path : "") {}

    /// 从 std::string 路径构造。
    lfile(const std::string& path)
        : path_(path) {}

    /// 从 lstring 路径构造。
    lfile(const lstring& path)
        : path_(path.str()) {}

    /// 从 string_view 路径构造。
    lfile(lstring_detail::string_view path)
        : path_(std::string(path)) {}

    /// 从 std::filesystem::path 赋值。
    lfile& operator=(std::filesystem::path path) {
        path_ = std::move(path);
        return *this;
    }

    /// 从 C 字符串路径赋值；nullptr 视为空路径。
    lfile& operator=(const char* path) {
        path_ = path ? path : "";
        return *this;
    }

    /// 从 std::string 路径赋值。
    lfile& operator=(const std::string& path) {
        path_ = path;
        return *this;
    }

    /// 从 lstring 路径赋值。
    lfile& operator=(const lstring& path) {
        path_ = path.str();
        return *this;
    }

    /// 隐式暴露底层 path 的可写引用。
    operator std::filesystem::path&() & noexcept {
        return path_;
    }

    /// 隐式暴露底层 path 的只读引用。
    operator const std::filesystem::path&() const& noexcept {
        return path_;
    }

    /// 从右值 lfile 中移出底层 path。
    operator std::filesystem::path() && noexcept {
        return std::move(path_);
    }

    /// 返回底层 path 的可写引用。
    std::filesystem::path& path() & noexcept {
        return path_;
    }

    /// 返回底层 path 的只读引用。
    const std::filesystem::path& path() const& noexcept {
        return path_;
    }

    /// 从右值 lfile 中移出底层 path。
    std::filesystem::path&& path() && noexcept {
        return std::move(path_);
    }

    /// 返回 path.string() 的 lstring 表示。
    lstring str() const {
        return path_.string();
    }

    /// 返回 path.native()。
    const std::filesystem::path::string_type& native() const noexcept {
        return path_.native();
    }

    /// 返回以 NUL 结尾的 path 字符串表示；适合临时传参。
    std::string string() const {
        return path_.string();
    }

    /// 判断路径是否为空。
    bool empty() const noexcept {
        return path_.empty();
    }

    /// 清空路径。
    void clear() noexcept {
        path_.clear();
    }

    /// 按文件系统路径规则追加路径组件，并返回 *this。
    lfile& append_path(const std::filesystem::path& rhs) {
        path_ /= rhs;
        return *this;
    }

    /// 返回拼接后的新路径。
    lfile join_path(const std::filesystem::path& rhs) const {
        return path_ / rhs;
    }

    /// 路径拼接运算符，等价于 join_path()。
    friend lfile operator/(const lfile& lhs, const std::filesystem::path& rhs) {
        return lhs.join_path(rhs);
    }

    /// 按路径字面值比较两个 lfile。
    friend bool operator==(const lfile& lhs, const lfile& rhs) noexcept {
        return lhs.path_ == rhs.path_;
    }

    /// 按路径字面值比较 lfile 和 std::filesystem::path。
    friend bool operator==(const lfile& lhs, const std::filesystem::path& rhs) noexcept {
        return lhs.path_ == rhs;
    }

    /// 按路径字面值比较 std::filesystem::path 和 lfile。
    friend bool operator==(const std::filesystem::path& lhs, const lfile& rhs) noexcept {
        return lhs == rhs.path_;
    }

#if __cplusplus >= 202002L
    /// 按 path 的字典序进行三路比较。
    friend auto operator<=>(const lfile&, const lfile&) = default;
#endif // __cplusplus >= 202002L

    /// 将路径字符串写入 ostream。
    friend std::ostream& operator<<(std::ostream& os, const lfile& value) {
        return os << value.path_.string();
    }

    /// 返回 path().filename()。
    lfile filename() const {
        return path_.filename();
    }

    /// 返回 path().stem()。
    lfile stem() const {
        return path_.stem();
    }

    /// 返回 path().extension()。
    lfile extension() const {
        return path_.extension();
    }

    /// 返回 path().parent_path()。
    lfile parent_path() const {
        return path_.parent_path();
    }

    /// 返回 path().root_name()。
    lfile root_name() const {
        return path_.root_name();
    }

    /// 返回 path().root_path()。
    lfile root_path() const {
        return path_.root_path();
    }

    /// 返回 path().relative_path()。
    lfile relative_path() const {
        return path_.relative_path();
    }

    /// 返回 path().lexically_normal()；不访问真实文件系统。
    lfile normalized() const {
        return path_.lexically_normal();
    }

    /// 判断当前路径是否为绝对路径。
    bool is_absolute() const {
        return path_.is_absolute();
    }

    /// 判断当前路径是否为相对路径。
    bool is_relative() const {
        return path_.is_relative();
    }

    /// 返回替换扩展名后的副本。
    lfile with_extension(const std::filesystem::path& extension) const {
        auto out = path_;
        out.replace_extension(extension);
        return out;
    }

    /// 原地替换扩展名，并返回 *this。
    lfile& replace_extension(const std::filesystem::path& extension = std::filesystem::path()) {
        path_.replace_extension(extension);
        return *this;
    }

    /// 返回当前路径相对 @p base 的词法相对路径；不访问真实文件系统。
    lfile lexically_relative(const std::filesystem::path& base) const {
        return path_.lexically_relative(base);
    }

    /// 返回绝对路径。
    lfile absolute() const {
        return std::filesystem::absolute(path_);
    }

    /// 返回规范路径；目标必须存在。
    lfile canonical() const {
        return std::filesystem::canonical(path_);
    }

    /// 返回弱规范路径；适合目标还未完全存在的路径。
    lfile weakly_canonical() const {
        return std::filesystem::weakly_canonical(path_);
    }

    /// 返回当前路径相对 @p base 的文件系统相对路径。
    lfile relative_to(const std::filesystem::path& base = std::filesystem::current_path()) const {
        return std::filesystem::relative(path_, base);
    }

    /// 判断路径是否存在；文件系统错误会被吞掉。
    bool exists() const {
        std::error_code ec;
        return std::filesystem::exists(path_, ec);
    }

    /// 判断路径是否指向普通文件；文件系统错误会被吞掉。
    bool is_file() const {
        std::error_code ec;
        return std::filesystem::is_regular_file(path_, ec);
    }

    /// 判断路径是否指向目录；文件系统错误会被吞掉。
    bool is_dir() const {
        std::error_code ec;
        return std::filesystem::is_directory(path_, ec);
    }

    /// is_dir() 的同义函数。
    bool is_directory() const {
        return is_dir();
    }

    /// 判断路径是否指向符号链接；文件系统错误会被吞掉。
    bool is_symlink() const {
        std::error_code ec;
        return std::filesystem::is_symlink(path_, ec);
    }

    /// 判断目标是否为空文件或空目录；不存在或出错时返回 false。
    bool is_empty() const {
        std::error_code ec;
        auto value = std::filesystem::is_empty(path_, ec);
        return !ec && value;
    }

    /// 返回文件大小；失败时抛出 filesystem_error。
    size_type size() const {
        return std::filesystem::file_size(path_);
    }

    /// 返回文件大小；失败时返回 default_value。
    size_type size_or(size_type default_value = 0) const {
        std::error_code ec;
        auto value = std::filesystem::file_size(path_, ec);
        return ec ? default_value : value;
    }

    /// 返回最后修改时间；失败时抛出 filesystem_error。
    file_time_type last_write_time() const {
        return std::filesystem::last_write_time(path_);
    }

    /// 设置最后修改时间；失败时抛出 filesystem_error。
    lfile& set_last_write_time(file_time_type time) {
        std::filesystem::last_write_time(path_, time);
        return *this;
    }

    /// 读取文件原始字节。
    std::string read_bytes() const {
        return read_bytes(path_);
    }

    /// 读取文本为 UTF-8 lstring；默认自动检测外部编码。
    lstring read_text(LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(path_, encoding, strict);
    }

    /// 按行读取文本；会移除每行末尾的 '\r'。
    std::vector<lstring> read_lines(bool keep_empty = true,
                                    LEncoding encoding = LEncoding::Unknown,
                                    bool strict = true) const {
        return read_text(encoding, strict).lines(keep_empty);
    }

    /// 覆盖写入原始字节。
    lfile& write_bytes(lstring_detail::string_view bytes, bool create_parent = true) {
        write_bytes(path_, bytes, create_parent);
        return *this;
    }

    /// 追加写入原始字节。
    lfile& append_bytes(lstring_detail::string_view bytes, bool create_parent = true) {
        append_bytes(path_, bytes, create_parent);
        return *this;
    }

    /// 覆盖写入 UTF-8 文本，可在边界处转为指定外部编码。
    lfile& write_text(lstring_detail::string_view text, LEncoding encoding = LEncoding::Utf8,
                      bool write_bom = false, bool strict = true, bool create_parent = true) {
        write_text(path_, text, encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 追加写入 UTF-8 文本，可在边界处转为指定外部编码。
    lfile& append_text(lstring_detail::string_view text, LEncoding encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true, bool create_parent = true) {
        append_text(path_, text, encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 覆盖写入多行文本。
    template<class Range>
    lfile& write_lines(const Range& lines, lstring_detail::string_view newline = "\n",
                       bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true, bool create_parent = true) {
        write_text(lfile_detail::join_lines(lines, newline, final_newline).view(),
                   encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 追加写入多行文本。
    template<class Range>
    lfile& append_lines(const Range& lines, lstring_detail::string_view newline = "\n",
                        bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                        bool write_bom = false, bool strict = true, bool create_parent = true) {
        append_text(lfile_detail::join_lines(lines, newline, final_newline).view(),
                    encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 创建空文件或更新时间戳；父目录可自动创建。
    lfile& touch(bool create_parent = true) {
        touch(path_, create_parent);
        return *this;
    }

    /// 创建当前路径指向的目录及其缺失父目录。
    lfile& create_directories() {
        std::filesystem::create_directories(path_);
        return *this;
    }

    /// 创建当前路径指向的单级目录。
    lfile& create_directory() {
        std::filesystem::create_directory(path_);
        return *this;
    }

    /// 创建当前路径的父目录。
    lfile& create_parent_directories() {
        lfile_detail::ensure_parent_directory(path_);
        return *this;
    }

    /// 删除文件、空目录或符号链接；不存在时返回 false。
    bool remove() const {
        return std::filesystem::remove(path_);
    }

    /// 递归删除路径；返回删除的目录项数量。
    size_type remove_all() const {
        return std::filesystem::remove_all(path_);
    }

    /// 将当前路径复制到 @p to。
    const lfile& copy_to(const std::filesystem::path& to, std::filesystem::copy_options options =
                                                             std::filesystem::copy_options::none) const {
        std::filesystem::copy(path_, to, options);
        return *this;
    }

    /// 将当前普通文件复制到 @p to。
    bool copy_file_to(const std::filesystem::path& to, bool overwrite = false) const {
        auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                                 : std::filesystem::copy_options::none;
        return std::filesystem::copy_file(path_, to, options);
    }

    /// 重命名或移动当前路径，并把对象路径更新为目标路径。
    lfile& move_to(const std::filesystem::path& to) {
        std::filesystem::rename(path_, to);
        path_ = to;
        return *this;
    }

    /// move_to() 的同义函数。
    lfile& rename_to(const std::filesystem::path& to) {
        return move_to(to);
    }

    /// 设置权限。
    lfile& permissions(std::filesystem::perms permissions,
                       std::filesystem::perm_options options =
                           std::filesystem::perm_options::replace) {
        std::filesystem::permissions(path_, permissions, options);
        return *this;
    }

    /// 列出目录项，可选择递归和过滤隐藏文件名。
    std::vector<lfile> list(LFileEntryKind kind = LFileEntryKind::Any, bool recursive = false,
                            bool include_hidden = true) const {
        return list(path_, kind, recursive, include_hidden);
    }

    /// 列出当前目录下的普通文件。
    std::vector<lfile> files(lstring_detail::string_view extension = {},
                             bool include_hidden = true) const {
        return files(path_, extension, include_hidden);
    }

    /// 递归列出普通文件。
    std::vector<lfile> recursive_files(lstring_detail::string_view extension = {},
                                       bool include_hidden = true) const {
        return recursive_files(path_, extension, include_hidden);
    }

    /// 列出当前目录下的子目录。
    std::vector<lfile> directories(bool include_hidden = true) const {
        return directories(path_, include_hidden);
    }

    /// 使用当前目录作为 base，按文件名执行简单通配匹配。
    std::vector<lfile> glob(lstring_detail::string_view pattern, bool recursive = false,
                            LFileEntryKind kind = LFileEntryKind::Any,
                            bool include_hidden = true) const {
        return glob_path(path_ / std::string(pattern), recursive, kind, include_hidden);
    }

    /// 读取文件原始字节。
    static std::string read_bytes(const std::filesystem::path& path) {
        return lfile_detail::read_all_bytes(path);
    }

    /// 读取文本为 UTF-8 lstring；默认自动检测外部编码。
    static lstring read_text(const std::filesystem::path& path,
                             LEncoding encoding = LEncoding::Unknown,
                             bool strict = true) {
        return lstring::from_encoding(read_bytes(path), encoding, strict);
    }

    /// 按行读取文本；会移除每行末尾的 '\r'。
    static std::vector<lstring> read_lines(const std::filesystem::path& path,
                                           bool keep_empty = true,
                                           LEncoding encoding = LEncoding::Unknown,
                                           bool strict = true) {
        return read_text(path, encoding, strict).lines(keep_empty);
    }

    /// 覆盖写入原始字节。
    static void write_bytes(const std::filesystem::path& path, lstring_detail::string_view bytes,
                            bool create_parent = true) {
        lfile_detail::write_all_bytes(path, bytes, LFileWriteMode::Truncate, create_parent);
    }

    /// 追加写入原始字节。
    static void append_bytes(const std::filesystem::path& path, lstring_detail::string_view bytes,
                             bool create_parent = true) {
        lfile_detail::write_all_bytes(path, bytes, LFileWriteMode::Append, create_parent);
    }

    /// 覆盖写入 UTF-8 文本，可在边界处转为指定外部编码。
    static void write_text(const std::filesystem::path& path, lstring_detail::string_view text,
                           LEncoding encoding = LEncoding::Utf8, bool write_bom = false,
                           bool strict = true, bool create_parent = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = LEncoding::Utf8;
        }
        lfile_detail::write_all_bytes(path, lstring(text).to_encoding(encoding, write_bom, strict),
                                      LFileWriteMode::Truncate, create_parent);
    }

    /// 追加写入 UTF-8 文本，可在边界处转为指定外部编码。
    static void append_text(const std::filesystem::path& path, lstring_detail::string_view text,
                            LEncoding encoding = LEncoding::Utf8, bool write_bom = false,
                            bool strict = true, bool create_parent = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = LEncoding::Utf8;
        }
        lfile_detail::write_all_bytes(path, lstring(text).to_encoding(encoding, write_bom, strict),
                                      LFileWriteMode::Append, create_parent);
    }

    /// 覆盖写入多行文本。
    template<class Range>
    static void write_lines(const std::filesystem::path& path, const Range& lines,
                            lstring_detail::string_view newline = "\n",
                            bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                            bool write_bom = false, bool strict = true,
                            bool create_parent = true) {
        write_text(path, lfile_detail::join_lines(lines, newline, final_newline).view(),
                   encoding, write_bom, strict, create_parent);
    }

    /// 追加写入多行文本。
    template<class Range>
    static void append_lines(const std::filesystem::path& path, const Range& lines,
                             lstring_detail::string_view newline = "\n",
                             bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                             bool write_bom = false, bool strict = true,
                             bool create_parent = true) {
        append_text(path, lfile_detail::join_lines(lines, newline, final_newline).view(),
                    encoding, write_bom, strict, create_parent);
    }

    /// 创建空文件或更新时间戳；父目录可自动创建。
    static void touch(const std::filesystem::path& path, bool create_parent = true) {
        if (create_parent) {
            lfile_detail::ensure_parent_directory(path);
        }
        {
            std::ofstream file(path, std::ios::binary | std::ios::app);
            if (!file) {
                throw lfile_detail::make_error("cannot touch file", path);
            }
        }
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now());
    }

    /// 判断路径是否存在；文件系统错误会被吞掉。
    static bool exists(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    /// 判断路径是否指向普通文件；文件系统错误会被吞掉。
    static bool is_file(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }

    /// 判断路径是否指向目录；文件系统错误会被吞掉。
    static bool is_dir(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    /// 判断两个路径是否等价；错误时返回 false。
    static bool equivalent(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        std::error_code ec;
        auto value = std::filesystem::equivalent(lhs, rhs, ec);
        return !ec && value;
    }

    /// 返回当前工作目录。
    static lfile current_path() {
        return std::filesystem::current_path();
    }

    /// current_path() 的短别名。
    static lfile cwd() {
        return current_path();
    }

    /// 设置当前工作目录。
    static void set_current_path(const std::filesystem::path& path) {
        std::filesystem::current_path(path);
    }

    /// 返回系统临时目录。
    static lfile temp_directory() {
        return std::filesystem::temp_directory_path();
    }

    /// 生成一个当前尚不存在的临时文件路径；不会创建文件。
    static lfile temp_file(lstring_detail::string_view prefix = "lfile-",
                           lstring_detail::string_view extension = ".tmp") {
        auto dir = temp_directory().path();
        for (int i = 0; i < 100; ++i) {
            auto name = std::string(prefix) + lfile_detail::unique_token() + std::string(extension);
            auto candidate = dir / name;
            if (!exists(candidate)) {
                return candidate;
            }
        }
        throw std::runtime_error("cannot create unique temporary file path");
    }

    /// 创建并返回一个空临时文件路径。
    static lfile create_temp_file(lstring_detail::string_view prefix = "lfile-",
                                  lstring_detail::string_view extension = ".tmp") {
        auto path = temp_file(prefix, extension);
        path.touch(false);
        return path;
    }

    /// 列出目录项，可选择递归和过滤隐藏文件名。
    static std::vector<lfile> list(const std::filesystem::path& directory,
                                   LFileEntryKind kind = LFileEntryKind::Any,
                                   bool recursive = false,
                                   bool include_hidden = true) {
        std::vector<std::filesystem::path> paths;
        auto options = std::filesystem::directory_options::skip_permission_denied;

        if (recursive) {
            for (std::filesystem::recursive_directory_iterator it(directory, options), end;
                 it != end; ++it) {
                if ((!include_hidden && lfile_detail::is_hidden_name(it->path())) ||
                    !lfile_detail::matches_kind(*it, kind)) {
                    continue;
                }
                paths.push_back(it->path());
            }
        } else {
            for (std::filesystem::directory_iterator it(directory, options), end; it != end; ++it) {
                if ((!include_hidden && lfile_detail::is_hidden_name(it->path())) ||
                    !lfile_detail::matches_kind(*it, kind)) {
                    continue;
                }
                paths.push_back(it->path());
            }
        }

        lfile_detail::sort_paths(paths);
        std::vector<lfile> out;
        out.reserve(paths.size());
        for (auto& path : paths) {
            out.emplace_back(std::move(path));
        }
        return out;
    }

    /// 列出目录下的普通文件。
    static std::vector<lfile> files(const std::filesystem::path& directory,
                                    lstring_detail::string_view extension = {},
                                    bool include_hidden = true) {
        auto out = list(directory, LFileEntryKind::File, false, include_hidden);
        if (!extension.empty()) {
            out.erase(std::remove_if(out.begin(), out.end(), [&](const lfile& file) {
                return file.extension().str() != extension;
            }), out.end());
        }
        return out;
    }

    /// 递归列出普通文件。
    static std::vector<lfile> recursive_files(const std::filesystem::path& directory,
                                              lstring_detail::string_view extension = {},
                                              bool include_hidden = true) {
        auto out = list(directory, LFileEntryKind::File, true, include_hidden);
        if (!extension.empty()) {
            out.erase(std::remove_if(out.begin(), out.end(), [&](const lfile& file) {
                return file.extension().str() != extension;
            }), out.end());
        }
        return out;
    }

    /// 列出目录下的子目录。
    static std::vector<lfile> directories(const std::filesystem::path& directory,
                                          bool include_hidden = true) {
        return list(directory, LFileEntryKind::Directory, false, include_hidden);
    }

    /**
     * @brief 简单通配查找。
     *
     * pattern 的父路径作为搜索目录，filename 部分支持 * 和 ?。例如传入
     * "src / *.cpp" 形态的路径会在 src 下匹配文件名，不展开通配目录组件。
     */
    static std::vector<lfile> glob_path(const std::filesystem::path& pattern,
                                        bool recursive = false,
                                        LFileEntryKind kind = LFileEntryKind::Any,
                                        bool include_hidden = true) {
        auto directory = pattern.parent_path();
        if (directory.empty()) {
            directory = ".";
        }
        auto mask = pattern.filename().string();
        auto entries = list(directory, kind, recursive, include_hidden);
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const lfile& file) {
            return !lfile_detail::wildcard_match(file.filename().str(), mask);
        }), entries.end());
        return entries;
    }

    /// 使用当前标准下可用的标准哈希器对路径求哈希。
    std::size_t hash() const noexcept {
        return std::filesystem::hash_value(path_);
    }
};

/// PascalCase 别名，供偏好类型命名风格的代码使用。
using LFile = lfile;

/// 用户自定义字面量，用于从字符串字面量简洁构造 lfile。
inline lfile operator""_lf(const char* text, std::size_t size) {
    return lfile(std::filesystem::path(std::string(text, size)));
}

/// fmt 格式化器实现：lfile 复用 std::filesystem::path 的格式化规则。
namespace fmt {
template<typename Char>
template<class FormatContext>
typename FormatContext::iterator formatter<lfile, Char, void>::format(
    const lfile& value, FormatContext& ctx) const {
    return formatter<std::filesystem::path, Char>::format(value.path(), ctx);
}
} // namespace fmt

/// std::hash 特化，使 lfile 可用于无序容器。
template<>
struct std::hash<lfile> {
    /// 按 lfile 的底层路径求哈希。
    std::size_t operator()(const lfile& value) const noexcept {
        return value.hash();
    }
};

#endif // !LFILE_INCLUDE

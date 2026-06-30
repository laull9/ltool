/**
 * @file LFile.hpp
 * @brief 面向文件内容读写的纯头文件工具。
 *
 * LFile 保存一个 LPath，但职责集中在普通文件内容：
 * - 读取原始字节、文本和行。
 * - 覆盖或追加写入原始字节、文本和行。
 * - 创建空文件或更新时间戳。
 * - 创建临时文件。
 *
 * 路径拼接、目录枚举、glob、路径规范化等能力由 LPath 提供。
 */

#ifndef LFILE_INCLUDE
#define LFILE_INCLUDE

#include "detail/LConcepts.hpp"
#include "LPath.hpp"

#include <fstream>
#include <ios>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/// 写文件时使用的打开模式。
enum class LFileWriteMode {
    /// 覆盖已有内容。
    Truncate,
    /// 追加到已有内容末尾。
    Append
};

class LFile;

namespace fmt {
template<typename Char>
struct formatter<LFile, Char, void> : formatter<std::filesystem::path, Char> {
    template<class FormatContext>
    typename FormatContext::iterator format(const LFile& value, FormatContext& ctx) const;
};
} // namespace fmt

namespace LFileDetail {

inline std::ios::openmode write_open_mode(LFileWriteMode mode) {
    auto flags = std::ios::binary | std::ios::out;
    if (mode == LFileWriteMode::Append) {
        flags |= std::ios::app;
    } else {
        flags |= std::ios::trunc;
    }
    return flags;
}

inline std::string read_all_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw LPathDetail::make_error("cannot open file for reading", path);
    }

    file.seekg(0, std::ios::end);
    auto end = file.tellg();
    if (end < 0) {
        throw LPathDetail::make_error("cannot determine file size", path);
    }

    std::string out;
    out.resize(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!out.empty()) {
        file.read(&out[0], static_cast<std::streamsize>(out.size()));
        if (!file) {
            throw LPathDetail::make_error("cannot read file", path);
        }
    }
    return out;
}

inline void write_all_bytes(const std::filesystem::path& path, LStringDetail::string_view bytes,
                            LFileWriteMode mode, bool create_parent) {
    if (create_parent) {
        LPathDetail::ensure_parent_directory(path);
    }

    std::ofstream file(path, write_open_mode(mode));
    if (!file) {
        throw LPathDetail::make_error("cannot open file for writing", path);
    }

    if (!bytes.empty()) {
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    if (!file) {
        throw LPathDetail::make_error("cannot write file", path);
    }
}

inline std::ifstream open_input_stream(const std::filesystem::path& path,
                                       std::ios::openmode mode) {
    std::ifstream file(path, mode);
    if (!file) {
        throw LPathDetail::make_error("cannot open file for reading", path);
    }
    return file;
}

inline std::ofstream open_output_stream(const std::filesystem::path& path,
                                        std::ios::openmode mode, bool create_parent) {
    if (create_parent) {
        LPathDetail::ensure_parent_directory(path);
    }

    std::ofstream file(path, mode);
    if (!file) {
        throw LPathDetail::make_error("cannot open file for writing", path);
    }
    return file;
}

inline std::fstream open_io_stream(const std::filesystem::path& path,
                                   std::ios::openmode mode, bool create_parent) {
    if (create_parent) {
        LPathDetail::ensure_parent_directory(path);
    }

    std::fstream file(path, mode);
    if (!file) {
        throw LPathDetail::make_error("cannot open file stream", path);
    }
    return file;
}

template<class Range LTOOL_ENABLE_IF(LTool::traits::is_const_range<Range>::value)>
    LTOOL_REQUIRES(LTool::concepts::ConstRange<Range>)
inline LString join_lines(const Range& lines, LStringDetail::string_view newline,
                          bool final_newline) {
    LString out;
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

} // namespace LFileDetail

/**
 * @brief 文件内容读写对象。
 *
 * LFile 不做路径拼接和目录遍历；需要这类能力时先使用 LPath，再把目标路径交给 LFile。
 */
class LFile {
private:
    LPath path_;

public:
    using path_type = std::filesystem::path;
    using size_type = std::uintmax_t;
    using file_time_type = std::filesystem::file_time_type;
    using text_size_type = LString::size_type;
    using bytes_size_type = std::string::size_type;

    static constexpr text_size_type text_npos = LString::npos;
    static constexpr bytes_size_type bytes_npos = std::string::npos;

    LFile() = default;
    LFile(const LFile&) = default;
    LFile(LFile&&) noexcept = default;
    LFile& operator=(const LFile&) = default;
    LFile& operator=(LFile&&) noexcept = default;

    LFile(LPath path)
        : path_(std::move(path)) {}

    LFile(std::filesystem::path path)
        : path_(std::move(path)) {}

    LFile(const char* path)
        : path_(path) {}

    LFile(const std::string& path)
        : path_(path) {}

    LFile(const LString& path)
        : path_(path) {}

    LFile(LStringDetail::string_view path)
        : path_(path) {}

    LFile& operator=(LPath path) {
        path_ = std::move(path);
        return *this;
    }

    LFile& operator=(std::filesystem::path path) {
        path_ = std::move(path);
        return *this;
    }

    LFile& operator=(const char* path) {
        path_ = path;
        return *this;
    }

    LFile& operator=(const std::string& path) {
        path_ = path;
        return *this;
    }

    LFile& operator=(const LString& path) {
        path_ = path;
        return *this;
    }

    operator std::filesystem::path&() & noexcept {
        return path_.path();
    }

    operator const std::filesystem::path&() const& noexcept {
        return path_.path();
    }

    operator std::filesystem::path() && noexcept {
        return std::move(path_).path();
    }

    LPath& lpath() & noexcept {
        return path_;
    }

    const LPath& lpath() const& noexcept {
        return path_;
    }

    LPath&& lpath() && noexcept {
        return std::move(path_);
    }

    std::filesystem::path& path() & noexcept {
        return path_.path();
    }

    const std::filesystem::path& path() const& noexcept {
        return path_.path();
    }

    std::filesystem::path&& path() && noexcept {
        return std::move(path_).path();
    }

    LString str() const {
        return path_.str();
    }

    std::string string() const {
        return path_.string();
    }

    const std::filesystem::path::string_type& native() const noexcept {
        return path_.native();
    }

    bool empty() const noexcept {
        return path_.empty();
    }

    bool exists() const {
        return path_.exists();
    }

    bool is_file() const {
        return path_.is_file();
    }

    size_type size() const {
        return path_.size();
    }

    size_type size_or(size_type default_value = 0) const {
        return path_.size_or(default_value);
    }

    file_time_type last_write_time() const {
        return path_.last_write_time();
    }

    LFile& set_last_write_time(file_time_type time) {
        path_.set_last_write_time(time);
        return *this;
    }

    std::string read_bytes() const {
        return read_bytes(path());
    }

    LString read_text(LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(path(), encoding, strict);
    }

    std::vector<LString> read_lines(bool keep_empty = true,
                                    LEncoding encoding = LEncoding::Unknown,
                                    bool strict = true) const {
        return read_text(encoding, strict).lines(keep_empty);
    }

    bool bytes_contains(LStringDetail::string_view needle) const {
        return bytes_find(needle) != bytes_npos;
    }

    bytes_size_type bytes_find(LStringDetail::string_view needle,
                               bytes_size_type pos = 0) const {
        return read_bytes().find(needle, pos);
    }

    bytes_size_type bytes_rfind(LStringDetail::string_view needle,
                                bytes_size_type pos = bytes_npos) const {
        return read_bytes().rfind(needle, pos);
    }

    LFile& replace_bytes_all(LStringDetail::string_view from, LStringDetail::string_view to,
                             bool create_parent = true) {
        auto bytes = read_bytes();
        LString text(bytes);
        text.replace_all(from, to);
        write_bytes(text.view(), create_parent);
        return *this;
    }

    bool contains(LStringDetail::string_view needle,
                  LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).contains(needle);
    }

    text_size_type find(LStringDetail::string_view needle, text_size_type pos = 0,
                        LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).find(needle, pos);
    }

    text_size_type rfind(LStringDetail::string_view needle,
                         text_size_type pos = text_npos,
                         LEncoding encoding = LEncoding::Unknown,
                         bool strict = true) const {
        return read_text(encoding, strict).rfind(needle, pos);
    }

    LString replaced_all(LStringDetail::string_view from, LStringDetail::string_view to,
                         LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).replaced_all(from, to);
    }

    LFile& replace_all(LStringDetail::string_view from, LStringDetail::string_view to,
                       LEncoding read_encoding = LEncoding::Unknown,
                       LEncoding write_encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true,
                       bool create_parent = true) {
        auto text = read_text(read_encoding, strict);
        text.replace_all(from, to);
        write_text(text.view(), write_encoding, write_bom, strict, create_parent);
        return *this;
    }

    bool regex_contains(LStringDetail::string_view pattern,
                        LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).regex_contains(pattern);
    }

#if LSTRING_HAS_STD_OPTIONAL
    std::optional<LString> regex_find(LStringDetail::string_view pattern,
                                      LEncoding encoding = LEncoding::Unknown,
                                      bool strict = true) const {
        return read_text(encoding, strict).regex_find(pattern);
    }
#endif // LSTRING_HAS_STD_OPTIONAL

    std::vector<LString> regex_find_all(LStringDetail::string_view pattern,
                                        LEncoding encoding = LEncoding::Unknown,
                                        bool strict = true) const {
        return read_text(encoding, strict).regex_find_all(pattern);
    }

    LString regex_replaced(LStringDetail::string_view pattern, LStringDetail::string_view rewrite,
                           bool replace_all = true,
                           LEncoding encoding = LEncoding::Unknown,
                           bool strict = true) const {
        return read_text(encoding, strict).regex_replaced(pattern, rewrite, replace_all);
    }

    LFile& regex_replace(LStringDetail::string_view pattern, LStringDetail::string_view rewrite,
                         bool replace_all = true,
                         LEncoding read_encoding = LEncoding::Unknown,
                         LEncoding write_encoding = LEncoding::Utf8,
                         bool write_bom = false, bool strict = true,
                         bool create_parent = true) {
        auto text = read_text(read_encoding, strict);
        text.regex_replace(pattern, rewrite, replace_all);
        write_text(text.view(), write_encoding, write_bom, strict, create_parent);
        return *this;
    }

    std::ifstream open_input(std::ios::openmode mode = std::ios::binary) const {
        return open_input(path(), mode);
    }

    std::ofstream open_output(LFileWriteMode mode = LFileWriteMode::Truncate,
                              bool create_parent = true) const {
        return open_output(path(), mode, create_parent);
    }

    std::ofstream open_output(std::ios::openmode mode, bool create_parent = true) const {
        return open_output(path(), mode, create_parent);
    }

    std::fstream open_stream(std::ios::openmode mode =
                                 std::ios::binary | std::ios::in | std::ios::out,
                             bool create_parent = false) const {
        return open_stream(path(), mode, create_parent);
    }

    std::string read_bytes_at(std::streamoff offset, std::size_t count,
                              std::ios::seekdir dir = std::ios::beg) const {
        return read_bytes_at(path(), offset, count, dir);
    }

    std::string read_bytes_from(std::streamoff offset,
                                std::ios::seekdir dir = std::ios::beg) const {
        return read_bytes_from(path(), offset, dir);
    }

    LFile& write_bytes_at(std::streamoff offset, LStringDetail::string_view bytes,
                          std::ios::seekdir dir = std::ios::beg,
                          bool create_parent = true) {
        write_bytes_at(path(), offset, bytes, dir, create_parent);
        return *this;
    }

    LFile& write_bytes(LStringDetail::string_view bytes, bool create_parent = true) {
        write_bytes(path(), bytes, create_parent);
        return *this;
    }

    LFile& append_bytes(LStringDetail::string_view bytes, bool create_parent = true) {
        append_bytes(path(), bytes, create_parent);
        return *this;
    }

    LFile& write_text(LStringDetail::string_view text, LEncoding encoding = LEncoding::Utf8,
                      bool write_bom = false, bool strict = true, bool create_parent = true) {
        write_text(path(), text, encoding, write_bom, strict, create_parent);
        return *this;
    }

    LFile& append_text(LStringDetail::string_view text, LEncoding encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true, bool create_parent = true) {
        append_text(path(), text, encoding, write_bom, strict, create_parent);
        return *this;
    }

    template<class Range LTOOL_ENABLE_IF(LTool::traits::is_const_range<Range>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Range>)
    LFile& write_lines(const Range& lines, LStringDetail::string_view newline = "\n",
                       bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true, bool create_parent = true) {
        write_text(LFileDetail::join_lines(lines, newline, final_newline).view(),
                   encoding, write_bom, strict, create_parent);
        return *this;
    }

    template<class Range LTOOL_ENABLE_IF(LTool::traits::is_const_range<Range>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Range>)
    LFile& append_lines(const Range& lines, LStringDetail::string_view newline = "\n",
                        bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                        bool write_bom = false, bool strict = true, bool create_parent = true) {
        append_text(LFileDetail::join_lines(lines, newline, final_newline).view(),
                    encoding, write_bom, strict, create_parent);
        return *this;
    }

    LFile& touch(bool create_parent = true) {
        touch(path(), create_parent);
        return *this;
    }

    bool remove() const {
        return std::filesystem::remove(path());
    }

    bool copy_to(const std::filesystem::path& to, bool overwrite = false) const {
        auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                                 : std::filesystem::copy_options::none;
        return std::filesystem::copy_file(path(), to, options);
    }

    LFile& move_to(const std::filesystem::path& to) {
        std::filesystem::rename(path(), to);
        path_ = to;
        return *this;
    }

    LFile& rename_to(const std::filesystem::path& to) {
        return move_to(to);
    }

    friend bool operator==(const LFile& lhs, const LFile& rhs) noexcept {
        return lhs.path_ == rhs.path_;
    }

    friend bool operator==(const LFile& lhs, const std::filesystem::path& rhs) noexcept {
        return lhs.path_ == rhs;
    }

    friend bool operator==(const std::filesystem::path& lhs, const LFile& rhs) noexcept {
        return lhs == rhs.path_;
    }

#if __cplusplus >= 202002L
    friend auto operator<=>(const LFile&, const LFile&) = default;
#endif // __cplusplus >= 202002L

    friend std::ostream& operator<<(std::ostream& os, const LFile& value) {
        return os << value.path().string();
    }

    static std::string read_bytes(const std::filesystem::path& path) {
        return LFileDetail::read_all_bytes(path);
    }

    static LString read_text(const std::filesystem::path& path,
                             LEncoding encoding = LEncoding::Unknown,
                             bool strict = true) {
        return LString::from_encoding(read_bytes(path), encoding, strict);
    }

    static std::vector<LString> read_lines(const std::filesystem::path& path,
                                           bool keep_empty = true,
                                           LEncoding encoding = LEncoding::Unknown,
                                           bool strict = true) {
        return read_text(path, encoding, strict).lines(keep_empty);
    }

    static bool bytes_contains(const std::filesystem::path& path,
                               LStringDetail::string_view needle) {
        return bytes_find(path, needle) != bytes_npos;
    }

    static bytes_size_type bytes_find(const std::filesystem::path& path,
                                      LStringDetail::string_view needle,
                                      bytes_size_type pos = 0) {
        return read_bytes(path).find(needle, pos);
    }

    static bytes_size_type bytes_rfind(const std::filesystem::path& path,
                                       LStringDetail::string_view needle,
                                       bytes_size_type pos = bytes_npos) {
        return read_bytes(path).rfind(needle, pos);
    }

    static void replace_bytes_all(const std::filesystem::path& path,
                                  LStringDetail::string_view from,
                                  LStringDetail::string_view to,
                                  bool create_parent = true) {
        auto bytes = read_bytes(path);
        LString text(bytes);
        text.replace_all(from, to);
        write_bytes(path, text.view(), create_parent);
    }

    static bool contains(const std::filesystem::path& path, LStringDetail::string_view needle,
                         LEncoding encoding = LEncoding::Unknown, bool strict = true) {
        return read_text(path, encoding, strict).contains(needle);
    }

    static text_size_type find(const std::filesystem::path& path,
                               LStringDetail::string_view needle,
                               text_size_type pos = 0,
                               LEncoding encoding = LEncoding::Unknown,
                               bool strict = true) {
        return read_text(path, encoding, strict).find(needle, pos);
    }

    static text_size_type rfind(const std::filesystem::path& path,
                                LStringDetail::string_view needle,
                                text_size_type pos = text_npos,
                                LEncoding encoding = LEncoding::Unknown,
                                bool strict = true) {
        return read_text(path, encoding, strict).rfind(needle, pos);
    }

    static LString replaced_all(const std::filesystem::path& path,
                                LStringDetail::string_view from,
                                LStringDetail::string_view to,
                                LEncoding encoding = LEncoding::Unknown,
                                bool strict = true) {
        return read_text(path, encoding, strict).replaced_all(from, to);
    }

    static void replace_all(const std::filesystem::path& path,
                            LStringDetail::string_view from,
                            LStringDetail::string_view to,
                            LEncoding read_encoding = LEncoding::Unknown,
                            LEncoding write_encoding = LEncoding::Utf8,
                            bool write_bom = false, bool strict = true,
                            bool create_parent = true) {
        auto text = read_text(path, read_encoding, strict);
        text.replace_all(from, to);
        write_text(path, text.view(), write_encoding, write_bom, strict, create_parent);
    }

    static bool regex_contains(const std::filesystem::path& path,
                               LStringDetail::string_view pattern,
                               LEncoding encoding = LEncoding::Unknown,
                               bool strict = true) {
        return read_text(path, encoding, strict).regex_contains(pattern);
    }

#if LSTRING_HAS_STD_OPTIONAL
    static std::optional<LString> regex_find(const std::filesystem::path& path,
                                             LStringDetail::string_view pattern,
                                             LEncoding encoding = LEncoding::Unknown,
                                             bool strict = true) {
        return read_text(path, encoding, strict).regex_find(pattern);
    }
#endif // LSTRING_HAS_STD_OPTIONAL

    static std::vector<LString> regex_find_all(const std::filesystem::path& path,
                                               LStringDetail::string_view pattern,
                                               LEncoding encoding = LEncoding::Unknown,
                                               bool strict = true) {
        return read_text(path, encoding, strict).regex_find_all(pattern);
    }

    static LString regex_replaced(const std::filesystem::path& path,
                                  LStringDetail::string_view pattern,
                                  LStringDetail::string_view rewrite,
                                  bool replace_all = true,
                                  LEncoding encoding = LEncoding::Unknown,
                                  bool strict = true) {
        return read_text(path, encoding, strict).regex_replaced(pattern, rewrite, replace_all);
    }

    static void regex_replace(const std::filesystem::path& path,
                              LStringDetail::string_view pattern,
                              LStringDetail::string_view rewrite,
                              bool replace_all = true,
                              LEncoding read_encoding = LEncoding::Unknown,
                              LEncoding write_encoding = LEncoding::Utf8,
                              bool write_bom = false, bool strict = true,
                              bool create_parent = true) {
        auto text = read_text(path, read_encoding, strict);
        text.regex_replace(pattern, rewrite, replace_all);
        write_text(path, text.view(), write_encoding, write_bom, strict, create_parent);
    }

    static std::ifstream open_input(const std::filesystem::path& path,
                                    std::ios::openmode mode = std::ios::binary) {
        return LFileDetail::open_input_stream(path, mode);
    }

    static std::ofstream open_output(const std::filesystem::path& path,
                                     LFileWriteMode mode = LFileWriteMode::Truncate,
                                     bool create_parent = true) {
        return open_output(path, LFileDetail::write_open_mode(mode), create_parent);
    }

    static std::ofstream open_output(const std::filesystem::path& path,
                                     std::ios::openmode mode,
                                     bool create_parent = true) {
        return LFileDetail::open_output_stream(path, mode, create_parent);
    }

    static std::fstream open_stream(const std::filesystem::path& path,
                                    std::ios::openmode mode =
                                        std::ios::binary | std::ios::in | std::ios::out,
                                    bool create_parent = false) {
        return LFileDetail::open_io_stream(path, mode, create_parent);
    }

    static std::istream& seekg(std::istream& stream, std::streamoff offset,
                               std::ios::seekdir dir = std::ios::beg) {
        stream.clear();
        stream.seekg(offset, dir);
        if (!stream) {
            throw std::runtime_error("cannot seek input stream");
        }
        return stream;
    }

    static std::ostream& seekp(std::ostream& stream, std::streamoff offset,
                               std::ios::seekdir dir = std::ios::beg) {
        stream.clear();
        stream.seekp(offset, dir);
        if (!stream) {
            throw std::runtime_error("cannot seek output stream");
        }
        return stream;
    }

    static std::streampos tellg(std::istream& stream) {
        auto pos = stream.tellg();
        if (pos == std::streampos(-1)) {
            throw std::runtime_error("cannot tell input stream position");
        }
        return pos;
    }

    static std::streampos tellp(std::ostream& stream) {
        auto pos = stream.tellp();
        if (pos == std::streampos(-1)) {
            throw std::runtime_error("cannot tell output stream position");
        }
        return pos;
    }

    static std::string read_bytes_at(const std::filesystem::path& path,
                                     std::streamoff offset, std::size_t count,
                                     std::ios::seekdir dir = std::ios::beg) {
        auto file = open_input(path, std::ios::binary);
        seekg(file, offset, dir);

        std::string out(count, '\0');
        if (count != 0) {
            file.read(&out[0], static_cast<std::streamsize>(count));
            out.resize(static_cast<std::size_t>(file.gcount()));
            if (!file.eof() && !file) {
                throw LPathDetail::make_error("cannot read file", path);
            }
        }
        return out;
    }

    static std::string read_bytes_from(const std::filesystem::path& path,
                                       std::streamoff offset,
                                       std::ios::seekdir dir = std::ios::beg) {
        auto file = open_input(path, std::ios::binary);
        seekg(file, offset, dir);
        std::ostringstream out;
        out << file.rdbuf();
        if (!file.eof() && file.bad()) {
            throw LPathDetail::make_error("cannot read file", path);
        }
        return out.str();
    }

    static void write_bytes_at(const std::filesystem::path& path,
                               std::streamoff offset,
                               LStringDetail::string_view bytes,
                               std::ios::seekdir dir = std::ios::beg,
                               bool create_parent = true) {
        if (create_parent) {
            LPathDetail::ensure_parent_directory(path);
        }

        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            touch(path, create_parent);
            file.open(path, std::ios::binary | std::ios::in | std::ios::out);
        }
        if (!file) {
            throw LPathDetail::make_error("cannot open file stream", path);
        }

        seekp(file, offset, dir);
        if (!bytes.empty()) {
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        if (!file) {
            throw LPathDetail::make_error("cannot write file", path);
        }
    }

    static void write_bytes(const std::filesystem::path& path, LStringDetail::string_view bytes,
                            bool create_parent = true) {
        LFileDetail::write_all_bytes(path, bytes, LFileWriteMode::Truncate, create_parent);
    }

    static void append_bytes(const std::filesystem::path& path, LStringDetail::string_view bytes,
                             bool create_parent = true) {
        LFileDetail::write_all_bytes(path, bytes, LFileWriteMode::Append, create_parent);
    }

    static void write_text(const std::filesystem::path& path, LStringDetail::string_view text,
                           LEncoding encoding = LEncoding::Utf8, bool write_bom = false,
                           bool strict = true, bool create_parent = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = LEncoding::Utf8;
        }
        LFileDetail::write_all_bytes(path, LString(text).to_encoding(encoding, write_bom, strict),
                                     LFileWriteMode::Truncate, create_parent);
    }

    static void append_text(const std::filesystem::path& path, LStringDetail::string_view text,
                            LEncoding encoding = LEncoding::Utf8, bool write_bom = false,
                            bool strict = true, bool create_parent = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = LEncoding::Utf8;
        }
        LFileDetail::write_all_bytes(path, LString(text).to_encoding(encoding, write_bom, strict),
                                     LFileWriteMode::Append, create_parent);
    }

    template<class Range LTOOL_ENABLE_IF(LTool::traits::is_const_range<Range>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Range>)
    static void write_lines(const std::filesystem::path& path, const Range& lines,
                            LStringDetail::string_view newline = "\n",
                            bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                            bool write_bom = false, bool strict = true,
                            bool create_parent = true) {
        write_text(path, LFileDetail::join_lines(lines, newline, final_newline).view(),
                   encoding, write_bom, strict, create_parent);
    }

    template<class Range LTOOL_ENABLE_IF(LTool::traits::is_const_range<Range>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Range>)
    static void append_lines(const std::filesystem::path& path, const Range& lines,
                             LStringDetail::string_view newline = "\n",
                             bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                             bool write_bom = false, bool strict = true,
                             bool create_parent = true) {
        append_text(path, LFileDetail::join_lines(lines, newline, final_newline).view(),
                    encoding, write_bom, strict, create_parent);
    }

    static void touch(const std::filesystem::path& path, bool create_parent = true) {
        if (create_parent) {
            LPathDetail::ensure_parent_directory(path);
        }
        {
            std::ofstream file(path, std::ios::binary | std::ios::app);
            if (!file) {
                throw LPathDetail::make_error("cannot touch file", path);
            }
        }
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now());
    }

    static LFile create_temp_file(LStringDetail::string_view prefix = "LFile-",
                                  LStringDetail::string_view extension = ".tmp") {
        auto file = LFile(LPath::temp_path(prefix, extension));
        file.touch(false);
        return file;
    }

    std::size_t hash() const noexcept {
        return path_.hash();
    }
};

inline LFile operator""_lf(const char* text, std::size_t size) {
    return LFile(std::filesystem::path(std::string(text, size)));
}

namespace fmt {
template<typename Char>
template<class FormatContext>
typename FormatContext::iterator formatter<LFile, Char, void>::format(
    const LFile& value, FormatContext& ctx) const {
    return formatter<std::filesystem::path, Char>::format(value.path(), ctx);
}
} // namespace fmt

template<>
struct std::hash<LFile> {
    std::size_t operator()(const LFile& value) const noexcept {
        return value.hash();
    }
};

#endif // LFILE_INCLUDE

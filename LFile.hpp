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
 *
 * 主要接口：
 * - 读：read_bytes()/read_text()/read_lines()、open_input()。
 * - 写：write_bytes()/write_text()/write_lines()、append_*、open_output()。
 * - 原子写：atomic_write_bytes()/atomic_write_text()（写同目录临时文件、
 *   fsync 落盘后用 rename 替换原文件，崩溃/磁盘满也不会截断原文件）。
 * - 搜索替换：find()/contains()/replace_all()/regex_replace()（字节或文本），
 *   replace_* 系列同样通过原子写落盘。
 * - 随机访问：read_bytes_at()/write_bytes_at()。
 * - 其它：touch()、remove()、copy_to()/move_to()、sha256()、create_temp_file()。
 *
 * 常用示例：
 * @code
 * LFile file("data.txt");
 * file.write_text("hello\nworld\n");                    // 写入 UTF-8 文本
 * auto text = file.read_text();                         // 读取文本
 * auto lines = file.read_lines();                       // 读取行
 * file.append_text("more\n");
 * file.replace_all("world", "ltool");                   // 原地文本替换（原子落盘）
 *
 * auto bytes = LFile::read_bytes("a.bin");              // 静态函数版
 * auto tmp = LFile::create_temp_file("out", ".txt");    // 临时文件
 * file.atomic_write_text("safe\n");                     // 原子覆盖写
 * @endcode
 */

#ifndef LFILE_INCLUDE
#define LFILE_INCLUDE

#include "detail/LConcepts.hpp"
#include "detail/LSha256.hpp"
#include "LPath.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if LTOOL_PLATFORM_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

/// SHA-256 文件读取缓冲大小。
inline constexpr std::size_t sha256_file_buffer_size = 1024 * 1024;

/// 把写入模式映射为 ios::openmode。
inline std::ios::openmode write_open_mode(LFileWriteMode mode) {
    auto flags = std::ios::binary | std::ios::out;
    if (mode == LFileWriteMode::Append) {
        flags |= std::ios::app;
    } else {
        flags |= std::ios::trunc;
    }
    return flags;
}

/// 读取文件全部字节；失败抛出带路径的异常。
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

/// 计算文件内容的 SHA-256 十六进制摘要（流式读取）。
inline std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw LPathDetail::make_error("cannot open file for reading", path);
    }

    LTool::detail::Sha256 sha256;
    std::vector<char> buffer(sha256_file_buffer_size);

    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto count = file.gcount();
        if (count > 0) {
            sha256.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (file.bad()) {
        throw LPathDetail::make_error("cannot read file", path);
    }
    return sha256.final_hex();
}

/// 把字节整体写入文件（覆盖或追加），可选自动创建父目录。
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
    file.flush();
    if (!file) {
        throw LPathDetail::make_error("cannot write file", path);
    }
    file.close();
    if (!file) {
        throw LPathDetail::make_error("cannot close file after writing", path);
    }
}

/// 把已写好的文件同步到磁盘（fsync/commit）。
inline void sync_file_to_disk(const std::filesystem::path& path) {
#if LTOOL_PLATFORM_WINDOWS
    const int handle = _wopen(path.c_str(), _O_WRONLY, _S_IREAD | _S_IWRITE);
    if (handle < 0) {
        throw LPathDetail::make_error("cannot open file for syncing", path);
    }
    if (_commit(handle) != 0) {
        _close(handle);
        throw LPathDetail::make_error("cannot sync file", path);
    }
    if (_close(handle) != 0) {
        throw LPathDetail::make_error("cannot close file after syncing", path);
    }
#else
    const int handle = ::open(path.c_str(), O_RDONLY);
    if (handle < 0) {
        throw LPathDetail::make_error("cannot open file for syncing", path);
    }
    if (::fsync(handle) != 0) {
        ::close(handle);
        throw LPathDetail::make_error("cannot sync file", path);
    }
    if (::close(handle) != 0) {
        throw LPathDetail::make_error("cannot close file after syncing", path);
    }
#endif
}

/// 原子替换：把临时文件 rename 到目标路径。
///
/// POSIX 上 rename(2) 在同一文件系统内原子覆盖已存在目标；
/// Windows 上 rename 无法覆盖已存在目标，需先删除目标再重命名，
/// 两步之间存在目标文件短暂缺失的窗口期（可接受）。
inline void atomic_replace(const std::filesystem::path& temp_path,
                           const std::filesystem::path& target) {
    std::error_code ec;
#if LTOOL_PLATFORM_WINDOWS
    std::filesystem::remove(target, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        throw LPathDetail::make_error("cannot remove target before replacing", target);
    }
    std::filesystem::rename(temp_path, target, ec);
    if (ec) {
        throw LPathDetail::make_error("cannot replace file", target);
    }
#else
    std::filesystem::rename(temp_path, target, ec);
    if (ec) {
        throw LPathDetail::make_error("cannot replace file", target);
    }
#endif
}

/// 目标文件已存在时，把目标权限复制到临时文件；不存在或失败时静默跳过。
///
/// 原子写通过"新建同目录临时文件 + rename 替换"完成，会改变目标文件的
/// inode；新建文件权限默认按 umask 生成。若目标已存在且不复制权限，
/// 0600 等敏感文件（如 .env、带凭据的配置文件）的权限会被放宽为 0644，
/// 构成安全回归。这里在 rename 前用 stat 读取目标 st_mode，再用 chmod
/// 应用到临时文件，使替换后权限与原文件一致。
///
/// 权限复制是次要步骤：stat/chmod 失败（目标被并发删除、只读介质等）不
/// 影响主流程写入，与失败时清理临时文件的 best-effort 风格一致（见
/// atomic_write_all_bytes 的 catch 块），因此失败时忽略并继续。
inline void copy_existing_permissions(const std::filesystem::path& target,
                                      const std::filesystem::path& temp_path) noexcept {
#if LTOOL_PLATFORM_WINDOWS
    struct _stat st;
    if (_wstat(target.c_str(), &st) != 0) {
        return; // 目标不存在：新文件场景，跳过权限复制
    }
    _wchmod(temp_path.c_str(), st.st_mode);
#else
    struct stat st;
    if (::stat(target.c_str(), &st) != 0) {
        return; // 目标不存在：新文件场景，跳过权限复制
    }
    ::chmod(temp_path.c_str(), st.st_mode & 07777);
#endif
}

/// 原子覆盖写入字节：先写同目录临时文件并 fsync 落盘，再用 rename 替换目标；
/// 任何步骤失败都会清理临时文件并抛出带路径的异常。
///
/// 注意：原子写会改变文件 inode，临时文件权限默认按 umask 生成；若目标已
/// 存在，则继承原文件的权限（best-effort，复制失败忽略并继续），避免敏感
/// 文件权限被放宽。
inline void atomic_write_all_bytes(const std::filesystem::path& path,
                                   LStringDetail::string_view bytes, bool create_parent) {
    if (create_parent) {
        LPathDetail::ensure_parent_directory(path);
    }

    // 临时文件必须与目标同目录，rename 才能在同一文件系统内原子完成。
    const auto parent = path.parent_path();
    const auto directory = parent.empty() ? std::filesystem::path(".") : parent;
    const auto temp_path =
        directory / (path.filename().string() + "." + LPathDetail::unique_token() + ".tmp");

    try {
        {
            std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
            if (!file) {
                throw LPathDetail::make_error("cannot open temporary file for writing", temp_path);
            }
            if (!bytes.empty()) {
                file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
            file.flush();
            if (!file) {
                throw LPathDetail::make_error("cannot write temporary file", temp_path);
            }
            file.close();
            if (!file) {
                throw LPathDetail::make_error("cannot close temporary file", temp_path);
            }
        }
        // rename 前继承原文件权限（best-effort，失败忽略并继续）。
        copy_existing_permissions(path, temp_path);
        sync_file_to_disk(temp_path);
        atomic_replace(temp_path, path);
    } catch (...) {
        // 失败时清理临时文件，避免残留垃圾。
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        throw;
    }
}

/// 打开输入流；失败抛出带路径的异常。
inline std::ifstream open_input_stream(const std::filesystem::path& path,
                                       std::ios::openmode mode) {
    std::ifstream file(path, mode);
    if (!file) {
        throw LPathDetail::make_error("cannot open file for reading", path);
    }
    return file;
}

/// 打开输出流；可选自动创建父目录。
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

/// 打开可读写的 fstream；可选自动创建父目录。
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

/// 把多行文本用换行符拼接成一段文本。
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
    LPath path_; ///< 目标文件路径。

public:
    using path_type = std::filesystem::path;     ///< 路径类型。
    using size_type = std::uintmax_t;            ///< 文件大小类型。
    using file_time_type = std::filesystem::file_time_type; ///< 文件时间类型。
    using text_size_type = LString::size_type;   ///< 文本长度类型。
    using bytes_size_type = std::string::size_type; ///< 字节长度类型。

    static constexpr text_size_type text_npos = LString::npos;       ///< 文本未找到标记。
    static constexpr bytes_size_type bytes_npos = std::string::npos; ///< 字节未找到标记。

    /// 默认构造空路径文件对象。
    LFile() = default;
    /// 复制构造。
    LFile(const LFile&) = default;
    /// 移动构造。
    LFile(LFile&&) noexcept = default;
    /// 复制赋值。
    LFile& operator=(const LFile&) = default;
    /// 移动赋值。
    LFile& operator=(LFile&&) noexcept = default;

    /// 从 LPath 构造。
    LFile(LPath path)
        : path_(std::move(path)) {}

    /// 从 std::filesystem::path 构造。
    LFile(std::filesystem::path path)
        : path_(std::move(path)) {}

    /// 从 C 字符串路径构造。
    LFile(const char* path)
        : path_(path) {}

    /// 从 std::string 路径构造。
    LFile(const std::string& path)
        : path_(path) {}

    /// 从 LString 路径构造。
    LFile(const LString& path)
        : path_(path) {}

    /// 从 string_view 路径构造。
    LFile(LStringDetail::string_view path)
        : path_(path) {}

    /// 从 LPath 赋值。
    LFile& operator=(LPath path) {
        path_ = std::move(path);
        return *this;
    }

    /// 从 std::filesystem::path 赋值。
    LFile& operator=(std::filesystem::path path) {
        path_ = std::move(path);
        return *this;
    }

    /// 从 C 字符串路径赋值。
    LFile& operator=(const char* path) {
        path_ = path;
        return *this;
    }

    /// 从 std::string 路径赋值。
    LFile& operator=(const std::string& path) {
        path_ = path;
        return *this;
    }

    /// 从 LString 路径赋值。
    LFile& operator=(const LString& path) {
        path_ = path;
        return *this;
    }

    /// 隐式暴露底层路径的可写引用。
    operator std::filesystem::path&() & noexcept {
        return path_.path();
    }

    /// 隐式暴露底层路径的只读引用。
    operator const std::filesystem::path&() const& noexcept {
        return path_.path();
    }

    /// 从右值对象移出底层路径。
    operator std::filesystem::path() && noexcept {
        return std::move(path_).path();
    }

    /// 访问底层 LPath 的可写引用。
    LPath& lpath() & noexcept {
        return path_;
    }

    /// 访问底层 LPath 的只读引用。
    const LPath& lpath() const& noexcept {
        return path_;
    }

    /// 从右值对象移出底层 LPath。
    LPath&& lpath() && noexcept {
        return std::move(path_);
    }

    /// 访问底层 std::filesystem::path 的可写引用。
    std::filesystem::path& path() & noexcept {
        return path_.path();
    }

    /// 访问底层 std::filesystem::path 的只读引用。
    const std::filesystem::path& path() const& noexcept {
        return path_.path();
    }

    /// 从右值对象移出底层 std::filesystem::path。
    std::filesystem::path&& path() && noexcept {
        return std::move(path_).path();
    }

    /// 返回路径字符串的 LString 表示。
    LString str() const {
        return path_.str();
    }

    /// 返回路径字符串。
    std::string string() const {
        return path_.string();
    }

    /// 返回原生路径字符串。
    const std::filesystem::path::string_type& native() const noexcept {
        return path_.native();
    }

    /// 判断路径是否为空。
    bool empty() const noexcept {
        return path_.empty();
    }

    /// 判断路径是否存在。
    bool exists() const {
        return path_.exists();
    }

    /// 判断路径是否指向普通文件。
    bool is_file() const {
        return path_.is_file();
    }

    /// 返回文件大小；失败时抛出异常。
    size_type size() const {
        return path_.size();
    }

    /// 返回文件大小；失败时返回 default_value。
    size_type size_or(size_type default_value = 0) const {
        return path_.size_or(default_value);
    }

    /// 返回最后修改时间。
    file_time_type last_write_time() const {
        return path_.last_write_time();
    }

    /// 设置最后修改时间。
    LFile& set_last_write_time(file_time_type time) {
        path_.set_last_write_time(time);
        return *this;
    }

    /// 读取文件全部原始字节。
    std::string read_bytes() const {
        return read_bytes(path());
    }

    /// 读取文件文本（自动检测或指定编码）。
    LString read_text(LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(path(), encoding, strict);
    }

    /// 读取文件文本行。
    std::vector<LString> read_lines(bool keep_empty = true,
                                    LEncoding encoding = LEncoding::Unknown,
                                    bool strict = true) const {
        return read_text(encoding, strict).lines(keep_empty);
    }

    /// 判断文件字节中是否包含指定子串。
    bool bytes_contains(LStringDetail::string_view needle) const {
        return bytes_find(needle) != bytes_npos;
    }

    /// 在文件字节中查找子串位置。
    bytes_size_type bytes_find(LStringDetail::string_view needle,
                               bytes_size_type pos = 0) const {
        return read_bytes().find(needle, pos);
    }

    /// 在文件字节中反向查找子串位置。
    bytes_size_type bytes_rfind(LStringDetail::string_view needle,
                                bytes_size_type pos = bytes_npos) const {
        return read_bytes().rfind(needle, pos);
    }

    /// 原地替换文件字节中的全部子串并原子写回（临时文件 + rename 落盘）。
    LFile& replace_bytes_all(LStringDetail::string_view from, LStringDetail::string_view to,
                             bool create_parent = true) {
        auto bytes = read_bytes();
        LString text(bytes);
        text.replace_all(from, to);
        atomic_write_bytes(text.view(), create_parent);
        return *this;
    }

    /// 判断文件文本中是否包含指定子串。
    bool contains(LStringDetail::string_view needle,
                  LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).contains(needle);
    }

    /// 在文件文本中查找子串位置。
    text_size_type find(LStringDetail::string_view needle, text_size_type pos = 0,
                        LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).find(needle, pos);
    }

    /// 在文件文本中反向查找子串位置。
    text_size_type rfind(LStringDetail::string_view needle,
                         text_size_type pos = text_npos,
                         LEncoding encoding = LEncoding::Unknown,
                         bool strict = true) const {
        return read_text(encoding, strict).rfind(needle, pos);
    }

    /// 返回替换全部子串后的文件文本副本（不写回）。
    LString replaced_all(LStringDetail::string_view from, LStringDetail::string_view to,
                         LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).replaced_all(from, to);
    }

    /// 原地替换文件文本中的全部子串并原子写回（临时文件 + rename 落盘）。
    LFile& replace_all(LStringDetail::string_view from, LStringDetail::string_view to,
                       LEncoding read_encoding = LEncoding::Unknown,
                       LEncoding write_encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true,
                       bool create_parent = true) {
        auto text = read_text(read_encoding, strict);
        text.replace_all(from, to);
        atomic_write_text(text.view(), write_encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 判断文件文本是否匹配正则。
    bool regex_contains(LStringDetail::string_view pattern,
                        LEncoding encoding = LEncoding::Unknown, bool strict = true) const {
        return read_text(encoding, strict).regex_contains(pattern);
    }

#if LSTRING_HAS_STD_OPTIONAL
    /// 查找文件文本中第一个正则匹配。
    std::optional<LString> regex_find(LStringDetail::string_view pattern,
                                      LEncoding encoding = LEncoding::Unknown,
                                      bool strict = true) const {
        return read_text(encoding, strict).regex_find(pattern);
    }
#endif // LSTRING_HAS_STD_OPTIONAL

    /// 查找文件文本中所有正则匹配。
    std::vector<LString> regex_find_all(LStringDetail::string_view pattern,
                                        LEncoding encoding = LEncoding::Unknown,
                                        bool strict = true) const {
        return read_text(encoding, strict).regex_find_all(pattern);
    }

    /// 返回替换正则匹配后的文件文本副本（不写回）。
    LString regex_replaced(LStringDetail::string_view pattern, LStringDetail::string_view rewrite,
                           bool replace_all = true,
                           LEncoding encoding = LEncoding::Unknown,
                           bool strict = true) const {
        return read_text(encoding, strict).regex_replaced(pattern, rewrite, replace_all);
    }

    /// 原地替换文件文本中的正则匹配并原子写回（临时文件 + rename 落盘）。
    LFile& regex_replace(LStringDetail::string_view pattern, LStringDetail::string_view rewrite,
                         bool replace_all = true,
                         LEncoding read_encoding = LEncoding::Unknown,
                         LEncoding write_encoding = LEncoding::Utf8,
                         bool write_bom = false, bool strict = true,
                         bool create_parent = true) {
        auto text = read_text(read_encoding, strict);
        text.regex_replace(pattern, rewrite, replace_all);
        atomic_write_text(text.view(), write_encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 打开输入流。
    std::ifstream open_input(std::ios::openmode mode = std::ios::binary) const {
        return open_input(path(), mode);
    }

    /// 打开输出流（按写模式）。
    std::ofstream open_output(LFileWriteMode mode = LFileWriteMode::Truncate,
                              bool create_parent = true) const {
        return open_output(path(), mode, create_parent);
    }

    /// 打开输出流（按 openmode）。
    std::ofstream open_output(std::ios::openmode mode, bool create_parent = true) const {
        return open_output(path(), mode, create_parent);
    }

    /// 打开可读写流。
    std::fstream open_stream(std::ios::openmode mode =
                                 std::ios::binary | std::ios::in | std::ios::out,
                             bool create_parent = false) const {
        return open_stream(path(), mode, create_parent);
    }

    /// 从指定偏移读取 count 字节。
    std::string read_bytes_at(std::streamoff offset, std::size_t count,
                              std::ios::seekdir dir = std::ios::beg) const {
        return read_bytes_at(path(), offset, count, dir);
    }

    /// 从指定偏移读取到文件末尾。
    std::string read_bytes_from(std::streamoff offset,
                                std::ios::seekdir dir = std::ios::beg) const {
        return read_bytes_from(path(), offset, dir);
    }

    /// 从指定偏移写入字节。
    LFile& write_bytes_at(std::streamoff offset, LStringDetail::string_view bytes,
                          std::ios::seekdir dir = std::ios::beg,
                          bool create_parent = true) {
        write_bytes_at(path(), offset, bytes, dir, create_parent);
        return *this;
    }

    /// 覆盖写入原始字节。
    LFile& write_bytes(LStringDetail::string_view bytes, bool create_parent = true) {
        write_bytes(path(), bytes, create_parent);
        return *this;
    }

    /// 追加写入原始字节。
    LFile& append_bytes(LStringDetail::string_view bytes, bool create_parent = true) {
        append_bytes(path(), bytes, create_parent);
        return *this;
    }

    /// 原子覆盖写入原始字节（写同目录临时文件 + fsync + rename 替换）。
    LFile& atomic_write_bytes(LStringDetail::string_view bytes, bool create_parent = true) {
        atomic_write_bytes(path(), bytes, create_parent);
        return *this;
    }

    /// 原子覆盖写入文本（按编码转换，写同目录临时文件 + fsync + rename 替换）。
    LFile& atomic_write_text(LStringDetail::string_view text, LEncoding encoding = LEncoding::Utf8,
                             bool write_bom = false, bool strict = true,
                             bool create_parent = true) {
        atomic_write_text(path(), text, encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 覆盖写入文本（按编码转换）。
    LFile& write_text(LStringDetail::string_view text, LEncoding encoding = LEncoding::Utf8,
                      bool write_bom = false, bool strict = true, bool create_parent = true) {
        write_text(path(), text, encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 追加写入文本（按编码转换）。
    LFile& append_text(LStringDetail::string_view text, LEncoding encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true, bool create_parent = true) {
        append_text(path(), text, encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 覆盖写入多行文本。
    template<class Range LTOOL_ENABLE_IF(LTool::traits::is_const_range<Range>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Range>)
    LFile& write_lines(const Range& lines, LStringDetail::string_view newline = "\n",
                       bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                       bool write_bom = false, bool strict = true, bool create_parent = true) {
        write_text(LFileDetail::join_lines(lines, newline, final_newline).view(),
                   encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 追加写入多行文本。
    template<class Range LTOOL_ENABLE_IF(LTool::traits::is_const_range<Range>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Range>)
    LFile& append_lines(const Range& lines, LStringDetail::string_view newline = "\n",
                        bool final_newline = true, LEncoding encoding = LEncoding::Utf8,
                        bool write_bom = false, bool strict = true, bool create_parent = true) {
        append_text(LFileDetail::join_lines(lines, newline, final_newline).view(),
                    encoding, write_bom, strict, create_parent);
        return *this;
    }

    /// 创建空文件或刷新时间戳。
    LFile& touch(bool create_parent = true) {
        touch(path(), create_parent);
        return *this;
    }

    /// 删除文件；不存在返回 false。
    bool remove() const {
        return std::filesystem::remove(path());
    }

    /// 复制文件到目标路径。
    bool copy_to(const std::filesystem::path& to, bool overwrite = false) const {
        auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                                 : std::filesystem::copy_options::none;
        return std::filesystem::copy_file(path(), to, options);
    }

    /// 移动/重命名文件，并更新对象路径。
    LFile& move_to(const std::filesystem::path& to) {
        std::filesystem::rename(path(), to);
        path_ = to;
        return *this;
    }

    /// move_to() 的同义函数。
    LFile& rename_to(const std::filesystem::path& to) {
        return move_to(to);
    }

    /// 计算文件内容的 SHA-256 摘要。
    std::string sha256() const {
        return sha256(path());
    }

    /// 按路径比较两个 LFile。
    friend bool operator==(const LFile& lhs, const LFile& rhs) noexcept {
        return lhs.path_ == rhs.path_;
    }

    /// 与 std::filesystem::path 比较。
    friend bool operator==(const LFile& lhs, const std::filesystem::path& rhs) noexcept {
        return lhs.path_ == rhs;
    }

    /// 与 std::filesystem::path 比较（反向）。
    friend bool operator==(const std::filesystem::path& lhs, const LFile& rhs) noexcept {
        return lhs == rhs.path_;
    }

#if LTOOL_HAS_CPP20
    /// 按路径三路比较。
    friend auto operator<=>(const LFile&, const LFile&) = default;
#endif // LTOOL_HAS_CPP20

    /// 把路径字符串写入输出流。
    friend std::ostream& operator<<(std::ostream& os, const LFile& value) {
        return os << value.path().string();
    }

    /// 读取文件全部原始字节。
    static std::string read_bytes(const std::filesystem::path& path) {
        return LFileDetail::read_all_bytes(path);
    }

    /// 读取文件文本（自动检测或指定编码）。
    static LString read_text(const std::filesystem::path& path,
                             LEncoding encoding = LEncoding::Unknown,
                             bool strict = true) {
        return LString::from_encoding(read_bytes(path), encoding, strict);
    }

    /// 读取文件文本行。
    static std::vector<LString> read_lines(const std::filesystem::path& path,
                                           bool keep_empty = true,
                                           LEncoding encoding = LEncoding::Unknown,
                                           bool strict = true) {
        return read_text(path, encoding, strict).lines(keep_empty);
    }

    /// 判断文件字节中是否包含指定子串。
    static bool bytes_contains(const std::filesystem::path& path,
                               LStringDetail::string_view needle) {
        return bytes_find(path, needle) != bytes_npos;
    }

    /// 在文件字节中查找子串位置。
    static bytes_size_type bytes_find(const std::filesystem::path& path,
                                      LStringDetail::string_view needle,
                                      bytes_size_type pos = 0) {
        return read_bytes(path).find(needle, pos);
    }

    /// 在文件字节中反向查找子串位置。
    static bytes_size_type bytes_rfind(const std::filesystem::path& path,
                                       LStringDetail::string_view needle,
                                       bytes_size_type pos = bytes_npos) {
        return read_bytes(path).rfind(needle, pos);
    }

    /// 原地替换文件字节中的全部子串并原子写回（临时文件 + rename 落盘）。
    static void replace_bytes_all(const std::filesystem::path& path,
                                  LStringDetail::string_view from,
                                  LStringDetail::string_view to,
                                  bool create_parent = true) {
        auto bytes = read_bytes(path);
        LString text(bytes);
        text.replace_all(from, to);
        atomic_write_bytes(path, text.view(), create_parent);
    }

    /// 判断文件文本中是否包含指定子串。
    static bool contains(const std::filesystem::path& path, LStringDetail::string_view needle,
                         LEncoding encoding = LEncoding::Unknown, bool strict = true) {
        return read_text(path, encoding, strict).contains(needle);
    }

    /// 在文件文本中查找子串位置。
    static text_size_type find(const std::filesystem::path& path,
                               LStringDetail::string_view needle,
                               text_size_type pos = 0,
                               LEncoding encoding = LEncoding::Unknown,
                               bool strict = true) {
        return read_text(path, encoding, strict).find(needle, pos);
    }

    /// 在文件文本中反向查找子串位置。
    static text_size_type rfind(const std::filesystem::path& path,
                                LStringDetail::string_view needle,
                                text_size_type pos = text_npos,
                                LEncoding encoding = LEncoding::Unknown,
                                bool strict = true) {
        return read_text(path, encoding, strict).rfind(needle, pos);
    }

    /// 返回替换全部子串后的文件文本副本（不写回）。
    static LString replaced_all(const std::filesystem::path& path,
                                LStringDetail::string_view from,
                                LStringDetail::string_view to,
                                LEncoding encoding = LEncoding::Unknown,
                                bool strict = true) {
        return read_text(path, encoding, strict).replaced_all(from, to);
    }

    /// 原地替换文件文本中的全部子串并原子写回（临时文件 + rename 落盘）。
    static void replace_all(const std::filesystem::path& path,
                            LStringDetail::string_view from,
                            LStringDetail::string_view to,
                            LEncoding read_encoding = LEncoding::Unknown,
                            LEncoding write_encoding = LEncoding::Utf8,
                            bool write_bom = false, bool strict = true,
                            bool create_parent = true) {
        auto text = read_text(path, read_encoding, strict);
        text.replace_all(from, to);
        atomic_write_text(path, text.view(), write_encoding, write_bom, strict, create_parent);
    }

    /// 判断文件文本是否匹配正则。
    static bool regex_contains(const std::filesystem::path& path,
                               LStringDetail::string_view pattern,
                               LEncoding encoding = LEncoding::Unknown,
                               bool strict = true) {
        return read_text(path, encoding, strict).regex_contains(pattern);
    }

#if LSTRING_HAS_STD_OPTIONAL
    /// 查找文件文本中第一个正则匹配。
    static std::optional<LString> regex_find(const std::filesystem::path& path,
                                             LStringDetail::string_view pattern,
                                             LEncoding encoding = LEncoding::Unknown,
                                             bool strict = true) {
        return read_text(path, encoding, strict).regex_find(pattern);
    }
#endif // LSTRING_HAS_STD_OPTIONAL

    /// 查找文件文本中所有正则匹配。
    static std::vector<LString> regex_find_all(const std::filesystem::path& path,
                                               LStringDetail::string_view pattern,
                                               LEncoding encoding = LEncoding::Unknown,
                                               bool strict = true) {
        return read_text(path, encoding, strict).regex_find_all(pattern);
    }

    /// 返回替换正则匹配后的文件文本副本（不写回）。
    static LString regex_replaced(const std::filesystem::path& path,
                                  LStringDetail::string_view pattern,
                                  LStringDetail::string_view rewrite,
                                  bool replace_all = true,
                                  LEncoding encoding = LEncoding::Unknown,
                                  bool strict = true) {
        return read_text(path, encoding, strict).regex_replaced(pattern, rewrite, replace_all);
    }

    /// 原地替换文件文本中的正则匹配并原子写回（临时文件 + rename 落盘）。
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
        atomic_write_text(path, text.view(), write_encoding, write_bom, strict, create_parent);
    }

    /// 打开输入流。
    static std::ifstream open_input(const std::filesystem::path& path,
                                    std::ios::openmode mode = std::ios::binary) {
        return LFileDetail::open_input_stream(path, mode);
    }

    /// 打开输出流（按写模式）。
    static std::ofstream open_output(const std::filesystem::path& path,
                                     LFileWriteMode mode = LFileWriteMode::Truncate,
                                     bool create_parent = true) {
        return open_output(path, LFileDetail::write_open_mode(mode), create_parent);
    }

    /// 打开输出流（按 openmode）。
    static std::ofstream open_output(const std::filesystem::path& path,
                                     std::ios::openmode mode,
                                     bool create_parent = true) {
        return LFileDetail::open_output_stream(path, mode, create_parent);
    }

    /// 打开可读写流。
    static std::fstream open_stream(const std::filesystem::path& path,
                                    std::ios::openmode mode =
                                        std::ios::binary | std::ios::in | std::ios::out,
                                    bool create_parent = false) {
        return LFileDetail::open_io_stream(path, mode, create_parent);
    }

    /// 定位输入流到指定偏移；失败抛出异常。
    static std::istream& seekg(std::istream& stream, std::streamoff offset,
                               std::ios::seekdir dir = std::ios::beg) {
        stream.clear();
        stream.seekg(offset, dir);
        if (!stream) {
            throw std::runtime_error("cannot seek input stream");
        }
        return stream;
    }

    /// 定位输出流到指定偏移；失败抛出异常。
    static std::ostream& seekp(std::ostream& stream, std::streamoff offset,
                               std::ios::seekdir dir = std::ios::beg) {
        stream.clear();
        stream.seekp(offset, dir);
        if (!stream) {
            throw std::runtime_error("cannot seek output stream");
        }
        return stream;
    }

    /// 返回输入流当前位置；失败抛出异常。
    static std::streampos tellg(std::istream& stream) {
        auto pos = stream.tellg();
        if (pos == std::streampos(-1)) {
            throw std::runtime_error("cannot tell input stream position");
        }
        return pos;
    }

    /// 返回输出流当前位置；失败抛出异常。
    static std::streampos tellp(std::ostream& stream) {
        auto pos = stream.tellp();
        if (pos == std::streampos(-1)) {
            throw std::runtime_error("cannot tell output stream position");
        }
        return pos;
    }

    /// 从指定偏移读取 count 字节。
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

    /// 从指定偏移读取到文件末尾。
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

    /// 从指定偏移写入字节。
    static void write_bytes_at(const std::filesystem::path& path,
                               std::streamoff offset,
                               LStringDetail::string_view bytes,
                               std::ios::seekdir dir = std::ios::beg,
                               bool create_parent = true) {
        if (create_parent) {
            LPathDetail::ensure_parent_directory(path);
        }

        // 文件不存在时先创建再打开。
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
        file.flush();
        if (!file) {
            throw LPathDetail::make_error("cannot write file", path);
        }
        file.close();
        if (!file) {
            throw LPathDetail::make_error("cannot close file after writing", path);
        }
    }

    /// 覆盖写入原始字节。
    static void write_bytes(const std::filesystem::path& path, LStringDetail::string_view bytes,
                            bool create_parent = true) {
        LFileDetail::write_all_bytes(path, bytes, LFileWriteMode::Truncate, create_parent);
    }

    /// 追加写入原始字节。
    static void append_bytes(const std::filesystem::path& path, LStringDetail::string_view bytes,
                             bool create_parent = true) {
        LFileDetail::write_all_bytes(path, bytes, LFileWriteMode::Append, create_parent);
    }

    /// 覆盖写入文本（按编码转换）。
    static void write_text(const std::filesystem::path& path, LStringDetail::string_view text,
                           LEncoding encoding = LEncoding::Utf8, bool write_bom = false,
                           bool strict = true, bool create_parent = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = LEncoding::Utf8;
        }
        LFileDetail::write_all_bytes(path, LString(text).to_encoding(encoding, write_bom, strict),
                                     LFileWriteMode::Truncate, create_parent);
    }

    /// 追加写入文本（按编码转换）。
    static void append_text(const std::filesystem::path& path, LStringDetail::string_view text,
                            LEncoding encoding = LEncoding::Utf8, bool write_bom = false,
                            bool strict = true, bool create_parent = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = LEncoding::Utf8;
        }
        LFileDetail::write_all_bytes(path, LString(text).to_encoding(encoding, write_bom, strict),
                                     LFileWriteMode::Append, create_parent);
    }

    /// 原子覆盖写入原始字节（写同目录临时文件 + fsync + rename 替换）。
    static void atomic_write_bytes(const std::filesystem::path& path,
                                   LStringDetail::string_view bytes,
                                   bool create_parent = true) {
        LFileDetail::atomic_write_all_bytes(path, bytes, create_parent);
    }

    /// 原子覆盖写入文本（按编码转换，写同目录临时文件 + fsync + rename 替换）。
    static void atomic_write_text(const std::filesystem::path& path,
                                  LStringDetail::string_view text,
                                  LEncoding encoding = LEncoding::Utf8, bool write_bom = false,
                                  bool strict = true, bool create_parent = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = LEncoding::Utf8;
        }
        LFileDetail::atomic_write_all_bytes(
            path, LString(text).to_encoding(encoding, write_bom, strict), create_parent);
    }

    /// 覆盖写入多行文本。
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

    /// 追加写入多行文本。
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

    /// 创建空文件或刷新时间戳。
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

    /// 在系统临时目录创建唯一临时文件并返回 LFile；失败时清理并抛异常。
    static LFile create_temp_file(LStringDetail::string_view prefix = "LFile-",
                                   LStringDetail::string_view extension = ".tmp") {
        const auto directory = LPath::temp_directory().path();
        for (int i = 0; i < 100; ++i) {
            const auto name = std::string(prefix) + LPathDetail::unique_token() +
                              std::string(extension);
            const auto path = directory / name;
#if LTOOL_PLATFORM_WINDOWS
            const int handle = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                                      _S_IREAD | _S_IWRITE);
#else
            const int handle = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL,
                                      S_IRUSR | S_IWUSR);
#endif
            if (handle >= 0) {
#if LTOOL_PLATFORM_WINDOWS
                if (_close(handle) != 0) {
#else
                if (::close(handle) != 0) {
#endif
                    std::error_code ec;
                    std::filesystem::remove(path, ec);
                    throw LPathDetail::make_error("cannot close temporary file", path);
                }
                return LFile(path);
            }
            if (errno != EEXIST) {
                throw LPathDetail::make_error("cannot create temporary file", path);
            }
        }
        throw std::runtime_error("cannot create unique temporary file");
    }

    /// 计算文件内容的 SHA-256 摘要。
    static std::string sha256(const std::filesystem::path& path) {
        return LFileDetail::sha256_file(path);
    }

    /// 返回路径哈希。
    std::size_t path_hash() const noexcept {
        return path_.hash();
    }
};

/// 用户自定义字面量，用于从字符串字面量简洁构造 LFile。
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
        return value.path_hash();
    }
};

#endif // LFILE_INCLUDE

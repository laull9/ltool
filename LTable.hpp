/**
 * @file LTable.hpp
 * @brief tabulate 的纯头文件薄封装，提供更顺手的命令行表格构建入口。
 *
 * LTable 的核心定位：
 * - 内部直接保存 tabulate::Table，保留 native() 访问底层能力。
 * - add_row() 接收 tabulate 原生行、initializer_list 和常见容器。
 * - add_values() / header_values() 可把 int、double 等可流式输出的值自动转成单元格。
 * - shape() 返回逻辑行列数；display_shape() 可取得 tabulate 原生渲染宽高。
 * - 默认使用现代 Unicode 边框，并开启多字节字符宽度支持。
 * - 宽单元格会按显示宽度自动换行，默认最多 3 行，超出后用省略号折叠。
 * - str()、print()、markdown() 等方法提供常用输出路径。
 *
 * 常用示例：
 * @code
 * LTable table;
 * table.max_cell_width(24).max_cell_rows(3)
 *      .header({"Name", "Score"})
 *      .add_values("Alice", 98)
 *      .add_row(std::vector<std::string>{"Bob", "87"});
 *
 * std::cout << table << std::endl;
 * auto markdown = table.markdown();
 * @endcode
 */

#ifndef LTOOL_LTABLE_INCLUDE
#define LTOOL_LTABLE_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"
#include "pkgs/tabulate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace LTableDetail {

/// 剥除 cv 和引用后的类型。
template<class T>
struct remove_cvref {
    using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<class T>
using remove_cvref_t = typename remove_cvref<T>::type;

/// 判断类型是否可直接转成文本（const char* 或 std::string）。
template<class T>
struct is_text_like
    : std::integral_constant<bool,
                             std::is_convertible<T, const char*>::value ||
                                 std::is_same<remove_cvref_t<T>, std::string>::value> {};

/// 用流式输出把任意可打印值转为字符串。
template<class T>
inline std::string stream_to_string(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

/// 判断字节是否为 UTF-8 连续字节（10xxxxxx）。
inline bool is_utf8_continuation(unsigned char ch) {
    return (ch & 0xc0U) == 0x80U;
}

/// 从 pos 处解码一个 Unicode 码点，并把原始字节写入 bytes。
inline std::uint32_t next_codepoint(const std::string& text,
                                    std::size_t& pos,
                                    std::string& bytes) {
    bytes.clear();
    if (pos >= text.size()) {
        return 0;
    }

    const auto first = static_cast<unsigned char>(text[pos]);
    std::size_t length = 1;
    std::uint32_t codepoint = first;

    if ((first & 0x80U) == 0) {
        length = 1;
        codepoint = first;
    } else if ((first & 0xe0U) == 0xc0U && pos + 1 < text.size()) {
        length = 2;
        codepoint = first & 0x1fU;
    } else if ((first & 0xf0U) == 0xe0U && pos + 2 < text.size()) {
        length = 3;
        codepoint = first & 0x0fU;
    } else if ((first & 0xf8U) == 0xf0U && pos + 3 < text.size()) {
        length = 4;
        codepoint = first & 0x07U;
    }

    // 校验连续字节，非法时退回单字节处理。
    if (length > 1) {
        for (std::size_t i = 1; i < length; ++i) {
            const auto next = static_cast<unsigned char>(text[pos + i]);
            if (!is_utf8_continuation(next)) {
                length = 1;
                codepoint = first;
                break;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
    }

    bytes.assign(text.data() + pos, length);
    pos += length;
    return codepoint;
}

/// 判断码点是否为组合字符（宽度为 0）。
inline bool is_combining_codepoint(std::uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
           (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff) ||
           (cp >= 0xfe20 && cp <= 0xfe2f);
}

/// 判断码点是否为宽字符（显示宽度为 2）。
inline bool is_wide_codepoint(std::uint32_t cp) {
    return (cp >= 0x1100 &&
            (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
             (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) ||
             (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff) ||
             (cp >= 0xfe10 && cp <= 0xfe19) || (cp >= 0xfe30 && cp <= 0xfe6f) ||
             (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6) ||
             (cp >= 0x1f300 && cp <= 0x1faff) || (cp >= 0x20000 && cp <= 0x3fffd)));
}

/// 返回码点的终端显示宽度（0/1/2，制表符按 4 计）。
inline std::size_t codepoint_width(std::uint32_t cp) {
    if (cp == 0 || cp == '\r' || cp == '\n') {
        return 0;
    }
    if (cp == '\t') {
        return 4;
    }
    if (cp < 32 || (cp >= 0x7f && cp < 0xa0) || is_combining_codepoint(cp)) {
        return 0;
    }
    return is_wide_codepoint(cp) ? 2 : 1;
}

/// 计算文本的终端显示宽度（按码点累加）。
inline std::size_t display_width(const std::string& text) {
    std::size_t width = 0;
    std::size_t pos = 0;
    std::string bytes;
    while (pos < text.size()) {
        width += codepoint_width(next_codepoint(text, pos, bytes));
    }
    return width;
}

/// 判断码点是否为空白。
inline bool is_space_codepoint(std::uint32_t cp) {
    return cp == ' ' || cp == '\t';
}

/// 移除文本尾部的空白码点。
inline std::string trim_right_spaces(const std::string& text) {
    std::vector<std::pair<std::string, std::uint32_t>> chars;
    std::size_t pos = 0;
    std::string bytes;
    while (pos < text.size()) {
        const auto cp = next_codepoint(text, pos, bytes);
        chars.push_back(std::make_pair(bytes, cp));
    }
    while (!chars.empty() && is_space_codepoint(chars.back().second)) {
        chars.pop_back();
    }

    std::string out;
    for (const auto& item : chars) {
        out += item.first;
    }
    return out;
}

/// 把一行文本按显示宽度折行，返回若干行。
inline std::vector<std::string> wrap_one_line(const std::string& text, std::size_t width) {
    if (width == 0 || display_width(text) <= width) {
        return std::vector<std::string> {text};
    }

    std::vector<std::string> lines;
    std::string line;
    std::size_t line_width = 0;
    std::size_t pos = 0;
    std::string bytes;
    while (pos < text.size()) {
        const auto cp = next_codepoint(text, pos, bytes);
        const auto char_width = codepoint_width(cp);

        // 当前行已满，收尾并开启新行。
        if (line_width > 0 && line_width + char_width > width) {
            lines.push_back(trim_right_spaces(line));
            line.clear();
            line_width = 0;
            if (is_space_codepoint(cp)) {
                continue;
            }
        }

        line += bytes;
        line_width += char_width;
    }

    lines.push_back(trim_right_spaces(line));
    return lines;
}

/// 按显示宽度折行，保留原有换行。
inline std::vector<std::string> wrap_text(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto part =
            end == std::string::npos ? text.substr(start) : text.substr(start, end - start);
        const auto wrapped = wrap_one_line(part, width);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

/// 截断文本到指定显示宽度。
inline std::string truncate_to_width(const std::string& text, std::size_t width) {
    if (width == 0) {
        return std::string();
    }

    std::string out;
    std::size_t out_width = 0;
    std::size_t pos = 0;
    std::string bytes;
    while (pos < text.size()) {
        const auto old_pos = pos;
        const auto cp = next_codepoint(text, pos, bytes);
        const auto char_width = codepoint_width(cp);
        if (out_width + char_width > width) {
            pos = old_pos;
            break;
        }
        out += bytes;
        out_width += char_width;
    }
    return trim_right_spaces(out);
}

/// 用换行拼接多行文本。
inline std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            out.push_back('\n');
        }
        out += lines[i];
    }
    return out;
}

/// 按宽度折行、限制行数并加省略号折叠文本。
inline std::string fold_text(const std::string& text,
                             std::size_t width,
                             std::size_t max_rows,
                             const std::string& ellipsis) {
    if (width == 0 || max_rows == 0) {
        return text;
    }

    auto lines = wrap_text(text, width);
    if (lines.size() <= max_rows) {
        return join_lines(lines);
    }

    // 超出最大行数：保留前 max_rows-1 行，最后一行截断并追加省略号。
    lines.resize(max_rows);
    const auto ellipsis_width = (std::max<std::size_t>)(display_width(ellipsis), 1);
    if (width <= ellipsis_width) {
        lines.back() = ellipsis;
    } else {
        lines.back() = truncate_to_width(lines.back(), width - ellipsis_width) + ellipsis;
    }
    return join_lines(lines);
}

} // namespace LTableDetail

/**
 * @brief tabulate::Table 的 ltool 风格便捷封装。
 */
class LTable {
public:
    using table_type = tabulate::Table;      ///< 底层表格类型。
    using row_type = table_type::Row_t;      ///< 行类型。
    using cell_type = row_type::value_type;  ///< 单元格类型。
    using row_view = tabulate::Row;          ///< 行视图。
    using column_view = tabulate::Column;    ///< 列视图。
    using format_type = tabulate::Format;    ///< 格式类型。
    using color_type = tabulate::Color;      ///< 颜色类型。
    using font_align_type = tabulate::FontAlign; ///< 对齐类型。
    using font_style_type = tabulate::FontStyle; ///< 字体样式类型。

    /// 构造空表格并应用默认 Unicode 风格。
    LTable() {
        apply_unicode_style();
    }

    /// 用表头行构造表格。
    explicit LTable(const row_type& header_cells) {
        header(header_cells);
    }

    /// 用表头 initializer_list 构造表格。
    explicit LTable(std::initializer_list<cell_type> header_cells) {
        header(header_cells);
    }

    /// 访问底层 tabulate::Table 的可写引用。
    table_type& native() noexcept {
        return table_;
    }

    /// 访问底层 tabulate::Table 的只读引用。
    const table_type& native() const noexcept {
        return table_;
    }

    /// 隐式转换为底层表格可写引用。
    operator table_type&() noexcept {
        return table_;
    }

    /// 隐式转换为底层表格只读引用。
    operator const table_type&() const noexcept {
        return table_;
    }

    /// 追加一行（tabulate 原生行），自动应用单元格折叠与 Unicode 风格。
    LTable& add_row(const row_type& cells) {
        table_.add_row(normalize_row(cells));
        apply_unicode_style();
        return *this;
    }

    /// 追加一行（initializer_list 版本）。
    LTable& add_row(std::initializer_list<cell_type> cells) {
        return add_row(row_type(cells));
    }

    /// 追加一行（任意元素可流式输出的容器版本）。
    template<class Cells
             LTOOL_ENABLE_IF(LTool::traits::is_const_range<Cells>::value &&
                             !LTableDetail::is_text_like<Cells>::value &&
                             !std::is_same<LTableDetail::remove_cvref_t<Cells>, row_type>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Cells> &&
                       !LTableDetail::is_text_like<Cells>::value &&
                       !std::same_as<LTableDetail::remove_cvref_t<Cells>, row_type>)
    LTable& add_row(const Cells& cells) {
        return add_row(make_row(cells));
    }

    /// 追加一行，参数为多个可流式输出的值。
    template<class... Values>
    LTable& add_values(const Values&... values) {
        return add_row(make_row_values(values...));
    }

    /// 设置表头行（原生行）。
    LTable& header(const row_type& cells) {
        return add_row(cells);
    }

    /// 设置表头行（initializer_list 版本）。
    LTable& header(std::initializer_list<cell_type> cells) {
        return header(row_type(cells));
    }

    /// 设置表头行（容器版本）。
    template<class Cells
             LTOOL_ENABLE_IF(LTool::traits::is_const_range<Cells>::value &&
                             !LTableDetail::is_text_like<Cells>::value &&
                             !std::is_same<LTableDetail::remove_cvref_t<Cells>, row_type>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Cells> &&
                       !LTableDetail::is_text_like<Cells>::value &&
                       !std::same_as<LTableDetail::remove_cvref_t<Cells>, row_type>)
    LTable& header(const Cells& cells) {
        return header(make_row(cells));
    }

    /// 设置表头行，参数为多个可流式输出的值。
    template<class... Values>
    LTable& header_values(const Values&... values) {
        return header(make_row_values(values...));
    }

    /// 批量追加多行（行集合）。
    template<class Rows LTOOL_ENABLE_IF(LTool::traits::is_const_range<Rows>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Rows>)
    LTable& add_rows(const Rows& rows) {
        for (const auto& row : rows) {
            add_row(row);
        }
        return *this;
    }

    /// 返回指定行的可写视图。
    row_view& row(std::size_t index) {
        return table_.row(index);
    }

    /// 下标访问行视图。
    row_view& operator[](std::size_t index) {
        return row(index);
    }

    /// 返回指定列的视图。
    column_view column(std::size_t index) {
        return table_.column(index);
    }

    /// 访问表格级格式对象。
    format_type& format() {
        return table_.format();
    }

    /// 设置单元格最大显示宽度。
    LTable& max_cell_width(std::size_t value) {
        max_cell_width_ = value;
        return *this;
    }

    /// 返回单元格最大显示宽度。
    std::size_t max_cell_width() const noexcept {
        return max_cell_width_;
    }

    /// 设置单元格最大显示行数。
    LTable& max_cell_rows(std::size_t value) {
        max_cell_rows_ = value;
        return *this;
    }

    /// 返回单元格最大显示行数。
    std::size_t max_cell_rows() const noexcept {
        return max_cell_rows_;
    }

    /// rows() 的同义函数。
    std::size_t size() const {
        return rows();
    }

    /// 返回当前行数。
    std::size_t rows() const {
        return table_.size();
    }

    /// 返回当前列数；空表返回 0。
    std::size_t columns() const {
        if (rows() == 0) {
            return 0;
        }

        auto copy = table_;
        return copy.row(0).size();
    }

    /// 返回 (行数, 列数) 逻辑形状。
    std::pair<std::size_t, std::size_t> shape() const {
        return std::make_pair(rows(), columns());
    }

    /// 返回 tabulate 原生渲染后的形状。
    std::pair<std::size_t, std::size_t> display_shape() const {
        auto copy = table_;
        return copy.shape();
    }

    /// 把表格渲染后输出到流。
    void print(std::ostream& stream = std::cout) const {
        auto copy = table_;
        copy.print(stream);
    }

    /// 返回渲染后的表格文本。
    std::string str() const {
        auto copy = table_;
        return copy.str();
    }

    /// 导出为 Markdown 表格。
    std::string markdown() const {
        auto copy = table_;
        tabulate::MarkdownExporter exporter;
        return exporter.dump(copy);
    }

    /// 导出为 LaTeX 表格。
    std::string latex() const {
        auto copy = table_;
        tabulate::LatexExporter exporter;
        return exporter.dump(copy);
    }

    /// 导出为 AsciiDoc 表格。
    std::string asciidoc() const {
        auto copy = table_;
        tabulate::AsciiDocExporter exporter;
        return exporter.dump(copy);
    }

    /// 从元素容器构造一行（原生行）。
    template<class Cells LTOOL_ENABLE_IF(LTool::traits::is_const_range<Cells>::value)>
        LTOOL_REQUIRES(LTool::concepts::ConstRange<Cells>)
    static row_type make_row(const Cells& cells) {
        row_type row;
        for (const auto& cell : cells) {
            append_cell(row, cell);
        }
        return row;
    }

    /// 从多个可流式输出值构造一行。
    template<class... Values>
    static row_type make_row_values(const Values&... values) {
        row_type row;
        add_values_to_row(row, values...);
        return row;
    }

private:
    /// 把单元格内容转为可折叠的纯文本。
    static std::string cell_to_string(const cell_type& cell) {
        if (holds_alternative<std::string>(cell)) {
            return *get_if<std::string>(&cell);
        }
        if (holds_alternative<const char*>(cell)) {
            const auto* text = *get_if<const char*>(&cell);
            return text ? std::string(text) : std::string();
        }
        if (holds_alternative<string_view>(cell)) {
            return std::string(*get_if<string_view>(&cell));
        }

        auto table = *get_if<table_type>(&cell);
        std::ostringstream out;
        table.print(out);
        return out.str();
    }

    /// 按宽度和行数折叠单元格文本。
    std::string normalize_text(const std::string& text) const {
        return LTableDetail::fold_text(text, max_cell_width_, max_cell_rows_, ellipsis_);
    }

    /// 归一化整行：把每个单元格文本折叠为纯文本。
    row_type normalize_row(const row_type& cells) const {
        row_type row;
        row.reserve(cells.size());
        for (const auto& cell : cells) {
            row.push_back(normalize_text(cell_to_string(cell)));
        }
        return row;
    }

    /// 应用默认 Unicode 边框与多字节字符支持。
    void apply_unicode_style() {
        table_.format()
            .multi_byte_characters(true)
            .locale("")
            .trim_mode(format_type::TrimMode::kNone)
            .padding_left(1)
            .padding_right(1)
            .padding_top(0)
            .padding_bottom(0)
            .border_top("─")
            .border_bottom("─")
            .border_left("│")
            .border_right("│")
            .column_separator("│")
            .corner("┼");

        // 逐单元格补齐边框与圆角。
        const auto row_count = rows();
        const auto column_count = columns();
        for (std::size_t r = 0; r < row_count; ++r) {
            for (std::size_t c = 0; c < column_count; ++c) {
                auto& cell = table_.row(r).cell(c);
                auto& fmt = cell.format();
                fmt.multi_byte_characters(true)
                    .locale("")
                    .trim_mode(format_type::TrimMode::kNone)
                    .padding_left(1)
                    .padding_right(1)
                    .padding_top(0)
                    .padding_bottom(0)
                    .border_top("─")
                    .border_bottom("─")
                    .border_left("│")
                    .border_right("│")
                    .column_separator("│");

                if (r == 0) {
                    fmt.corner_top_left(c == 0 ? "┌" : "┬");
                    if (c + 1 == column_count) {
                        fmt.corner_top_right("┐");
                    }
                } else {
                    fmt.corner_top_left(c == 0 ? "├" : "┼");
                    if (c + 1 == column_count) {
                        fmt.corner_top_right("┤");
                    }
                }

                if (r + 1 == row_count) {
                    fmt.corner_bottom_left(c == 0 ? "└" : "┴");
                    if (c + 1 == column_count) {
                        fmt.corner_bottom_right("┘");
                    }
                }
            }
        }
    }

    /// 追加可隐式转单元格类型的值到行。
    template<class T LTOOL_ENABLE_IF(std::is_convertible<T, cell_type>::value)>
        LTOOL_REQUIRES(std::convertible_to<T, cell_type>)
    static void append_cell(row_type& row, const T& value) {
        row.push_back(cell_type(value));
    }

    /// 追加不可隐式转单元格类型的值（用流式输出转字符串）。
    template<class T LTOOL_ENABLE_IF(!std::is_convertible<T, cell_type>::value)>
        LTOOL_REQUIRES(!std::convertible_to<T, cell_type>)
    static void append_cell(row_type& row, const T& value) {
        row.push_back(LTableDetail::stream_to_string(value));
    }

    /// 可变参追加的终止重载。
    static void add_values_to_row(row_type&) {}

    /// 把多个值依次追加到行。
    template<class T, class... Rest>
    static void add_values_to_row(row_type& row, const T& value, const Rest&... rest) {
        append_cell(row, value);
        add_values_to_row(row, rest...);
    }

    table_type table_;                 ///< 底层 tabulate 表格。
    std::size_t max_cell_width_ = 32;  ///< 单元格最大显示宽度。
    std::size_t max_cell_rows_ = 3;    ///< 单元格最大显示行数。
    std::string ellipsis_ = "…";       ///< 折叠省略号。
};

/// 把表格渲染后写入输出流。
inline std::ostream& operator<<(std::ostream& stream, const LTable& table) {
    table.print(stream);
    return stream;
}

#endif // LTOOL_LTABLE_INCLUDE

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

template<class T>
struct remove_cvref {
    using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<class T>
using remove_cvref_t = typename remove_cvref<T>::type;

template<class T>
struct is_text_like
    : std::integral_constant<bool,
                             std::is_convertible<T, const char*>::value ||
                                 std::is_same<remove_cvref_t<T>, std::string>::value> {};

template<class T>
inline std::string stream_to_string(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline bool is_utf8_continuation(unsigned char ch) {
    return (ch & 0xc0U) == 0x80U;
}

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

inline bool is_combining_codepoint(std::uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
           (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff) ||
           (cp >= 0xfe20 && cp <= 0xfe2f);
}

inline bool is_wide_codepoint(std::uint32_t cp) {
    return (cp >= 0x1100 &&
            (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
             (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) ||
             (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff) ||
             (cp >= 0xfe10 && cp <= 0xfe19) || (cp >= 0xfe30 && cp <= 0xfe6f) ||
             (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6) ||
             (cp >= 0x1f300 && cp <= 0x1faff) || (cp >= 0x20000 && cp <= 0x3fffd)));
}

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

inline std::size_t display_width(const std::string& text) {
    std::size_t width = 0;
    std::size_t pos = 0;
    std::string bytes;
    while (pos < text.size()) {
        width += codepoint_width(next_codepoint(text, pos, bytes));
    }
    return width;
}

inline bool is_space_codepoint(std::uint32_t cp) {
    return cp == ' ' || cp == '\t';
}

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
    using table_type = tabulate::Table;
    using row_type = table_type::Row_t;
    using cell_type = row_type::value_type;
    using row_view = tabulate::Row;
    using column_view = tabulate::Column;
    using format_type = tabulate::Format;
    using color_type = tabulate::Color;
    using font_align_type = tabulate::FontAlign;
    using font_style_type = tabulate::FontStyle;

    LTable() {
        apply_unicode_style();
    }

    explicit LTable(const row_type& header_cells) {
        header(header_cells);
    }

    explicit LTable(std::initializer_list<cell_type> header_cells) {
        header(header_cells);
    }

    table_type& native() noexcept {
        return table_;
    }

    const table_type& native() const noexcept {
        return table_;
    }

    operator table_type&() noexcept {
        return table_;
    }

    operator const table_type&() const noexcept {
        return table_;
    }

    LTable& add_row(const row_type& cells) {
        table_.add_row(normalize_row(cells));
        apply_unicode_style();
        return *this;
    }

    LTable& add_row(std::initializer_list<cell_type> cells) {
        return add_row(row_type(cells));
    }

    template<class Cells>
    typename std::enable_if<!LTableDetail::is_text_like<Cells>::value &&
                                !std::is_same<LTableDetail::remove_cvref_t<Cells>, row_type>::value,
                            LTable&>::type
    add_row(const Cells& cells) {
        return add_row(make_row(cells));
    }

    template<class... Values>
    LTable& add_values(const Values&... values) {
        return add_row(make_row_values(values...));
    }

    LTable& header(const row_type& cells) {
        return add_row(cells);
    }

    LTable& header(std::initializer_list<cell_type> cells) {
        return header(row_type(cells));
    }

    template<class Cells>
    typename std::enable_if<!LTableDetail::is_text_like<Cells>::value &&
                                !std::is_same<LTableDetail::remove_cvref_t<Cells>, row_type>::value,
                            LTable&>::type
    header(const Cells& cells) {
        return header(make_row(cells));
    }

    template<class... Values>
    LTable& header_values(const Values&... values) {
        return header(make_row_values(values...));
    }

    template<class Rows>
    LTable& add_rows(const Rows& rows) {
        for (const auto& row : rows) {
            add_row(row);
        }
        return *this;
    }

    row_view& row(std::size_t index) {
        return table_.row(index);
    }

    row_view& operator[](std::size_t index) {
        return row(index);
    }

    column_view column(std::size_t index) {
        return table_.column(index);
    }

    format_type& format() {
        return table_.format();
    }

    LTable& max_cell_width(std::size_t value) {
        max_cell_width_ = value;
        return *this;
    }

    std::size_t max_cell_width() const noexcept {
        return max_cell_width_;
    }

    LTable& max_cell_rows(std::size_t value) {
        max_cell_rows_ = value;
        return *this;
    }

    std::size_t max_cell_rows() const noexcept {
        return max_cell_rows_;
    }

    std::size_t size() const {
        return rows();
    }

    std::size_t rows() const {
        return table_.size();
    }

    std::size_t columns() const {
        if (rows() == 0) {
            return 0;
        }

        auto copy = table_;
        return copy.row(0).size();
    }

    std::pair<std::size_t, std::size_t> shape() const {
        return std::make_pair(rows(), columns());
    }

    std::pair<std::size_t, std::size_t> display_shape() const {
        auto copy = table_;
        return copy.shape();
    }

    void print(std::ostream& stream = std::cout) const {
        auto copy = table_;
        copy.print(stream);
    }

    std::string str() const {
        auto copy = table_;
        return copy.str();
    }

    std::string markdown() const {
        auto copy = table_;
        tabulate::MarkdownExporter exporter;
        return exporter.dump(copy);
    }

    std::string latex() const {
        auto copy = table_;
        tabulate::LatexExporter exporter;
        return exporter.dump(copy);
    }

    std::string asciidoc() const {
        auto copy = table_;
        tabulate::AsciiDocExporter exporter;
        return exporter.dump(copy);
    }

    template<class Cells>
    static row_type make_row(const Cells& cells) {
        row_type row;
        for (const auto& cell : cells) {
            append_cell(row, cell);
        }
        return row;
    }

    template<class... Values>
    static row_type make_row_values(const Values&... values) {
        row_type row;
        add_values_to_row(row, values...);
        return row;
    }

private:
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

    std::string normalize_text(const std::string& text) const {
        return LTableDetail::fold_text(text, max_cell_width_, max_cell_rows_, ellipsis_);
    }

    row_type normalize_row(const row_type& cells) const {
        row_type row;
        row.reserve(cells.size());
        for (const auto& cell : cells) {
            row.push_back(normalize_text(cell_to_string(cell)));
        }
        return row;
    }

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

    template<class T>
    static typename std::enable_if<std::is_convertible<T, cell_type>::value>::type
    append_cell(row_type& row, const T& value) {
        row.push_back(cell_type(value));
    }

    template<class T>
    static typename std::enable_if<!std::is_convertible<T, cell_type>::value>::type
    append_cell(row_type& row, const T& value) {
        row.push_back(LTableDetail::stream_to_string(value));
    }

    static void add_values_to_row(row_type&) {}

    template<class T, class... Rest>
    static void add_values_to_row(row_type& row, const T& value, const Rest&... rest) {
        append_cell(row, value);
        add_values_to_row(row, rest...);
    }

    table_type table_;
    std::size_t max_cell_width_ = 32;
    std::size_t max_cell_rows_ = 3;
    std::string ellipsis_ = "…";
};

inline std::ostream& operator<<(std::ostream& stream, const LTable& table) {
    table.print(stream);
    return stream;
}

#endif // LTOOL_LTABLE_INCLUDE

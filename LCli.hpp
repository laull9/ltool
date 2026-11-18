/**
 * @file LCli.hpp
 * @brief LTool 命令行参数解析器（零依赖、纯头文件）
 *
 * @code
 * #include "LCli.hpp"
 *
 * int main(int argc, char** argv) {
 *     LCli cli("myapp", "示例命令行工具");
 *     cli.version("1.0.0")
 *        .flag("verbose", 'v', "启用详细日志")
 *        .value_option("port", 'p', "PORT", "监听端口", "8080")
 *        .positional("input", "输入文件路径", true);
 *     auto res = cli.parse_or_exit(argc, argv);
 *     int port = res.get_or<int>("port", 8080);
 *     bool verbose = res.flag("verbose");
 * }
 * @endcode
 */
#ifndef LTOOL_LCLI_INCLUDE
#define LTOOL_LCLI_INCLUDE

#include "detail/LToolConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

/// 命令行选项描述
struct LCliOption {
    /// 长选项名（不含 "--" 前缀）
    std::string name;
    /// 短选项字符（不含 '-'，'\0' 表示无短选项）
    char short_name = '\0';
    /// 帮助文档中显示的值占位符（仅非布尔标志有效）
    std::string value_name = "VALUE";
    /// 选项说明文字
    std::string description;
    /// 默认值（空串表示无默认值）
    std::string default_value;
    /// 是否为布尔标志（无需参数值）
    bool is_flag = false;
    /// 是否为必填选项
    bool required = false;
    /// 是否允许重复出现（多次赋值收集到列表）
    bool multiple = false;
};

/// 位置参数描述
struct LCliPositional {
    /// 位置参数名称
    std::string name;
    /// 说明文字
    std::string description;
    /// 是否必填
    bool required = false;
    /// 是否收集剩余所有位置参数
    bool multiple = false;
};

namespace LCliDetail {

inline bool parse_bool_text(std::string_view text, bool& out) noexcept {
    std::string lower;
    lower.reserve(text.size());
    for (unsigned char ch : text) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        out = false;
        return true;
    }
    return false;
}

template <typename T>
inline std::optional<T> convert_cli_value(std::string_view text) {
    using Decayed = std::decay_t<T>;
    if constexpr (std::is_same_v<Decayed, std::string>) {
        return std::string(text);
    } else if constexpr (std::is_same_v<Decayed, bool>) {
        bool v = false;
        if (parse_bool_text(text, v)) {
            return v;
        }
        return std::nullopt;
    } else if constexpr (std::is_integral_v<Decayed> && !std::is_same_v<Decayed, char>) {
        Decayed val{};
        const char* begin = text.data();
        const char* end = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(begin, end, val, 10);
        if (ec == std::errc{} && ptr == end) {
            return val;
        }
        return std::nullopt;
    } else if constexpr (std::is_floating_point_v<Decayed>) {
        std::string s(text);
        char* end_ptr = nullptr;
        errno = 0;
        double d = std::strtod(s.c_str(), &end_ptr);
        if (errno == 0 && end_ptr == s.c_str() + s.size() && !s.empty()) {
            return static_cast<Decayed>(d);
        }
        return std::nullopt;
    } else {
        return std::nullopt;
    }
}

} // namespace LCliDetail

/// 命令行解析结果
class LCliParseResult {
public:
    /// 解析是否成功（包含请求 --help / --version）
    [[nodiscard]] bool ok() const noexcept {
        return error_.empty();
    }

    /// 是否请求了 --help / -h
    [[nodiscard]] bool help_requested() const noexcept {
        return help_requested_;
    }

    /// 是否请求了 --version / -V
    [[nodiscard]] bool version_requested() const noexcept {
        return version_requested_;
    }

    /// 解析失败时的错误信息（成功时为空串）
    [[nodiscard]] const std::string& error() const noexcept {
        return error_;
    }

    /// 检查选项是否存在（显式传入或具有默认值）
    [[nodiscard]] bool has(std::string_view name) const {
        auto it = values_.find(std::string(name));
        return it != values_.end() && !it->second.empty();
    }

    /// 检查布尔标志是否启用
    [[nodiscard]] bool flag(std::string_view name) const {
        auto v = get<bool>(name);
        return v.value_or(false);
    }

    /// 获取选项的最后一个字符串值
    [[nodiscard]] std::optional<std::string> get_string(std::string_view name) const {
        auto it = values_.find(std::string(name));
        if (it == values_.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second.back();
    }

    /// 获取多值选项的全部字符串值
    [[nodiscard]] std::vector<std::string> get_all(std::string_view name) const {
        auto it = values_.find(std::string(name));
        if (it == values_.end()) {
            return {};
        }
        return it->second;
    }

    /// 按类型读取选项值
    template <typename T>
    [[nodiscard]] std::optional<T> get(std::string_view name) const {
        auto s = get_string(name);
        if (!s.has_value()) {
            return std::nullopt;
        }
        return LCliDetail::convert_cli_value<T>(*s);
    }

    /// 按类型读取选项值，不存在或转换失败时返回默认值
    template <typename T>
    [[nodiscard]] T get_or(std::string_view name, T fallback) const {
        auto v = get<T>(name);
        return v.has_value() ? std::move(*v) : std::move(fallback);
    }

    /// 获取全部位置参数
    [[nodiscard]] const std::vector<std::string>& positionals() const noexcept {
        return positionals_;
    }

    /// 获取指定索引的位置参数
    [[nodiscard]] std::optional<std::string> positional(std::size_t index) const {
        if (index >= positionals_.size()) {
            return std::nullopt;
        }
        return positionals_[index];
    }

    /// 获取 "--" 之后透传的原始参数
    [[nodiscard]] const std::vector<std::string>& passthrough() const noexcept {
        return passthrough_;
    }

    /// 获取匹配的直属子命令名称（无子命令时为空）
    [[nodiscard]] const std::string& subcommand_name() const noexcept {
        return subcommand_name_;
    }

    /// 获取子命令的解析结果（无子命令时为 nullptr）
    [[nodiscard]] const LCliParseResult* subcommand_result() const noexcept {
        return subcommand_result_.get();
    }

private:
    friend class LCli;

    std::unordered_map<std::string, std::vector<std::string>> values_;
    std::vector<std::string> positionals_;
    std::vector<std::string> passthrough_;
    std::string subcommand_name_;
    std::shared_ptr<LCliParseResult> subcommand_result_;
    std::string error_;
    bool help_requested_ = false;
    bool version_requested_ = false;
};

/// 命令行参数解析器（支持长短选项、短标志组合、子命令、位置参数、自动帮助与版本输出）
class LCli {
public:
    /// 构造解析器
    explicit LCli(std::string program_name, std::string description = {})
        : program_name_(std::move(program_name))
        , description_(std::move(description)) {}

    /// 设置版本号（自动启用 --version / -V）
    LCli& version(std::string ver) {
        version_ = std::move(ver);
        return *this;
    }

    /// 设置帮助信息末尾附注
    LCli& footer(std::string text) {
        footer_ = std::move(text);
        return *this;
    }

    /// 是否禁用自动 --help / -h（默认启用）
    LCli& auto_help(bool enabled) noexcept {
        auto_help_ = enabled;
        return *this;
    }

    /// 添加自定义选项
    LCli& option(LCliOption opt) {
        if (opt.name.empty()) {
            throw std::invalid_argument("LCli option name cannot be empty");
        }
        if (find_option_index(opt.name) != kNpos) {
            throw std::invalid_argument("Duplicate LCli option name: " + opt.name);
        }
        if (opt.short_name != '\0' && find_short_index(opt.short_name) != kNpos) {
            throw std::invalid_argument(std::string("Duplicate LCli short option: -") + opt.short_name);
        }
        options_.push_back(std::move(opt));
        return *this;
    }

    /// 添加布尔标志选项
    LCli& flag(std::string name, char short_name = '\0', std::string description = {}) {
        LCliOption opt;
        opt.name = std::move(name);
        opt.short_name = short_name;
        opt.description = std::move(description);
        opt.is_flag = true;
        return option(std::move(opt));
    }

    /// 添加单值选项
    LCli& value_option(
        std::string name,
        char short_name = '\0',
        std::string value_name = "VALUE",
        std::string description = {},
        std::string default_value = {},
        bool required = false
    ) {
        LCliOption opt;
        opt.name = std::move(name);
        opt.short_name = short_name;
        opt.value_name = std::move(value_name);
        opt.description = std::move(description);
        opt.default_value = std::move(default_value);
        opt.is_flag = false;
        opt.required = required;
        return option(std::move(opt));
    }

    /// 添加多值选项（可重复指定）
    LCli& multi_option(
        std::string name,
        char short_name = '\0',
        std::string value_name = "VALUE",
        std::string description = {}
    ) {
        LCliOption opt;
        opt.name = std::move(name);
        opt.short_name = short_name;
        opt.value_name = std::move(value_name);
        opt.description = std::move(description);
        opt.is_flag = false;
        opt.multiple = true;
        return option(std::move(opt));
    }

    /// 添加位置参数定义
    LCli& positional(
        std::string name,
        std::string description = {},
        bool required = false,
        bool multiple = false
    ) {
        if (name.empty()) {
            throw std::invalid_argument("LCli positional name cannot be empty");
        }
        LCliPositional pos;
        pos.name = std::move(name);
        pos.description = std::move(description);
        pos.required = required;
        pos.multiple = multiple;
        positionals_.push_back(std::move(pos));
        return *this;
    }

    /// 注册并返回一个直属子命令解析器
    LCli& subcommand(std::string name, std::string description = {}) {
        if (name.empty()) {
            throw std::invalid_argument("LCli subcommand name cannot be empty");
        }
        for (const auto& sub : subcommands_) {
            if (sub->program_name_ == name) {
                throw std::invalid_argument("Duplicate LCli subcommand: " + name);
            }
        }
        auto child = std::make_shared<LCli>(program_name_ + " " + name, std::move(description));
        child->short_command_name_ = std::move(name);
        subcommands_.push_back(child);
        return *child;
    }

    /// 解析 main(argc, argv) 参数（自动跳过 argv[0]）
    [[nodiscard]] LCliParseResult parse(int argc, const char* const* argv) const {
        std::vector<std::string> args;
        if (argc > 1 && argv != nullptr) {
            args.reserve(static_cast<std::size_t>(argc - 1));
            for (int i = 1; i < argc; ++i) {
                if (argv[i] != nullptr) {
                    args.emplace_back(argv[i]);
                }
            }
        }
        return parse(args);
    }

    /// 解析已去除程序名的参数列表
    [[nodiscard]] LCliParseResult parse(const std::vector<std::string>& args) const {
        LCliParseResult res;
        apply_defaults(res);

        std::size_t i = 0;
        bool seen_double_dash = false;
        while (i < args.size()) {
            const std::string& tok = args[i];
            if (seen_double_dash) {
                res.passthrough_.push_back(tok);
                ++i;
                continue;
            }
            if (tok == "--") {
                seen_double_dash = true;
                ++i;
                continue;
            }

            // 长选项：--name 或 --name=value
            if (tok.size() > 2 && tok[0] == '-' && tok[1] == '-') {
                std::string_view body(tok.data() + 2, tok.size() - 2);
                auto eq = body.find('=');
                std::string_view name = (eq == std::string_view::npos) ? body : body.substr(0, eq);
                bool has_inline_val = (eq != std::string_view::npos);
                std::string_view inline_val = has_inline_val ? body.substr(eq + 1) : std::string_view{};

                if (auto_help_ && name == "help" && find_option_index("help") == kNpos) {
                    res.help_requested_ = true;
                    return res;
                }
                if (!version_.empty() && name == "version" && find_option_index("version") == kNpos) {
                    res.version_requested_ = true;
                    return res;
                }

                std::size_t idx = find_option_index(name);
                if (idx == kNpos) {
                    res.error_ = "Unknown option: --" + std::string(name);
                    return res;
                }

                const auto& opt = options_[idx];
                if (opt.is_flag) {
                    if (has_inline_val) {
                        bool b = false;
                        if (!LCliDetail::parse_bool_text(inline_val, b)) {
                            res.error_ = "Invalid boolean value for --" + opt.name + ": " + std::string(inline_val);
                            return res;
                        }
                        assign_value(res, opt, b ? "true" : "false");
                    } else {
                        assign_value(res, opt, "true");
                    }
                    ++i;
                } else {
                    std::string val;
                    if (has_inline_val) {
                        val.assign(inline_val);
                        ++i;
                    } else if (i + 1 < args.size()) {
                        val = args[i + 1];
                        i += 2;
                    } else {
                        res.error_ = "Option --" + opt.name + " requires a value";
                        return res;
                    }
                    assign_value(res, opt, std::move(val));
                }
                continue;
            }

            // 短选项：-x、-abc 或 -p8080 / -p 8080
            if (tok.size() >= 2 && tok[0] == '-' && tok[1] != '-') {
                if (tok.size() == 2) {
                    char ch = tok[1];
                    if (auto_help_ && ch == 'h' && find_short_index('h') == kNpos) {
                        res.help_requested_ = true;
                        return res;
                    }
                    if (!version_.empty() && ch == 'V' && find_short_index('V') == kNpos) {
                        res.version_requested_ = true;
                        return res;
                    }
                    std::size_t idx = find_short_index(ch);
                    if (idx == kNpos) {
                        res.error_ = std::string("Unknown option: -") + ch;
                        return res;
                    }
                    const auto& opt = options_[idx];
                    if (opt.is_flag) {
                        assign_value(res, opt, "true");
                        ++i;
                    } else if (i + 1 < args.size()) {
                        assign_value(res, opt, args[i + 1]);
                        i += 2;
                    } else {
                        res.error_ = std::string("Option -") + ch + " requires a value";
                        return res;
                    }
                } else {
                    // 短选项组合：先检查首字符是否为带值选项（支持 -p8080），否则逐字符展开布尔标志
                    std::size_t first_idx = find_short_index(tok[1]);
                    if (first_idx != kNpos && !options_[first_idx].is_flag) {
                        assign_value(res, options_[first_idx], tok.substr(2));
                        ++i;
                    } else {
                        for (std::size_t k = 1; k < tok.size(); ++k) {
                            char ch = tok[k];
                            if (auto_help_ && ch == 'h' && find_short_index('h') == kNpos) {
                                res.help_requested_ = true;
                                return res;
                            }
                            if (!version_.empty() && ch == 'V' && find_short_index('V') == kNpos) {
                                res.version_requested_ = true;
                                return res;
                            }
                            std::size_t idx = find_short_index(ch);
                            if (idx == kNpos) {
                                res.error_ = std::string("Unknown option: -") + ch;
                                return res;
                            }
                            if (!options_[idx].is_flag) {
                                if (k + 1 < tok.size()) {
                                    assign_value(res, options_[idx], tok.substr(k + 1));
                                    break;
                                }
                                if (i + 1 < args.size()) {
                                    assign_value(res, options_[idx], args[i + 1]);
                                    ++i;
                                    break;
                                }
                                res.error_ = std::string("Option -") + ch + " requires a value";
                                return res;
                            }
                            assign_value(res, options_[idx], "true");
                        }
                        ++i;
                    }
                }
                continue;
            }

            // 匹配直属子命令（在尚无位置参数时匹配第一个非选项词）
            if (!subcommands_.empty() && res.positionals_.empty()) {
                if (const LCli* sub = find_subcommand(tok)) {
                    std::vector<std::string> sub_args(args.begin() + static_cast<std::ptrdiff_t>(i + 1), args.end());
                    auto sub_res = std::make_shared<LCliParseResult>(sub->parse(sub_args));
                    res.subcommand_name_ = tok;
                    res.subcommand_result_ = sub_res;
                    if (!sub_res->ok()) {
                        res.error_ = sub_res->error();
                    }
                    if (sub_res->help_requested()) {
                        res.help_requested_ = true;
                    }
                    if (sub_res->version_requested()) {
                        res.version_requested_ = true;
                    }
                    if (!sub_res->ok() || res.help_requested_ || res.version_requested_) {
                        return res;
                    }
                    break;
                }
            }

            // 位置参数
            res.positionals_.push_back(tok);
            ++i;
        }

        // 检查必填选项
        for (const auto& opt : options_) {
            if (opt.required && !res.has(opt.name)) {
                res.error_ = "Missing required option: --" + opt.name;
                return res;
            }
        }

        // 检查必填位置参数（仅当未选择子命令时检查当前层位置参数）
        if (res.subcommand_name_.empty()) {
            std::size_t required_pos = 0;
            for (const auto& pos : positionals_) {
                if (pos.required) {
                    ++required_pos;
                }
            }
            if (res.positionals_.size() < required_pos) {
                res.error_ = "Missing required positional argument: " + positionals_[res.positionals_.size()].name;
                return res;
            }
        }

        return res;
    }

    /// 解析参数；若请求帮助/版本或解析失败则自动打印信息并调用 std::exit
    [[nodiscard]] LCliParseResult parse_or_exit(
        int argc,
        const char* const* argv,
        std::ostream& out = std::cout,
        std::ostream& err = std::cerr
    ) const {
        auto res = parse(argc, argv);
        if (res.help_requested()) {
            if (!res.subcommand_name_.empty()) {
                if (const LCli* sub = find_subcommand(res.subcommand_name_)) {
                    out << sub->help_text();
                    std::exit(0);
                }
            }
            out << help_text();
            std::exit(0);
        }
        if (res.version_requested()) {
            out << program_name_ << " " << version_ << '\n';
            std::exit(0);
        }
        if (!res.ok()) {
            err << "Error: " << res.error() << "\n\n" << help_text();
            std::exit(1);
        }
        return res;
    }

    /// 生成格式化的帮助文档
    [[nodiscard]] std::string help_text() const {
        std::ostringstream oss;
        oss << "Usage: " << program_name_;
        if (!options_.empty() || auto_help_ || !version_.empty()) {
            oss << " [OPTIONS]";
        }
        if (!subcommands_.empty()) {
            oss << " <COMMAND>";
        }
        for (const auto& pos : positionals_) {
            oss << ' ';
            oss << (pos.required ? '<' : '[') << pos.name;
            if (pos.multiple) {
                oss << "...";
            }
            oss << (pos.required ? '>' : ']');
        }
        oss << '\n';

        if (!description_.empty()) {
            oss << '\n' << description_ << '\n';
        }

        if (!subcommands_.empty()) {
            oss << "\nCommands:\n";
            std::size_t max_len = 0;
            for (const auto& sub : subcommands_) {
                max_len = (std::max)(max_len, sub->short_command_name_.size());
            }
            for (const auto& sub : subcommands_) {
                oss << "  " << sub->short_command_name_;
                if (!sub->description_.empty()) {
                    oss << std::string(max_len + 4 - sub->short_command_name_.size(), ' ') << sub->description_;
                }
                oss << '\n';
            }
        }

        if (!positionals_.empty()) {
            oss << "\nArguments:\n";
            std::size_t max_len = 0;
            for (const auto& pos : positionals_) {
                max_len = (std::max)(max_len, pos.name.size());
            }
            for (const auto& pos : positionals_) {
                oss << "  " << pos.name;
                if (!pos.description.empty()) {
                    oss << std::string(max_len + 4 - pos.name.size(), ' ') << pos.description;
                }
                if (pos.required) {
                    oss << " (required)";
                }
                oss << '\n';
            }
        }

        std::vector<std::pair<std::string, std::string>> rows;
        for (const auto& opt : options_) {
            std::string lhs;
            if (opt.short_name != '\0') {
                lhs.push_back('-');
                lhs.push_back(opt.short_name);
                lhs += ", ";
            } else {
                lhs += "    ";
            }
            lhs += "--" + opt.name;
            if (!opt.is_flag) {
                lhs += " <" + opt.value_name + ">";
            }
            std::string rhs = opt.description;
            if (!opt.default_value.empty()) {
                rhs += (rhs.empty() ? "" : " ") + std::string("[default: ") + opt.default_value + "]";
            }
            if (opt.required) {
                rhs += (rhs.empty() ? "" : " ") + std::string("(required)");
            }
            rows.emplace_back(std::move(lhs), std::move(rhs));
        }
        if (auto_help_ && find_option_index("help") == kNpos) {
            std::string lhs = (find_short_index('h') == kNpos) ? "-h, --help" : "    --help";
            rows.emplace_back(std::move(lhs), "Show this help message");
        }
        if (!version_.empty() && find_option_index("version") == kNpos) {
            std::string lhs = (find_short_index('V') == kNpos) ? "-V, --version" : "    --version";
            rows.emplace_back(std::move(lhs), "Show version information");
        }

        if (!rows.empty()) {
            oss << "\nOptions:\n";
            std::size_t max_len = 0;
            for (const auto& r : rows) {
                max_len = (std::max)(max_len, r.first.size());
            }
            for (const auto& r : rows) {
                oss << "  " << r.first;
                if (!r.second.empty()) {
                    oss << std::string(max_len + 4 - r.first.size(), ' ') << r.second;
                }
                oss << '\n';
            }
        }

        if (!footer_.empty()) {
            oss << '\n' << footer_ << '\n';
        }
        return oss.str();
    }

private:
    static constexpr std::size_t kNpos = static_cast<std::size_t>(-1);

    [[nodiscard]] std::size_t find_option_index(std::string_view name) const {
        for (std::size_t i = 0; i < options_.size(); ++i) {
            if (options_[i].name == name) {
                return i;
            }
        }
        return kNpos;
    }

    [[nodiscard]] std::size_t find_short_index(char ch) const {
        for (std::size_t i = 0; i < options_.size(); ++i) {
            if (options_[i].short_name == ch) {
                return i;
            }
        }
        return kNpos;
    }

    [[nodiscard]] const LCli* find_subcommand(std::string_view name) const {
        for (const auto& sub : subcommands_) {
            if (sub->short_command_name_ == name) {
                return sub.get();
            }
        }
        return nullptr;
    }

    void apply_defaults(LCliParseResult& res) const {
        for (const auto& opt : options_) {
            if (!opt.default_value.empty()) {
                res.values_[opt.name].push_back(opt.default_value);
            }
        }
    }

    static void assign_value(LCliParseResult& res, const LCliOption& opt, std::string val) {
        if (opt.multiple) {
            res.values_[opt.name].push_back(std::move(val));
        } else {
            res.values_[opt.name] = {std::move(val)};
        }
    }

    std::string program_name_;
    std::string short_command_name_;
    std::string description_;
    std::string version_;
    std::string footer_;
    bool auto_help_ = true;
    std::vector<LCliOption> options_;
    std::vector<LCliPositional> positionals_;
    std::vector<std::shared_ptr<LCli>> subcommands_;
};

namespace LTool {

/// 命令行解析器类型别名
using Cli = ::LCli;

/// 命令行选项描述类型别名
using CliOption = ::LCliOption;

/// 位置参数描述类型别名
using CliPositional = ::LCliPositional;

/// 命令行解析结果类型别名
using CliParseResult = ::LCliParseResult;

} // namespace LTool

#endif // LTOOL_LCLI_INCLUDE

/**
 * @file LEnv.hpp
 * @brief 轻量 dotenv 配置工具，纯头文件、零第三方依赖。
 *
 * LEnv 的核心定位：
 * - 解析 .env 风格文本到内存 map，默认不污染当前进程环境。
 * - 需要时用 apply() 或 load_dotenv() 写入进程环境变量。
 * - 支持 export KEY=VALUE、单引号、双引号转义、行尾注释和 ${VAR}/$VAR 展开。
 * - 只依赖标准库，适合在小工具、测试和命令行程序里直接 include 使用。
 *
 * 常用示例：
 * @code
 * auto env = LEnv::load(".env");
 * auto url = env.get("DATABASE_URL", "sqlite://local.db");
 *
 * LEnv::load_dotenv(".env"); // 读取并写入当前进程环境，不覆盖已有变量
 * auto mode = LEnv::get_env("APP_ENV", "dev");
 * @endcode
 */

#ifndef LTOOL_LENV_INCLUDE
#define LTOOL_LENV_INCLUDE

#include "detail/LToolConfig.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#if LTOOL_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/**
 * @brief LEnv 加载配置时使用的选项。
 */
struct LEnvOptions {
    /// apply()/load_dotenv() 写入进程环境时是否覆盖已有变量。
    bool overwrite = false;
    /// 解析值时是否展开 ${KEY} 和 $KEY。
    bool expand = true;
};

namespace LEnvDetail {

/// 读取进程环境变量到 out；不存在返回 false。
inline bool read_process_env(const char* key, std::string& out) {
#if LTOOL_PLATFORM_WINDOWS
    ::SetLastError(ERROR_SUCCESS);
    const DWORD size = ::GetEnvironmentVariableA(key, nullptr, 0);
    if (size == 0 && ::GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        return false;
    }
    if (size == 0) {
        out.clear();
        return true;
    }
    std::string value(size, '\0');
    const DWORD copied = ::GetEnvironmentVariableA(key, &value[0], size);
    if (copied >= size) {
        return false;
    }
    out.assign(value.data(), copied);
    return true;
#else
    const char* value = std::getenv(key);
    if (!value) {
        return false;
    }
    out.assign(value);
    return true;
#endif
}

/// 判断字符是否为空白。
inline bool is_space(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

/// 判断字符可否作为变量名首字符。
inline bool is_name_first(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

/// 判断字符可否作为变量名后续字符。
inline bool is_name_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '.' ||
           ch == '-';
}

/// 移除字符串开头的空白并返回副本。
inline std::string ltrim(std::string text) {
    std::size_t pos = 0;
    while (pos < text.size() && is_space(text[pos])) {
        ++pos;
    }
    text.erase(0, pos);
    return text;
}

/// 移除字符串结尾的空白并返回副本。
inline std::string rtrim(std::string text) {
    while (!text.empty() && is_space(text.back())) {
        text.pop_back();
    }
    return text;
}

/// 移除首尾空白并返回副本。
inline std::string trim(std::string text) {
    return rtrim(ltrim(std::move(text)));
}

/// 判断 text 是否以 prefix 开头。
inline bool starts_with(const std::string& text, const char* prefix) {
    std::size_t i = 0;
    while (prefix[i] != '\0') {
        if (i >= text.size() || text[i] != prefix[i]) {
            return false;
        }
        ++i;
    }
    return true;
}

/// 构造带来源和行号的解析错误异常。
inline std::runtime_error parse_error(const std::string& source, std::size_t line,
                                      const std::string& message) {
    std::ostringstream out;
    out << "LEnv parse error";
    if (!source.empty()) {
        out << " in " << source;
    }
    out << " at line " << line << ": " << message;
    return std::runtime_error(out.str());
}

/// 校验 key 是否符合 .env 变量命名规则。
inline bool valid_key(const std::string& key) {
    if (key.empty() || !is_name_first(key[0])) {
        return false;
    }
    for (std::size_t i = 1; i < key.size(); ++i) {
        if (!is_name_char(key[i])) {
            return false;
        }
    }
    return true;
}

/// 判断字符是否为 ${VAR} 展开的合法名称字符。
inline bool is_expand_name_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

/// 按 key 查找值：先查内存 map，再退回进程环境变量。
inline std::string lookup_value(const std::map<std::string, std::string>& values,
                                const std::string& key) {
    auto it = values.find(key);
    if (it != values.end()) {
        return it->second;
    }
    std::string env;
    return read_process_env(key.c_str(), env) ? env : std::string();
}

/// 展开 value 中的 ${KEY} 与 $KEY 变量引用。
inline std::string expand_vars(const std::string& value,
                               const std::map<std::string, std::string>& values) {
    std::string out;
    out.reserve(value.size());

    for (std::size_t i = 0; i < value.size();) {
        if (value[i] != '$') {
            out.push_back(value[i++]);
            continue;
        }

        if (i + 1 < value.size() && value[i + 1] == '{') {
            const std::size_t end = value.find('}', i + 2);
            if (end == std::string::npos) {
                out.push_back(value[i++]);
                continue;
            }
            out += lookup_value(values, value.substr(i + 2, end - i - 2));
            i = end + 1;
            continue;
        }

        std::size_t begin = i + 1;
        if (begin >= value.size() || !is_name_first(value[begin])) {
            out.push_back(value[i++]);
            continue;
        }
        std::size_t end = begin + 1;
        while (end < value.size() && is_expand_name_char(value[end])) {
            ++end;
        }
        out += lookup_value(values, value.substr(begin, end - begin));
        i = end;
    }

    return out;
}

/// 反转义双引号字符串中的 \n \r \t 等转义序列。
inline std::string unescape_double_quoted(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out.push_back(value[i]);
            continue;
        }

        const char next = value[++i];
        switch (next) {
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case '"':
        case '\'':
        case '\\':
        case '$':
            out.push_back(next);
            break;
        default:
            out.push_back('\\');
            out.push_back(next);
            break;
        }
    }
    return out;
}

/// 解析未加引号的值：遇到行尾注释（#）截断，并移除尾部空白。
inline std::string parse_unquoted_value(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '#' && (i == 0 || is_space(text[i - 1]))) {
            break;
        }
        out.push_back(text[i]);
    }

    return rtrim(std::move(out));
}

/// 解析单引号或双引号包裹的值；引号后有多余内容或缺少闭合引号会抛异常。
inline std::string parse_quoted_value(const std::string& text, std::size_t line,
                                      const std::string& source) {
    const char quote = text[0];
    std::string inner;
    bool escaped = false;

    for (std::size_t i = 1; i < text.size(); ++i) {
        const char ch = text[i];
        if (quote == '"' && ch == '\\' && !escaped) {
            escaped = true;
            inner.push_back(ch);
            continue;
        }
        if (ch == quote && !escaped) {
            auto tail = trim(text.substr(i + 1));
            if (!tail.empty() && tail[0] != '#') {
                throw parse_error(source, line, "unexpected text after quoted value");
            }
            return quote == '"' ? unescape_double_quoted(inner) : inner;
        }
        escaped = false;
        inner.push_back(ch);
    }

    throw parse_error(source, line, "missing closing quote");
}

} // namespace LEnvDetail

/**
 * @brief .env 配置集合和进程环境变量辅助工具。
 */
class LEnv {
public:
    using map_type = std::map<std::string, std::string>;
    using const_iterator = map_type::const_iterator;

    LEnv() = default;

    explicit LEnv(map_type values)
        : values_(std::move(values)) {}

    /**
     * @brief 从文件读取 .env 配置，返回内存中的配置集合。
     */
    static LEnv load(const std::string& path = ".env", LEnvOptions options = LEnvOptions()) {
        LEnv env;
        env.load_file(path, options);
        return env;
    }

    /**
     * @brief 从文件读取 .env 配置并写入当前进程环境。
     */
    static LEnv load_dotenv(const std::string& path = ".env",
                            LEnvOptions options = LEnvOptions()) {
        LEnv env = load(path, options);
        env.apply(options.overwrite);
        return env;
    }

    /**
     * @brief 解析一段 .env 文本并合并到当前对象。
     */
    LEnv& parse(const std::string& text, LEnvOptions options = LEnvOptions(),
                const std::string& source = std::string()) {
        std::istringstream input(text);
        std::string line;
        std::size_t line_no = 0;

        while (std::getline(input, line)) {
            ++line_no;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line_no == 1 && line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
            parse_line(line, line_no, options, source);
        }

        return *this;
    }

    /**
     * @brief 从文件读取 .env 配置并合并到当前对象。
     */
    LEnv& load_file(const std::string& path, LEnvOptions options = LEnvOptions()) {
        std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
        if (!file) {
            throw std::runtime_error("cannot open env file: " + path);
        }
        std::ostringstream content;
        content << file.rdbuf();
        return parse(content.str(), options, path);
    }

    /**
     * @brief 把当前配置写入进程环境。
     */
    void apply(bool overwrite = false) const {
        for (const auto& item : values_) {
            set_env(item.first, item.second, overwrite);
        }
    }

    /**
     * @brief 设置内存中的配置项。
     */
    LEnv& set(std::string key, std::string value) {
        if (!LEnvDetail::valid_key(key)) {
            throw std::invalid_argument("invalid env key: " + key);
        }
        values_[std::move(key)] = std::move(value);
        return *this;
    }

    /**
     * @brief 删除内存中的配置项。
     */
    bool erase(const std::string& key) {
        return values_.erase(key) != 0;
    }

    /**
     * @brief 判断内存配置中是否存在指定 key。
     */
    bool contains(const std::string& key) const {
        return values_.find(key) != values_.end();
    }

    /**
     * @brief 获取内存配置值；不存在时返回 fallback。
     */
    std::string get(const std::string& key, std::string fallback = std::string()) const {
        auto it = values_.find(key);
        return it == values_.end() ? std::move(fallback) : it->second;
    }

    /**
     * @brief 获取内存配置值；存在时写入 out 并返回 true。
     */
    bool get(const std::string& key, std::string& out) const {
        auto it = values_.find(key);
        if (it == values_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    const map_type& values() const noexcept {
        return values_;
    }

    std::size_t size() const noexcept {
        return values_.size();
    }

    bool empty() const noexcept {
        return values_.empty();
    }

    const_iterator begin() const noexcept {
        return values_.begin();
    }

    const_iterator end() const noexcept {
        return values_.end();
    }

    /**
     * @brief 获取当前进程环境变量；不存在时返回 fallback。
     */
    static std::string get_env(const std::string& key,
                               std::string fallback = std::string()) {
        std::string value;
        return LEnvDetail::read_process_env(key.c_str(), value)
                   ? value
                   : std::move(fallback);
    }

    /**
     * @brief 获取当前进程环境变量；存在时写入 out 并返回 true。
     */
    static bool try_get_env(const std::string& key, std::string& out) {
        return LEnvDetail::read_process_env(key.c_str(), out);
    }

    /**
     * @brief 设置当前进程环境变量。
     */
    static bool set_env(const std::string& key, const std::string& value,
                        bool overwrite = true) {
        if (!overwrite) {
            std::string existing;
            if (LEnvDetail::read_process_env(key.c_str(), existing)) {
                return true;
            }
        }
#if LTOOL_PLATFORM_WINDOWS
        return ::SetEnvironmentVariableA(key.c_str(), value.c_str()) != 0;
#else
        return ::setenv(key.c_str(), value.c_str(), overwrite ? 1 : 0) == 0;
#endif
    }

    /**
     * @brief 删除当前进程环境变量。
     */
    static bool unset_env(const std::string& key) {
#if LTOOL_PLATFORM_WINDOWS
        return ::SetEnvironmentVariableA(key.c_str(), nullptr) != 0;
#else
        return ::unsetenv(key.c_str()) == 0;
#endif
    }

private:
    /// 解析单行 .env 文本并合并到 values_。
    void parse_line(std::string line, std::size_t line_no, LEnvOptions options,
                    const std::string& source) {
        line = LEnvDetail::ltrim(std::move(line));
        if (line.empty() || line[0] == '#') {
            return;
        }

        // 去掉可选的 export 前缀。
        if (LEnvDetail::starts_with(line, "export") &&
            (line.size() == 6 || LEnvDetail::is_space(line[6]))) {
            line = LEnvDetail::ltrim(line.substr(6));
        }

        // 定位 '=' 分隔符。
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            throw LEnvDetail::parse_error(source, line_no, "missing '='");
        }

        // 校验变量名。
        std::string key = LEnvDetail::trim(line.substr(0, eq));
        if (!LEnvDetail::valid_key(key)) {
            throw LEnvDetail::parse_error(source, line_no, "invalid key: " + key);
        }

        // 按引号/非引号两种方式解析值。
        std::string raw = LEnvDetail::ltrim(line.substr(eq + 1));
        std::string value;
        if (raw.empty()) {
            value.clear();
        } else if (raw[0] == '\'' || raw[0] == '"') {
            value = LEnvDetail::parse_quoted_value(raw, line_no, source);
        } else {
            value = LEnvDetail::parse_unquoted_value(raw);
        }

        // 按选项展开 ${VAR}/$VAR 引用。
        if (options.expand) {
            value = LEnvDetail::expand_vars(value, values_);
        }
        values_[std::move(key)] = std::move(value);
    }

    map_type values_; ///< 内存中的 .env 配置集合。
};

namespace LTool {
using Env = ::LEnv;
} // namespace LTool

#endif // LTOOL_LENV_INCLUDE

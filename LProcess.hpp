/**
 * @file LProcess.hpp
 * @brief LTool 跨平台子进程执行与系统进程信息工具（纯头文件）
 *
 * @code
 * #include "LProcess.hpp"
 *
 * auto res = LProcess::run("echo", {"hello", "world"});
 * if (res.ok()) {
 *     // res.stdout_text == "hello world\n"
 * }
 * @endcode
 */
#ifndef LTOOL_LPROCESS_INCLUDE
#define LTOOL_LPROCESS_INCLUDE

#include "detail/LToolConfig.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if LTOOL_PLATFORM_WINDOWS
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#    include <limits.h>
#    include <poll.h>
#    include <signal.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#    if LTOOL_PLATFORM_APPLE
#        include <mach-o/dyld.h>
#    endif
extern "C" char** environ;
#endif

/// 子进程运行选项
struct LProcessOptions {
    /// 子进程工作目录（为空则继承当前目录）
    std::string working_directory;
    /// 额外或覆盖的环境变量
    std::unordered_map<std::string, std::string> env;
    /// 是否继承父进程环境变量
    bool inherit_env = true;
    /// 写入子进程标准输入的数据
    std::string stdin_data;
    /// 超时时间（超时后会强行终止子进程，std::nullopt 表示不限时）
    std::optional<std::chrono::milliseconds> timeout;
    /// 是否将标准错误重定向并合并到标准输出
    bool merge_stderr = false;
};

/// 子进程运行结果
struct LProcessResult {
    /// 进程退出码（未启动或异常终止时为 -1 或信号编号负值/128+sig）
    int exit_code = -1;
    /// 捕获到的标准输出内容
    std::string stdout_text;
    /// 捕获到的标准错误内容
    std::string stderr_text;
    /// 是否因超时被终止
    bool timed_out = false;
    /// 是否成功启动进程
    bool started = false;
    /// 启动或等待过程中的系统错误信息
    std::string error;

    /// 判断子进程是否正常启动、未超时且退出码为 0
    [[nodiscard]] bool ok() const noexcept {
        return started && !timed_out && exit_code == 0;
    }
};

namespace LProcessDetail {

#if LTOOL_PLATFORM_WINDOWS

inline std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

inline std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}

inline std::wstring quote_windows_arg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}

inline LProcessResult run_windows(
    const std::string& program,
    const std::vector<std::string>& args,
    const LProcessOptions& opts
) {
    LProcessResult result;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;

    auto close_handles = [&]() {
        if (stdin_read) CloseHandle(stdin_read);
        if (stdin_write) CloseHandle(stdin_write);
        if (stdout_read) CloseHandle(stdout_read);
        if (stdout_write) CloseHandle(stdout_write);
        if (stderr_read) CloseHandle(stderr_read);
        if (stderr_write) CloseHandle(stderr_write);
    };

    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0) ||
        !SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
        !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        close_handles();
        result.error = "CreatePipe failed";
        return result;
    }

    // 匿名管道也是字节模式的命名管道。只将父进程的写端设为非阻塞。
    DWORD pipe_mode = PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(stdin_write, &pipe_mode, nullptr, nullptr)) {
        result.error = "cannot configure stdin pipe: " + std::to_string(GetLastError());
        close_handles();
        return result;
    }

    if (!opts.merge_stderr) {
        if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0) ||
            !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
            close_handles();
            result.error = "CreatePipe for stderr failed";
            return result;
        }
    }

    std::wstring cmdline = quote_windows_arg(utf8_to_wide(program));
    for (const auto& arg : args) {
        cmdline.push_back(L' ');
        cmdline += quote_windows_arg(utf8_to_wide(arg));
    }

    std::wstring cwd_wide = opts.working_directory.empty() ? std::wstring{} : utf8_to_wide(opts.working_directory);

    // 构建环境变量块
    std::wstring env_block;
    LPCWSTR env_ptr = nullptr;
    if (!opts.inherit_env || !opts.env.empty()) {
        std::unordered_map<std::wstring, std::wstring> merged;
        if (opts.inherit_env) {
            if (LPWCH raw_env = GetEnvironmentStringsW()) {
                for (LPCWSTR p = raw_env; *p != L'\0';) {
                    std::wstring entry(p);
                    p += entry.size() + 1;
                    auto eq = entry.find(L'=');
                    if (eq != std::wstring::npos && eq > 0) {
                        merged[entry.substr(0, eq)] = entry.substr(eq + 1);
                    }
                }
                FreeEnvironmentStringsW(raw_env);
            }
        }
        for (const auto& kv : opts.env) {
            merged[utf8_to_wide(kv.first)] = utf8_to_wide(kv.second);
        }
        for (const auto& kv : merged) {
            env_block += kv.first;
            env_block.push_back(L'=');
            env_block += kv.second;
            env_block.push_back(L'\0');
        }
        env_block.push_back(L'\0');
        env_ptr = env_block.data();
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = opts.merge_stderr ? stdout_write : stderr_write;

    PROCESS_INFORMATION pi{};
    BOOL created = CreateProcessW(
        nullptr,
        cmdline.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        const_cast<LPWSTR>(env_ptr),
        cwd_wide.empty() ? nullptr : cwd_wide.c_str(),
        &si,
        &pi
    );

    CloseHandle(stdin_read);
    stdin_read = nullptr;
    CloseHandle(stdout_write);
    stdout_write = nullptr;
    if (stderr_write) {
        CloseHandle(stderr_write);
        stderr_write = nullptr;
    }

    if (!created) {
        close_handles();
        result.error = "CreateProcessW failed with error code " + std::to_string(GetLastError());
        return result;
    }

    result.started = true;

    const auto start = std::chrono::steady_clock::now();
    std::size_t offset = 0;
    // 仅读取本次快照中的字节，让大量连续输出也有机会检查截止时间。
    auto drain_pipe = [](HANDLE pipe, std::string& dst) {
        DWORD available = 0;
        if (!pipe || !PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
        const bool ready = available != 0;
        while (available) {
            char buffer[8192];
            DWORD count = 0;
            const DWORD chunk = (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
            if (!ReadFile(pipe, buffer, chunk, &count, nullptr) || !count) break;
            dst.append(buffer, count);
            available -= count;
        }
        return ready;
    };

    for (;;) {
        bool progress = drain_pipe(stdout_read, result.stdout_text);
        if (drain_pipe(stderr_read, result.stderr_text)) progress = true;
        if (stdin_write && offset < opts.stdin_data.size()) {
            DWORD count = 0;
            const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(4096, opts.stdin_data.size() - offset));
            if (WriteFile(stdin_write, opts.stdin_data.data() + offset, chunk, &count, nullptr)) {
                offset += count;
                progress = progress || count != 0;
            } else if (GetLastError() != ERROR_NO_DATA) {
                CloseHandle(stdin_write);
                stdin_write = nullptr;
            }
        }
        if (stdin_write && offset == opts.stdin_data.size()) {
            CloseHandle(stdin_write);
            stdin_write = nullptr;
        }
        if (WaitForSingleObject(pi.hProcess, progress ? 0 : 5) == WAIT_OBJECT_0) break;
        if (opts.timeout && std::chrono::steady_clock::now() - start >= *opts.timeout) {
            result.timed_out = true;
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            break;
        }
    }
    drain_pipe(stdout_read, result.stdout_text);
    drain_pipe(stderr_read, result.stderr_text);

    DWORD code = 0;
    if (GetExitCodeProcess(pi.hProcess, &code)) {
        result.exit_code = static_cast<int>(code);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    close_handles();
    return result;
}

#else

inline void set_nonblocking(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#ifdef F_SETNOSIGPIPE
    ::fcntl(fd, F_SETNOSIGPIPE, 1);
#endif
}

inline void set_cloexec(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

inline LProcessResult run_posix(
    const std::string& program,
    const std::vector<std::string>& args,
    const LProcessOptions& opts
) {
    LProcessResult result;
    if (program.empty()) {
        result.error = "Program name cannot be empty";
        return result;
    }

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int exec_err_pipe[2] = {-1, -1};

    auto close_pair = [](int (&p)[2]) {
        if (p[0] >= 0) { ::close(p[0]); p[0] = -1; }
        if (p[1] >= 0) { ::close(p[1]); p[1] = -1; }
    };
    auto close_all = [&]() {
        close_pair(stdin_pipe);
        close_pair(stdout_pipe);
        close_pair(stderr_pipe);
        close_pair(exec_err_pipe);
    };

    if (::pipe(stdin_pipe) != 0 || ::pipe(stdout_pipe) != 0 || ::pipe(exec_err_pipe) != 0) {
        result.error = std::string("pipe failed: ") + std::strerror(errno);
        close_all();
        return result;
    }
    if (!opts.merge_stderr && ::pipe(stderr_pipe) != 0) {
        result.error = std::string("pipe for stderr failed: ") + std::strerror(errno);
        close_all();
        return result;
    }
    set_cloexec(exec_err_pipe[0]);
    set_cloexec(exec_err_pipe[1]);

    // 在 fork() 之前准备 argv 与 envp，确保 fork 后子进程只调用异步信号安全操作
    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.push_back(program);
    for (const auto& a : args) {
        argv_storage.push_back(a);
    }
    std::vector<char*> argv_ptrs;
    argv_ptrs.reserve(argv_storage.size() + 1);
    for (auto& s : argv_storage) {
        argv_ptrs.push_back(s.data());
    }
    argv_ptrs.push_back(nullptr);

    bool custom_env = !opts.inherit_env || !opts.env.empty();
    std::vector<std::string> env_storage;
    std::vector<char*> env_ptrs;
    if (custom_env) {
        std::unordered_map<std::string, std::string> merged;
        if (opts.inherit_env && ::environ != nullptr) {
            for (char** ep = ::environ; *ep != nullptr; ++ep) {
                std::string entry(*ep);
                auto eq = entry.find('=');
                if (eq != std::string::npos) {
                    merged[entry.substr(0, eq)] = entry.substr(eq + 1);
                }
            }
        }
        for (const auto& kv : opts.env) {
            merged[kv.first] = kv.second;
        }
        env_storage.reserve(merged.size());
        for (const auto& kv : merged) {
            env_storage.push_back(kv.first + "=" + kv.second);
        }
        env_ptrs.reserve(env_storage.size() + 1);
        for (auto& s : env_storage) {
            env_ptrs.push_back(s.data());
        }
        env_ptrs.push_back(nullptr);
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        result.error = std::string("fork failed: ") + std::strerror(errno);
        close_all();
        return result;
    }

    if (pid == 0) {
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(exec_err_pipe[0]);
        if (!opts.merge_stderr) {
            ::close(stderr_pipe[0]);
        }

        auto write_err_and_exit = [&](int err_no) -> void {
            ssize_t unused = ::write(exec_err_pipe[1], &err_no, sizeof(err_no));
            (void)unused;
            ::_exit(127);
        };

        if (::dup2(stdin_pipe[0], STDIN_FILENO) < 0 ||
            ::dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
            ::dup2(opts.merge_stderr ? stdout_pipe[1] : stderr_pipe[1], STDERR_FILENO) < 0) {
            write_err_and_exit(errno);
        }
        ::close(stdin_pipe[0]);
        ::close(stdout_pipe[1]);
        if (!opts.merge_stderr) {
            ::close(stderr_pipe[1]);
        }

        if (!opts.working_directory.empty() && ::chdir(opts.working_directory.c_str()) != 0) {
            write_err_and_exit(errno);
        }
        if (custom_env) {
            ::environ = env_ptrs.data();
        }
        ::execvp(program.c_str(), argv_ptrs.data());
        write_err_and_exit(errno);
    }

    // 父进程
    ::close(stdin_pipe[0]); stdin_pipe[0] = -1;
    ::close(stdout_pipe[1]); stdout_pipe[1] = -1;
    if (!opts.merge_stderr) {
        ::close(stderr_pipe[1]); stderr_pipe[1] = -1;
    }
    ::close(exec_err_pipe[1]); exec_err_pipe[1] = -1;

    // 检测子进程 exec 是否立即失败
    int child_errno = 0;
    ssize_t err_n = ::read(exec_err_pipe[0], &child_errno, sizeof(child_errno));
    ::close(exec_err_pipe[0]);
    exec_err_pipe[0] = -1;
    if (err_n == static_cast<ssize_t>(sizeof(child_errno))) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        close_all();
        result.error = std::string("exec failed: ") + std::strerror(child_errno);
        return result;
    }

    result.started = true;
    set_nonblocking(stdin_pipe[1]);
    set_nonblocking(stdout_pipe[0]);
    if (!opts.merge_stderr) {
        set_nonblocking(stderr_pipe[0]);
    }

    std::size_t stdin_offset = 0;
    if (opts.stdin_data.empty()) {
        ::close(stdin_pipe[1]);
        stdin_pipe[1] = -1;
    }

    const auto start = std::chrono::steady_clock::now();
    char buf[4096];

    while (stdin_pipe[1] >= 0 || stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0) {
        int poll_ms = 50;
        if (opts.timeout.has_value()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (elapsed >= *opts.timeout) {
                result.timed_out = true;
                ::kill(pid, SIGKILL);
                break;
            }
            auto remaining = (*opts.timeout - elapsed).count();
            poll_ms = static_cast<int>(remaining < 50 ? remaining : 50);
            if (poll_ms < 1) {
                poll_ms = 1;
            }
        }

        struct pollfd fds[3]{};
        nfds_t nfds = 0;
        int idx_in = -1;
        int idx_out = -1;
        int idx_err = -1;

        if (stdin_pipe[1] >= 0) {
            idx_in = static_cast<int>(nfds);
            fds[nfds++] = {stdin_pipe[1], POLLOUT, 0};
        }
        if (stdout_pipe[0] >= 0) {
            idx_out = static_cast<int>(nfds);
            fds[nfds++] = {stdout_pipe[0], POLLIN, 0};
        }
        if (stderr_pipe[0] >= 0) {
            idx_err = static_cast<int>(nfds);
            fds[nfds++] = {stderr_pipe[0], POLLIN, 0};
        }

        int rc = ::poll(fds, nfds, poll_ms);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (idx_in >= 0) {
            if (fds[idx_in].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                ::close(stdin_pipe[1]);
                stdin_pipe[1] = -1;
            } else if (fds[idx_in].revents & POLLOUT) {
                ssize_t w = ::write(
                    stdin_pipe[1],
                    opts.stdin_data.data() + stdin_offset,
                    opts.stdin_data.size() - stdin_offset
                );
                if (w > 0) {
                    stdin_offset += static_cast<std::size_t>(w);
                    if (stdin_offset >= opts.stdin_data.size()) {
                        ::close(stdin_pipe[1]);
                        stdin_pipe[1] = -1;
                    }
                } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    ::close(stdin_pipe[1]);
                    stdin_pipe[1] = -1;
                }
            }
        }

        auto read_pipe = [&](int idx, int& fd, std::string& dst) {
            if (idx < 0) {
                return;
            }
            if (fds[idx].revents & (POLLIN | POLLHUP)) {
                for (;;) {
                    ssize_t r = ::read(fd, buf, sizeof(buf));
                    if (r > 0) {
                        dst.append(buf, static_cast<std::size_t>(r));
                    } else if (r == 0) {
                        ::close(fd);
                        fd = -1;
                        break;
                    } else if (errno == EINTR) {
                        continue;
                    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    } else {
                        ::close(fd);
                        fd = -1;
                        break;
                    }
                }
            } else if (fds[idx].revents & (POLLERR | POLLNVAL)) {
                ::close(fd);
                fd = -1;
            }
        };

        read_pipe(idx_out, stdout_pipe[0], result.stdout_text);
        read_pipe(idx_err, stderr_pipe[0], result.stderr_text);
    }

    close_all();

    int status = 0;
    for (;;) {
        if (opts.timeout.has_value() && !result.timed_out) {
            pid_t w = ::waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                break;
            }
            if (w < 0 && errno != EINTR) {
                break;
            }
            if (std::chrono::steady_clock::now() - start >= *opts.timeout) {
                result.timed_out = true;
                ::kill(pid, SIGKILL);
                ::waitpid(pid, &status, 0);
                break;
            }
            ::usleep(1000);
        } else {
            pid_t w = ::waitpid(pid, &status, 0);
            if (w >= 0 || errno != EINTR) {
                break;
            }
        }
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

#endif

} // namespace LProcessDetail

/// 跨平台子进程执行与进程信息工具
class LProcess {
public:
    /// 启动并等待子进程执行完毕
    [[nodiscard]] static LProcessResult run(
        const std::string& program,
        const std::vector<std::string>& args = {},
        const LProcessOptions& opts = {}
    ) {
#if LTOOL_PLATFORM_WINDOWS
        return LProcessDetail::run_windows(program, args, opts);
#else
        return LProcessDetail::run_posix(program, args, opts);
#endif
    }

    /// 通过系统默认 Shell 执行命令（POSIX 为 /bin/sh -c，Windows 为 cmd.exe /c）
    [[nodiscard]] static LProcessResult shell(
        const std::string& command,
        const LProcessOptions& opts = {}
    ) {
#if LTOOL_PLATFORM_WINDOWS
        return run("cmd.exe", {"/c", command}, opts);
#else
        return run("/bin/sh", {"-c", command}, opts);
#endif
    }

    /// 获取当前可执行文件的完整路径（失败返回空串）
    [[nodiscard]] static std::string executable_path() {
#if LTOOL_PLATFORM_WINDOWS
        std::wstring buf(MAX_PATH, L'\0');
        for (;;) {
            DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0) {
                return {};
            }
            if (n < buf.size()) {
                buf.resize(n);
                return LProcessDetail::wide_to_utf8(buf);
            }
            buf.resize(buf.size() * 2);
        }
#elif LTOOL_PLATFORM_APPLE
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size == 0) {
            return {};
        }
        std::string buf(size, '\0');
        if (_NSGetExecutablePath(buf.data(), &size) != 0) {
            return {};
        }
        buf.resize(std::strlen(buf.c_str()));
        char resolved[PATH_MAX];
        if (::realpath(buf.c_str(), resolved) != nullptr) {
            return std::string(resolved);
        }
        return buf;
#else
        std::string buf(4096, '\0');
        ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
        if (n <= 0) {
            return {};
        }
        buf.resize(static_cast<std::size_t>(n));
        return buf;
#endif
    }

    /// 获取当前可执行文件所在目录路径
    [[nodiscard]] static std::string executable_dir() {
        std::string exe = executable_path();
        if (exe.empty()) {
            return {};
        }
        auto pos = exe.find_last_of("/\\");
        if (pos == std::string::npos) {
            return {};
        }
        return exe.substr(0, pos);
    }

    /// 获取当前进程 ID
    [[nodiscard]] static std::int64_t current_pid() noexcept {
#if LTOOL_PLATFORM_WINDOWS
        return static_cast<std::int64_t>(GetCurrentProcessId());
#else
        return static_cast<std::int64_t>(::getpid());
#endif
    }

    /// 获取本机主机名
    [[nodiscard]] static std::string hostname() {
#if LTOOL_PLATFORM_WINDOWS
        DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
        std::wstring buf(len, L'\0');
        if (GetComputerNameW(buf.data(), &len)) {
            buf.resize(len);
            return LProcessDetail::wide_to_utf8(buf);
        }
        return {};
#else
        char buf[256] = {};
        if (::gethostname(buf, sizeof(buf) - 1) == 0) {
            buf[sizeof(buf) - 1] = '\0';
            return std::string(buf);
        }
        return {};
#endif
    }
};

namespace LTool {

/// 子进程工具类型别名
using Process = ::LProcess;

/// 子进程运行选项类型别名
using ProcessOptions = ::LProcessOptions;

/// 子进程运行结果类型别名
using ProcessResult = ::LProcessResult;

} // namespace LTool

#endif // LTOOL_LPROCESS_INCLUDE

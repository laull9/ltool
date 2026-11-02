-- ltool 自身的 xmake 工程：只用于构建和运行 tests/ 下的测试。
-- ltool 是纯头文件库，使用方只需 `-I ltool`，无需引入本文件。
--
--   xmake                 # 构建全部测试
--   xmake test            # 运行全部测试
--   xmake run LJson_test  # 运行单个测试

set_project("ltool")
set_version("1.0.0")
set_xmakever("2.8.5")

option("modules")
    set_default(true)
    set_showmenu(true)
    set_description("Build LModule_test (import ltool;); needs C++20 module scanning, e.g. LLVM clang 17+ (--toolchain=clang), GCC 14+ or MSVC")
option_end()

add_rules("mode.debug", "mode.release")
set_languages("c++20")

-- macOS 默认的 Xcode Apple clang 无法完成 C++20 模块依赖扫描，且其 libc++ 与自带
-- range-v3 的 std 前向声明冲突；未显式指定 --toolchain 时改用 PATH 中的 LLVM clang
-- （如 Homebrew llvm）。其他平台沿用 xmake 默认工具链。
if is_plat("macosx") and not get_config("toolchain") then
    set_toolchains("clang")
end

-- 测试依赖；ltool 本身的依赖都在 pkgs/ 中随仓库提供
add_requires("catch2 3.x")
add_requires("nlohmann_json", "jsoncpp")

-- 生成 compile_commands.json 供 clangd 使用
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

includes("tests")

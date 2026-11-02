-- ltool 测试
--
-- 约定：tests 目录下每个 *.cpp 生成一个独立的测试可执行文件，目标名 = 文件名（不带扩展名），
-- 例如 tests/LJson_test.cpp -> target("LJson_test")。

local ltool_root = path.join(os.scriptdir(), "..")

for _, filepath in ipairs(os.files(path.join(os.scriptdir(), "*.cpp"))) do
    local name = path.basename(filepath)

    -- 模块测试依赖编译器的 C++20 模块依赖扫描，可用 `xmake f --modules=n` 关闭
    if name ~= "LModule_test" or has_config("modules") then
        target(name)
            set_kind("binary")
            set_group("test")
            add_tests("default")

            add_files(filepath)
            add_includedirs(ltool_root)
            add_includedirs(path.join(ltool_root, "pkgs/range-v3/include"))
            add_packages("catch2")

            -- LJson 互操作适配器测试
            if name == "LJson_test" then
                add_packages("nlohmann_json", "jsoncpp")
            end

            -- C++20 模块：编译 ltool 模块接口单元，测试中 `import ltool;`
            if name == "LModule_test" then
                add_files(path.join(ltool_root, "modules/ltool.cppm"))
                set_policy("build.c++.modules", true)
            end

            if is_plat("macosx", "bsd") then
                add_links("iconv")
            end
        target_end()
    end
end

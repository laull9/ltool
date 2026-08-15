#!/usr/bin/env python3
"""
LTool 本地适配脚本：把 vendored cinatra 的前缀 include 改写为相对路径。

背景：ltool 约定"无外部声明"——使用方只需 -I ltool（或把 ltool 复制进项目），
头文件内部依赖一律用相对 include 自包含。Cinatra 原生使用 `"asio/..."`、
`"async_simple/..."`、`"cinatra/..."` 等前缀 include，必须把 include 根指向
pkgs/cinatra/，违反该约定。

本脚本把所有 `#include <asio/`、`#include "asio/`、`<async_simple/`、
`"async_simple/`（以及 cinatra/ 子目录内文件的 `"cinatra/`）改写为以
pkgs/cinatra/ 为根的相对路径（按文件所在目录深度计算 `../` 数量），
使 LWeb.hpp 只需要 `-I ltool` 即可编译。

用法（在 ltool 仓库根目录执行）：
    python3 pkgs/cinatra_fix_relative_includes.py

升级 Cinatra 后必须重新运行本脚本（先重新拷入新版 cinatra，再执行）。
脚本幂等：已改写的相对路径不会被重复替换。
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), 'cinatra'))
PREFIX_PAT = re.compile(r'(#\s*include\s*[<"])(asio/|async_simple/|cinatra/)')
EXTS = ('.hpp', '.h', '.ipp', '.cpp', '.cc', '.cxx')


def depth_from_root(path: str) -> int:
    """文件所在目录相对 pkgs/cinatra/ 的深度（asio/x.hpp -> 1）。"""
    return len(os.path.relpath(path, ROOT).split(os.sep)) - 1


def main() -> int:
    changed = files = 0
    for dirpath, _, filenames in os.walk(ROOT):
        for fn in filenames:
            if not fn.endswith(EXTS):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, encoding='utf-8') as f:
                content = f.read()
            rel = '../' * depth_from_root(path)
            new = PREFIX_PAT.sub(lambda m: m.group(1) + rel + m.group(2),
                                 content)
            if new != content:
                with open(path, 'w', encoding='utf-8') as f:
                    f.write(new)
                changed += 1
                files += 1
    print(f'cinatra include 适配：修改 {files} 个文件')
    return 0


if __name__ == '__main__':
    sys.exit(main())

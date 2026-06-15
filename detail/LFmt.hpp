/**
 * @file detail/LFmt.hpp
 * @brief ltool 内部统一的 fmt 引入入口。
 */

#ifndef LTOOL_LFMT_INCLUDE
#define LTOOL_LFMT_INCLUDE

#include "LConfig.hpp"

#if LTOOL_USE_EXTERNAL_FMT
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#else
#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY 1
#define LTOOL_FMT_HEADER_ONLY_ADD 1
#endif // !FMT_HEADER_ONLY
#include "../pkgs/fmt/format.h"
#include "../pkgs/fmt/ranges.h"
#include "../pkgs/fmt/std.h"
#ifdef LTOOL_FMT_HEADER_ONLY_ADD
#undef FMT_HEADER_ONLY
#endif // LTOOL_FMT_HEADER_ONLY_ADD
#endif // LTOOL_USE_EXTERNAL_FMT

#endif // LTOOL_LFMT_INCLUDE

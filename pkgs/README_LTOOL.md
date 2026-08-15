# Barkeep External Source Note

Source: https://github.com/oir/barkeep (v0.1.6, single-header `barkeep.h`)

`pkgs/` is reserved for external code. LTool-owned progress display facades
live in `ltool/LProgress.hpp`.

## Cinatra include 本地化适配

Source: https://github.com/qicosmos/cinatra (vendored at `pkgs/cinatra/`)

ltool 约定"无外部声明"：使用方只需 `-I ltool`（或把 ltool 复制进项目），
头文件内部依赖一律用相对 include 自包含，不在构建系统中声明额外 include 路径。

Cinatra 原生使用 `#include "asio/..."`、`#include "async_simple/..."`、
`#include "cinatra/..."`（以及尖括号形式）等前缀 include，必须把 include 根
指向 `pkgs/cinatra/`，违反该约定。因此做了本地化改写：

- 所有 `asio/`、`async_simple/`、`cinatra/` 前缀 include（`<...>` 与 `"..."`
  形式，含 asio/async_simple/ylt 内部文件）统一改写为以 `pkgs/cinatra/` 为
  根的相对路径（按文件所在目录深度加 `../`）。
- `use_asio.hpp` / `smtp_client.hpp` 的 `#include <asio.hpp>` 改为
  `#include "../asio.hpp"`。
- 不改写 `ylt/` 前缀（ylt 位于 `cinatra/ylt/`，相对自身目录可直接解析）、
  条件编译的系统库（brotli/zlib）与标准库头。

升级 Cinatra 时：重新拷入新版后执行 `python3 pkgs/cinatra_fix_relative_includes.py`
（脚本幂等，可重复运行）。此改动不影响外部使用者以 `-I pkgs/cinatra` 方式
使用 cinatra 本身。

## LTool-local modifications

Backward-compatible additions made on top of the upstream header so that
`LProgress` can offer terminal-adaptive bar width (including runtime resize
handling) and on-by-default statistics (elapsed time / speed / ETA):

- `BarParts`: appended `elapsed_left_modifier` / `elapsed_right_modifier` /
  `eta_left_modifier` / `eta_right_modifier` (empty by default). The `Rich`
  style entry sets them to light green (`\033[92m`) and light blue
  (`\033[94m`) for the stats block.
- `ProgressBarConfig`: added fields `width` (0 = default 30), `show_elapsed`,
  `show_eta` (both default `false` to preserve upstream behavior), and
  `width_provider` (per-render `std::function<size_t(Duration, double)>`;
  with `width == 0` it is re-queried on every render so the bar adapts to
  terminal resizes; it receives the current elapsed time and speed so the
  caller can fit the line precisely; returning 0 falls back to the default
  width).
- `ProgressBarDisplay`:
  - `width_` changed from `static constexpr size_t width_ = 30` to a settable
    instance member (still defaults to 30).
  - Added `width_provider_`, `start_time_`, `show_elapsed_`, `show_eta_` and
    the `format_duration_` / `compute_eta_` / `has_stats_` helpers.
  - `render_()` re-measures the width via `width_provider_` on every render.
  - The default (non-`format`) render path appends a compact stats block
    `[elapsed - eta]` (`mm:ss`) when enabled, coloring the two timers through
    the `BarParts` modifiers; ETA is only rendered when speed is enabled.
- `IterableBarConfig`: added the same `width` / `show_elapsed` / `show_eta` /
  `width_provider` fields; `IterableBar` forwards them into the internal
  `ProgressBarConfig`.

Terminal-width detection itself lives in `ltool/LProgress.hpp`
(`LProgress::detail::terminal_columns` / `resolve_bar_width`), so barkeep
stays platform-agnostic. When upgrading the vendored header, re-apply these
changes.

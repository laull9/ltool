# Barkeep External Source Note

Source: https://github.com/oir/barkeep (v0.1.6, single-header `barkeep.h`)

`pkgs/` is reserved for external code. LTool-owned progress display facades
live in `ltool/LProgress.hpp`.

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

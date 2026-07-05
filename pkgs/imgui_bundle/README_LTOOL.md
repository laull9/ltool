# ImGui Bundle External Source Note

Source: https://github.com/pthom/imgui_bundle

Imported external header-only piece:

- `imspinner/` from submodule commit
  `ffe57a9cf741a92bdb6042cd4f8eb152b9c95b1d`.

`pkgs/` is reserved for external code. LTool-owned Dear ImGui helper facades
such as table/drawing/widget helpers and optional ImPlot wrappers live in
`ltool/detail/LImGuiBundle.hpp`.

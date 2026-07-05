# HelloImGui External Source Note

Source: https://github.com/pthom/hello_imgui

Reference commit used when shaping the LTool facade:
`e24530d0591a346f7869a007ad4b17d995cd01c7`.

`pkgs/` is reserved for external code. The LTool-owned header-only
HelloImGui-compatible facade lives in `ltool/detail/LHelloImGui.hpp`, not in
this directory.

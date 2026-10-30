# Third-party notices

The project-specific host, renderer, scripts and documentation are provided
under the root MIT license. Third-party components retain their own notices.
This license does not grant rights to Electronic Arts game code, assets or media.

## Binary-derived game sources

`src/recomp/` contains C lifted from the supported Porsche.exe and dx7z.dll,
including dispatch tables, declarations and project-specific adapters. The
underlying game code belongs to its respective rights holders and is not
licensed under the project's MIT license. Original executables and game assets
are not included; users must supply their own supported installation.

## Vendored pcrecomp source

- Upstream: https://github.com/sp00nznet/pcrecomp
- Base revision: `a85ea970f9e0640ab6546d2ca7a01fccb132973d`
- License: MIT, copyright 2026 sp00nz; see `third_party/pcrecomp/LICENSE`.
- Additional upstream notices: `third_party/pcrecomp/NOTICE` and `LICENSES/`.

The snapshot includes local runtime modifications for SDL3, rendering, input,
threading, memory, paths and title-specific hooks. It is committed as ordinary
source files rather than an unmodified submodule.

## External dependencies

Dependencies are obtained separately; their binaries are not included:

| Component | Purpose | License / upstream |
| --- | --- | --- |
| SDL3 | Windowing, input, audio and GPU abstraction | zlib; https://github.com/libsdl-org/SDL |
| SDL3_ttf | Font rendering in the Win32 HLE layer | zlib, with dependency notices; https://github.com/libsdl-org/SDL_ttf |
| llvm-mingw / LLVM | Windows compiler and runtime | Toolchain notices; https://github.com/mstorsjo/llvm-mingw |
| DirectX Shader Compiler | Compile authored HLSL shaders | LLVM/Microsoft notices; https://github.com/microsoft/DirectXShaderCompiler |
| @webgpu/glslang | Compile authored GLSL to SPIR-V | glslang notices; https://github.com/kainino0x/glslang.js |
| Capstone | Disassembly for local source generation | BSD; https://github.com/capstone-engine/capstone |
| pefile | PE inspection for local source generation | MIT; https://github.com/erocarrera/pefile |

Python, Node.js, CMake, PowerShell and Git retain their respective licenses.

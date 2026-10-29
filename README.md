# NFS Porsche Unleashed recompilation

An experimental preservation project for the **USA PC 3.5** version of
**Need for Speed: Porsche Unleashed**. It lifts the original x86 program to C
with pcrecomp and runs it in a native 64-bit host. A custom SDL3 GPU renderer
replaces the original Thrash/DirectX 7 presentation path.

The aim is native support for **Windows 11, Linux and macOS**. Windows startup,
menus, a Cote d'Azur race, keyboard acceleration and widescreen rendering have
been checked. Linux and macOS builds remain experimental and have not been
validated on their native operating systems. This is not a complete or
original-source recreation of the game.

**NFS Porsche Unleashed is an Electronic Arts game. This independent project
is not affiliated with, endorsed by, or sponsored by Electronic Arts.** The
game's names, trademarks, code and assets belong to their respective owners.

## What is included

- Project-authored host integration, input handling, renderer and shaders.
- A locally modified, vendored pcrecomp source snapshot and its license notices.
- Build/preparation scripts and asset-free regression tests.
- Function-address catalogs used to generate the private lift locally.

**No game assets, original executables, generated game-function C, disc images,
compiled applications, libraries or shader bytecode are included.** You must
provide your own legally obtained installation. The game executable is also
needed at runtime to load its original data and resources.

## Tools and dependencies

| Tool | Use |
| --- | --- |
| [pcrecomp](https://github.com/sp00nznet/pcrecomp) | x86 analysis/lifting and the Win32 HLE runtime; the modified source is vendored in `third_party/pcrecomp` |
| Python 3.10+; Capstone and pefile | Generate private C from your local binaries; versions are pinned in `requirements-lift.txt` |
| [SDL3](https://github.com/libsdl-org/SDL), [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) | Windowing, input, audio, fonts and the GPU API |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) / Clang | Windows compilation; tested with the 20261020 MSVCRT x86-64 toolchain |
| CMake 3.16+ and a C11 compiler | Native builds and regression-test orchestration |
| PowerShell | Windows dependency download/build and private-lift maintenance |
| Node.js and npm | Shader generation; `@webgpu/glslang` is pinned by the lockfile |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) | Compile HLSL to DXIL; tested with DXC 1.9.2609 |
| LLVM inspection utilities and REA | Binary inspection and debugging during development; not runtime requirements |

The Windows download script pins **SDL3 3.4.18** and **SDL3_ttf 3.2.2** official
MinGW development archives and verifies their SHA-256 hashes. The CMake build
requires SDL3 >=3.2.6 and SDL3_ttf >=3.2 with CMake package configuration files.

## 1. Supply the game files

From the repository root, copy a complete installation into the ignored
`game/` directory. Preserve the original relative paths, including:

```text
game/
  Porsche.exe
  setup/thrash/dx7z.dll
  GameData/
  FEData/
  SaveData/
  ...other original installation files...
```

Only these input versions are supported by the current lift and native hooks:

| Input | SHA-256 |
| --- | --- |
| `game/Porsche.exe` | `02437ddbf41af86dffe14251b28ed8889bd1c4c904fe77ff730d8912257ec90d` |
| `game/setup/thrash/dx7z.dll` | `0433cc93106603e9e0a81168e2fa44c40e3121a71b7ae9cd6f3b1f7551f3fc18` |

Other editions or modified executables need new analysis; changing a checksum
alone does not make their addresses compatible. Game files stay local and
must not be added to Git.

## 2. Generate the private C sources

Install Python, then run from the repository root:

```sh
python -m venv .venv
# Windows PowerShell:
.\.venv\Scripts\Activate.ps1
# Linux/macOS instead: source .venv/bin/activate
python -m pip install -r requirements-lift.txt
python scripts/prepare-recomp.py
```

This verifies the input hashes, uses the checked-in function catalogs and
vendored lifter, writes `src/recomp/gen/` and `src/recomp/dx7z/`, merges the
dispatch/declaration files, and installs loop-scheduling, widescreen and HUD
adapters. All generated files are ignored. Existing private lifts are not
overwritten; use `--output-root` to generate a separate comparison copy.

For an older existing lift, `scripts/restore-loop-yields.ps1` restores missing
scheduling points and `scripts/install-widescreen-hooks.ps1` restores the native
adapters. The Windows build runs the adapter installer automatically.

## 3. Generate the renderer shaders

The authored HLSL, GLSL and Metal sources are included. Their compiled output
and generated `nfspu_shaders.h` wrapper are intentionally excluded.

Install Node.js/npm and DXC, then:

```powershell
npm ci --ignore-scripts
$env:DXC = 'C:\tools\dxc\bin\x64\dxc.exe'
npm run shaders
```

On Linux, set `DXC` to an installed `dxc` executable before running the same
npm commands. The script generates DXIL, SPIR-V and an embedded Metal source
string. For macOS development, generate this header on a Windows/Linux machine
with DXC available and copy the generated header locally, or provide a working
DXC build. Normal compilation uses the generated header and does not invoke a
shader compiler. Do not commit these generated artifacts.

## 4. Compile on Windows

Install a 64-bit llvm-mingw toolchain, then run from the repository root:

```powershell
powershell -NoProfile -File scripts/fetch-sdl3-windows.ps1
powershell -NoProfile -File scripts/build-windows.ps1 -LlvmMingw 'C:\tools\llvm-mingw'
```

`-LlvmMingw` must point to the toolchain root containing `bin/clang.exe`.
`LLVM_MINGW` can supply the same path. `-SDL3Root` and `-SDL3TtfRoot` can override
the dependency directories. The default SDL paths match the pinned downloads.
The build is incremental and copies the required runtime DLLs beside the EXE.

Output: `build/windows/PorscheUnleashed.exe` and `nfspu_gpu.dll`, alongside
`SDL3.dll`, `SDL3_ttf.dll` and `libwinpthread-1.dll`. Keep them together. None of
these files belongs in the source repository.

## Linux and macOS compilation (experimental)

After generating the private lift and shader header, install SDL3/SDL3_ttf
development packages, CMake, a C11 compiler and a native build tool:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel
./build/native/PorscheUnleashed game/Porsche.exe
```

Supply `SDL3_DIR` and `SDL3_ttf_DIR` if CMake cannot find those packages. Keep
`libnfspu_gpu.so` (Linux) or `libnfspu_gpu.dylib` (macOS) beside the host. The
same renderer targets Direct3D 12, Vulkan and Metal through SDL3 GPU. These
commands describe the intended build path, not a claim of validated ports.

## Run on Windows

From the repository root:

```powershell
.\build\windows\PorscheUnleashed.exe
# An explicit path to the supported original executable also works:
.\build\windows\PorscheUnleashed.exe .\game\Porsche.exe
```

The launcher finds `game/Porsche.exe` relative to the host, including Explorer
launches, and uses its directory as the game-data root. It exposes those local
files as a virtual data CD on guest drive D:, so a complete installation does
not need a physical CD or mounted image. No Windows registry installation is
required.

`PorscheUnleashed.log`, `PorscheUnleashed.graphics.ini` and the portable
`PorscheUnleashed.registry.txt` are stored beside the host. Player saves and
other native preferences stay in `game/SaveData/`.

## Graphics and controls

- **F10:** graphics overlay; use arrows/mouse and choose Apply. Escape cancels
  an unapplied draft. The overlay consumes input but does not pause a race.
- **F11 / Alt+Enter:** borderless fullscreen.
- **Render Scale:** 100/150/200/300% actual offscreen rendering detail.
- **Proportional Wide:** a native wide race canvas and a wider horizontal view;
  window/mode changes apply at a race mode switch. Original menus/movies stay 4:3.
- Texture filtering, final image scaling, VSync and frame statistics are also
  available. Original bitmap assets do not gain detail from a higher render scale.

When both instrument styles are enabled, overlapping digital speed/RPM widgets
are placed to the left of the analogue speedometer. Their native labels,
colours, values and saved HUD records are retained.

The old game's cinematic "Wide Race Camera" option is separate from proportional
widescreen. Original resolution choices and the host output size are also
separate: the frontend's logical canvas can remain 640x480.

## Verification

The CMake tests require prepared local inputs for configuration, but the test
executables themselves use synthetic data rather than loading game assets.
Use a configured CMake build with SDL3/SDL3_ttf and an appropriate native build
tool. For example, with Ninja on Windows:

```powershell
cmake -S . -B build/tests -G Ninja -DCMAKE_C_COMPILER='C:/tools/llvm-mingw/bin/clang.exe' -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests --target nfspu_gpu_smoke nfspu_overlay_ui_test nfspu_overlay_gpu_test nfspu_media_test nfspu_hud_layout_test nfspu_registry_test nfspu_widescreen_test nfspu_runtime_test nfspu_keyboard_test nfspu_dinput_test nfspu_thrash_test
ctest --test-dir build/tests --output-on-failure
```

On Linux/macOS, use the native compiler/generator instead. GPU tests need a
working graphics session and backend. Tests cover pixels/render states, the
overlay, virtual media, HUD positions, registry restoration in separate
processes, widescreen math, thread lifetime, keyboard and DirectInput ABI.
The Windows Direct3D 12/Vulkan regression suites passed during development.

Useful diagnostics:

| Environment variable | Purpose |
| --- | --- |
| `NFSPU_GPU_DRIVER` | Select `direct3d12`, `vulkan`, or `metal` where available |
| `NFSPU_GPU_DEBUG=1` | Enable SDL GPU validation |
| `NFSPU_THRASH_TRACE=1` | Trace renderer states |
| `NFSPU_TRACE=1` | Trace HLE imports; verbose and affects timing |
| `NFSPU_GPU_LIBRARY` | Override the native renderer library path |

## Known limitations

Full gameplay, saves, gamepads, force feedback and networking are not fully
validated. The renderer implements one texture stage; multitexture effects,
mipmap fidelity and special depth/stencil paths need further work. Optional
`gimme.dll` portrait conversion is unavailable in the portable host.
Unsupported instructions remain in the lift; a successful build is not proof
of full binary compatibility. Distant scenery can still pop into view at the
original engine's visibility limits. Frontend "Error in expression" parser
diagnostics have been observed and their individual causes remain unresolved;
"Current year" / "Current era" messages are normal career-state logging.

## License and attribution

Project-authored code is licensed under [MIT](LICENSE), copyright 2026
Sergei Biriukov. Vendored dependencies retain their original licenses; see
[third-party notices](THIRD_PARTY_NOTICES.md). The MIT license does not cover or
redistribute Electronic Arts' proprietary game code, assets, music or trademarks.

## AI disclosure

AI tools, including OpenAI Codex, were used to create this recompilation and
assist with reverse engineering, implementation, debugging, tests and documentation.

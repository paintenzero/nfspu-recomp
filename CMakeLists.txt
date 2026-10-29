cmake_minimum_required(VERSION 3.16)
project(PorscheUnleashed LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(PCRECOMP "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pcrecomp")
option(NFSPU_SDL3_GPU "Build the portable SDL3 GPU Thrash replacement" ON)

# SDL3 hosts both the Win32 presentation/audio layer and the GPU renderer.
if(WIN32)
    if(NOT SDL3_DIR)
        set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl/SDL3-3.4.18/x86_64-w64-mingw32/lib/cmake/SDL3")
    endif()
    if(NOT SDL3_ttf_DIR)
        set(SDL3_ttf_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl/SDL3_ttf-3.2.2/x86_64-w64-mingw32/lib/cmake/SDL3_ttf")
    endif()
endif()
find_package(SDL3 3.2.6 CONFIG REQUIRED)
find_package(SDL3_ttf 3.2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
include(CTest)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/recomp_decls.h")
    message(FATAL_ERROR "Private game lift missing. Run python scripts/prepare-recomp.py with your local game files; see README.md.")
endif()
if(NFSPU_SDL3_GPU AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/nfspu_shaders.h")
    message(FATAL_ERROR "Shader header missing. Run npm ci --ignore-scripts, set DXC, then npm run shaders; see README.md.")
endif()
if(NFSPU_SDL3_GPU)
    add_library(nfspu_gpu SHARED src/renderer/nfspu_gpu.c src/renderer/graphics_overlay.c)
    if(WIN32)
        set_target_properties(nfspu_gpu PROPERTIES PREFIX "")
    endif()
    target_include_directories(nfspu_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer")
    target_link_libraries(nfspu_gpu PRIVATE SDL3::SDL3)
    if(NOT WIN32)
        target_link_libraries(nfspu_gpu PRIVATE m)
    endif()
    add_executable(nfspu_gpu_smoke src/renderer/gpu_smoke.c)
    target_link_libraries(nfspu_gpu_smoke PRIVATE nfspu_gpu)
    if(BUILD_TESTING)
        add_executable(nfspu_overlay_ui_test src/renderer/graphics_overlay_test.c src/renderer/graphics_overlay.c)
        target_include_directories(nfspu_overlay_ui_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer")
        add_executable(nfspu_overlay_gpu_test src/renderer/graphics_overlay_gpu_test.c)
        target_link_libraries(nfspu_overlay_gpu_test PRIVATE nfspu_gpu SDL3::SDL3)
        add_test(NAME gpu_pixels COMMAND nfspu_gpu_smoke)
        set_tests_properties(gpu_pixels PROPERTIES TIMEOUT 45)
        add_test(NAME graphics_overlay_ui COMMAND nfspu_overlay_ui_test)
        add_test(NAME graphics_overlay_gpu COMMAND nfspu_overlay_gpu_test)
        set_tests_properties(graphics_overlay_ui graphics_overlay_gpu PROPERTIES TIMEOUT 45)
        set_tests_properties(graphics_overlay_gpu PROPERTIES ENVIRONMENT "NFSPU_GPU_DEBUG=1")
    endif()
endif()

file(GLOB PORSCHE_GEN CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/gen/recomp_*.c")
if(WIN32)
    execute_process(COMMAND powershell -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-widescreen-hooks.ps1"
        RESULT_VARIABLE NFSPU_HOOK_RESULT)
    if(NOT NFSPU_HOOK_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not install widescreen lift adapters")
    endif()
endif()
file(GLOB DX7Z_GEN CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/dx7z/recomp_*.c")
list(FILTER PORSCHE_GEN EXCLUDE REGEX "recomp_dispatch\\.c$")
list(FILTER DX7Z_GEN EXCLUDE REGEX "recomp_dispatch\\.c$")

# Lifted functions call across translation units; the generated files do not
# include their declarations themselves.
set_source_files_properties(${PORSCHE_GEN} ${DX7Z_GEN} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/recomp_decls.h"
)

set(HLE_CORE
    advapi32.c bink.c ddraw.c dsound.c gdi32.c gdidc.c guest_mem.c hle_path.c
    host.c host_lite.c kernel32.c kernel32_crt.c kernel32_ext.c lz32.c module.c
    mss.c ole32.c pe_loader.c present.c screen.c storage.c user32.c winmm.c wsock32.c
)
set(HLE_SOURCES)
foreach(f ${HLE_CORE})
    list(APPEND HLE_SOURCES "${PCRECOMP}/runtime/win32hle/${f}")
endforeach()

add_executable(PorscheUnleashed
    src/host/main.c
    src/host/dinput.c
    src/host/game_shims.c
    src/host/thrash.c
    src/host/widescreen.c
    src/host/hud_layout.c
    src/recomp/recomp_dispatch.c
    ${PORSCHE_GEN}
    ${DX7Z_GEN}
    ${HLE_SOURCES}
)

function(nfspu_configure_host target)
target_include_directories(${target} PRIVATE
    "${PCRECOMP}/runtime/recomp32"
    "${PCRECOMP}/runtime/win32hle"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/dx7z"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
)
target_link_libraries(${target} PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf Threads::Threads ${CMAKE_DL_LIBS})
if(NFSPU_SDL3_GPU)
    add_dependencies(${target} nfspu_gpu)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(${target} PRIVATE RECOMP_MEM_BASE=0x100000000000ull)
endif()

if(WIN32)
    target_sources(${target} PRIVATE src/host/win_mman.c src/host/win_socket.c)
    target_include_directories(${target} BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/host/wincompat"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
    )
    target_compile_options(${target} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/src/host/win_posix.h")
    target_link_libraries(${target} PRIVATE ws2_32)
    target_link_options(${target} PRIVATE -Wl,--stack,16777216)

    # A normal desktop launch does not inherit the compiler's bin directory.
    # Keep the runtime dependencies beside the executable in every build tree.
    get_filename_component(COMPILER_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
    find_file(WINPTHREAD_RUNTIME NAMES libwinpthread-1.dll
        HINTS "${COMPILER_BIN}" "${COMPILER_BIN}/../${CMAKE_C_COMPILER_TARGET}/bin")
    if(NOT WINPTHREAD_RUNTIME)
        message(FATAL_ERROR "Set WINPTHREAD_RUNTIME to the toolchain's libwinpthread-1.dll")
    endif()
    foreach(runtime_dll "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE:SDL3_ttf::SDL3_ttf>" "${WINPTHREAD_RUNTIME}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${runtime_dll}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endforeach()
else()
    target_link_libraries(${target} PRIVATE m)
    if(APPLE)
        target_link_options(${target} PRIVATE -Wl,-stack_size,0x1000000)
    else()
        target_link_options(${target} PRIVATE -Wl,-z,stacksize=16777216)
    endif()
endif()

target_compile_options(${target} PRIVATE -O1 -fno-strict-aliasing -fwrapv -w)
if(NOT WIN32)
    target_compile_options(${target} PRIVATE -pthread)
endif()
endfunction()

nfspu_configure_host(PorscheUnleashed)

# Exercises scheduling flags and per-native-thread guest allocations without
# SDL, game data, or lifted translation units.
add_executable(nfspu_runtime_test src/host/runtime_thread_test.c
    "${PCRECOMP}/runtime/win32hle/host_lite.c"
    "${PCRECOMP}/runtime/win32hle/guest_mem.c")
target_include_directories(nfspu_runtime_test PRIVATE
    "${PCRECOMP}/runtime/recomp32" "${PCRECOMP}/runtime/win32hle")
target_link_libraries(nfspu_runtime_test PRIVATE Threads::Threads)
target_compile_options(nfspu_runtime_test PRIVATE -O1 -fno-strict-aliasing -fwrapv)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(nfspu_runtime_test PRIVATE RECOMP_MEM_BASE=0x100000000000ull)
endif()
if(WIN32)
    target_sources(nfspu_runtime_test PRIVATE src/host/win_mman.c)
    target_include_directories(nfspu_runtime_test BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/host/wincompat" "${CMAKE_CURRENT_SOURCE_DIR}/src/host")
    target_compile_options(nfspu_runtime_test PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/src/host/win_posix.h")
    add_custom_command(TARGET nfspu_runtime_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WINPTHREAD_RUNTIME}"
            "$<TARGET_FILE_DIR:nfspu_runtime_test>" VERBATIM)
else()
    target_link_libraries(nfspu_runtime_test PRIVATE m)
endif()
if(BUILD_TESTING)
    add_executable(nfspu_media_test src/host/media_test.c ${HLE_SOURCES})
    nfspu_configure_host(nfspu_media_test)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nfspu-media-test/nested")
    add_test(NAME local_game_media COMMAND nfspu_media_test)
    set_tests_properties(local_game_media PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nfspu-media-test" TIMEOUT 30)
    add_executable(nfspu_hud_layout_test src/host/hud_layout_test.c src/host/hud_layout.c)
    target_include_directories(nfspu_hud_layout_test PRIVATE "${PCRECOMP}/runtime/recomp32")
    add_test(NAME hud_instrument_layout COMMAND nfspu_hud_layout_test)
    add_executable(nfspu_registry_test src/host/registry_test.c ${HLE_SOURCES})
    nfspu_configure_host(nfspu_registry_test)
    add_test(NAME registry_write COMMAND nfspu_registry_test write "$<TARGET_FILE_DIR:nfspu_registry_test>/native-options.registry-test.txt")
    add_test(NAME registry_restore COMMAND nfspu_registry_test read "$<TARGET_FILE_DIR:nfspu_registry_test>/native-options.registry-test.txt")
    set_tests_properties(registry_write PROPERTIES FIXTURES_SETUP native_registry TIMEOUT 30)
    set_tests_properties(registry_restore PROPERTIES FIXTURES_REQUIRED native_registry TIMEOUT 30)
    add_executable(nfspu_widescreen_test src/host/widescreen_test.c src/host/widescreen.c)
    target_include_directories(nfspu_widescreen_test PRIVATE "${PCRECOMP}/runtime/recomp32")
    if(NOT WIN32)
        target_link_libraries(nfspu_widescreen_test PRIVATE m)
    endif()
    add_test(NAME widescreen_camera COMMAND nfspu_widescreen_test)
    add_test(NAME runtime_thread_lifetime COMMAND nfspu_runtime_test)
    set_tests_properties(runtime_thread_lifetime PROPERTIES TIMEOUT 30)
endif()

if(NFSPU_SDL3_GPU)
    add_executable(nfspu_keyboard_test src/host/keyboard_integration_test.c src/host/dinput.c src/host/thrash.c src/host/widescreen.c ${HLE_SOURCES})
    nfspu_configure_host(nfspu_keyboard_test)
    target_link_libraries(nfspu_keyboard_test PRIVATE nfspu_gpu)
    add_executable(nfspu_dinput_test src/host/dinput_test.c src/host/dinput.c ${HLE_SOURCES})
    nfspu_configure_host(nfspu_dinput_test)
    add_executable(nfspu_thrash_test src/host/thrash_bridge_test.c src/host/thrash.c src/host/widescreen.c ${HLE_SOURCES})
    nfspu_configure_host(nfspu_thrash_test)
    if(WIN32)
        add_custom_command(TARGET nfspu_gpu POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:nfspu_gpu>"
            VERBATIM)
    endif()
    if(BUILD_TESTING)
        add_test(NAME thrash_guest_abi COMMAND nfspu_thrash_test)
        add_test(NAME keyboard_input_bridge COMMAND nfspu_keyboard_test)
        add_test(NAME directinput_device2_abi COMMAND nfspu_dinput_test)
        set_tests_properties(directinput_device2_abi PROPERTIES TIMEOUT 45 WORKING_DIRECTORY "$<TARGET_FILE_DIR:nfspu_dinput_test>")
        set_tests_properties(keyboard_input_bridge PROPERTIES TIMEOUT 45 WORKING_DIRECTORY "$<TARGET_FILE_DIR:nfspu_keyboard_test>")
        set_tests_properties(thrash_guest_abi PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:nfspu_thrash_test>" TIMEOUT 45)
    endif()
endif()

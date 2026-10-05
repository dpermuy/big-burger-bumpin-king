cmake_minimum_required(VERSION 3.20)
project(BigBumpinRecomp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Homebrew LLVM's libc++ no longer transitively includes <cstdlib> from
# headers that used to provide it; vendored fmt 11.0.2 relies on malloc/free
# being visible without including it directly. Force it in globally (C++
# only — cstdlib isn't a valid header for the C sources in this tree)
# rather than patching vendored third-party source.
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:cstdlib>
)

add_subdirectory(thirdparty/XenonRecomp)
add_subdirectory(tools)

file(GLOB BIGBUMPIN_PPC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/private/ppc/*.cpp")

if(BIGBUMPIN_PPC_SOURCES)
    add_library(BigBumpinPPC STATIC ${BIGBUMPIN_PPC_SOURCES})
    target_include_directories(BigBumpinPPC PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private/ppc")
    target_link_libraries(BigBumpinPPC PRIVATE XenonUtils)
    # Genuine, side-effect-free idle/wait loops in the real game code (e.g.
    # the top-level "spin forever once the main loop returns" fallback at
    # sub_825DEC18, Phase 3 Finding 126) are technically UB per the C++
    # standard's forward-progress rules, and clang's optimizer is allowed to
    # (and does, at -O3) replace a loop it proves never terminates and has
    # no observable side effects with a trap instruction instead of actually
    # looping. That's correct per the standard but wrong for recompiled
    # guest code: these loops are deliberate real-hardware idle states, not
    # unreachable code. -fno-finite-loops disables that specific assumption
    # for all of this generated PPC code.
    target_compile_options(BigBumpinPPC PRIVATE -Wno-unused-label -Wno-unused-variable -fno-finite-loops)

    add_executable(BigBumpinHost host/main.cpp host/kernel_stubs.cpp host/kernel_impl.cpp host/xdvdfs.cpp host/gpu_trace.cpp host/gpu_state.cpp host/gpu_draw_list.cpp host/shader_decode.cpp host/shader_translate.cpp host/game_overrides.cpp host/renderer_metal.mm)
    target_include_directories(BigBumpinHost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private/ppc")
    target_link_libraries(BigBumpinHost PRIVATE BigBumpinPPC XenonUtils fmt::fmt "-framework Metal" "-framework MetalKit" "-framework Cocoa" "-framework QuartzCore")
    set_source_files_properties(host/renderer_metal.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

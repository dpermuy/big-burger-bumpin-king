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

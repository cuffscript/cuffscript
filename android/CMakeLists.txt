# CMake build for Android Studio / Gradle (externalNativeBuild) or `cmake -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake`.
# Builds the same two executables as Android.mk. Nothing outside android/ is modified.
cmake_minimum_required(VERSION 3.10)
project(cuffscript CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(CUFF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

foreach(target cuffc cuffsh)
    if(target STREQUAL "cuffc")
        set(entry "${CUFF_ROOT}/main.cpp")
    else()
        set(entry "${CUFF_ROOT}/cli/main.cpp")
    endif()
    add_executable(${target} "${entry}")
    target_include_directories(${target} PRIVATE "${CUFF_ROOT}")
    target_compile_options(${target} PRIVATE -Wall -Wextra -fexceptions -frtti)
    target_compile_definitions(${target} PRIVATE NDEBUG)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
endforeach()

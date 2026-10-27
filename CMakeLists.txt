# Copyright (c) 2026 Jyrki Alakuijala
# SPDX-License-Identifier: MIT
# PikasekoHash-256. The Makefile is the primary build; this is for CMake users.
#   cmake -B build/cmake && cmake --build build/cmake && ctest --test-dir build/cmake
# Options: PIKASEKO256_SIMD (ON on x86-64 and AArch64 when the compiler accepts the flags).
# With PIKASEKO256_SIMD=OFF the library contains only the portable code and
# PIKASEKO256_PORTABLE_ONLY is defined for everything that links it.
cmake_minimum_required(VERSION 3.16)
project(pikaseko256 VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckCCompilerFlag)
include(CMakeDependentOption)
set(_pikaseko_simd OFF)
set(_pikaseko_arch other)
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
  set(_pikaseko_simd ON)  # MSVC accepts the intrinsics without flags
  set(_pikaseko_arch x86)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  check_c_compiler_flag("-mavx512f -mavx512bw -mvaes -mavx2 -maes" PIKASEKO256_HAVE_X86_FLAGS)
  if(PIKASEKO256_HAVE_X86_FLAGS)
    set(_pikaseko_simd ON)
    set(_pikaseko_arch x86)
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  check_c_compiler_flag("-march=armv8-a+crypto" PIKASEKO256_HAVE_ARM_FLAGS)
  if(PIKASEKO256_HAVE_ARM_FLAGS)
    set(_pikaseko_simd ON)
    set(_pikaseko_arch arm64)
  endif()
endif()
option(PIKASEKO256_SIMD "Build the SIMD implementations (x86-64: AVX-512, AVX2, AES-NI; AArch64: ARMv8 AES)" ${_pikaseko_simd})
option(PIKASEKO256_BUILD_TESTS "Build the tests, bench and tools" ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(PIKASEKO256_WARNINGS -Wall -Wextra)
endif()

add_library(pikaseko256 STATIC src/pikaseko256_portable.c src/pikaseko256_dispatch.c)
target_include_directories(pikaseko256
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(pikaseko256 PRIVATE ${PIKASEKO256_WARNINGS})
if(PIKASEKO256_SIMD AND _pikaseko_arch STREQUAL "x86")
  target_sources(pikaseko256 PRIVATE src/pikaseko256_avx512.c src/pikaseko256_avx2.c src/pikaseko256_aesni.c)
  if(NOT MSVC)  # MSVC compiles the intrinsics without per-file flags
    set(_avx_flags -mavx512f -mavx512bw -mvaes)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      # gcc: code hoisting and tree PRE make the step spill registers.
      list(APPEND _avx_flags -fno-code-hoisting -fno-tree-pre)
    endif()
    string(REPLACE ";" " " _avx_flags "${_avx_flags}")
    set_source_files_properties(src/pikaseko256_avx512.c PROPERTIES COMPILE_FLAGS "${_avx_flags}")
    set_source_files_properties(src/pikaseko256_avx2.c PROPERTIES COMPILE_FLAGS "-mavx2 -mvaes -maes")
    set_source_files_properties(src/pikaseko256_aesni.c PROPERTIES COMPILE_FLAGS "-maes")
  endif()
elseif(PIKASEKO256_SIMD AND _pikaseko_arch STREQUAL "arm64")
  target_sources(pikaseko256 PRIVATE src/pikaseko256_neon.c)
  set_source_files_properties(src/pikaseko256_neon.c PROPERTIES COMPILE_FLAGS "-march=armv8-a+crypto")
else()
  target_compile_definitions(pikaseko256 PUBLIC PIKASEKO256_PORTABLE_ONLY)
endif()

if(PIKASEKO256_BUILD_TESTS)
  enable_testing()
  function(pikaseko256_program name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pikaseko256)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(${name} PRIVATE ${PIKASEKO256_WARNINGS})
  endfunction()
  pikaseko256_program(pikaseko256_test test/test.c)
  pikaseko256_program(pikaseko256_vectors test/vectors.c)
  pikaseko256_program(verify_step_collisions tools/verify_step_collisions.c)
  pikaseko256_program(diffusion tools/diffusion.c)
  pikaseko256_program(battery tools/battery.c)
  target_compile_definitions(battery PRIVATE _POSIX_C_SOURCE=200809L)
  if(NOT MSVC)
    target_link_libraries(battery PRIVATE m)
  endif()
  pikaseko256_program(bench bench/bench.c)

  add_test(NAME test COMMAND pikaseko256_test ${CMAKE_CURRENT_SOURCE_DIR}/test/test_vectors.txt)
  add_test(NAME vectors COMMAND ${CMAKE_COMMAND}
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    -DPROGRAM=$<TARGET_FILE:pikaseko256_vectors>
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/test/test_vectors.txt
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test_vectors.out
    -P ${CMAKE_CURRENT_SOURCE_DIR}/test/compare_vectors.cmake)
  file(GLOB _collisions ${CMAKE_CURRENT_SOURCE_DIR}/analysis/data/step_collision_t*.txt)
  if(_collisions)
    add_test(NAME verify_step_collisions COMMAND verify_step_collisions ${_collisions})
  endif()
  set_tests_properties(test PROPERTIES TIMEOUT 600)
endif()

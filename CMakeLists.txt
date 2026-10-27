# Copyright (c) 2026 Jyrki Alakuijala
# SPDX-License-Identifier: MIT
# PikasekoHash-256. The Makefile is the primary build; this is for CMake users.
#   cmake -B build/cmake && cmake --build build/cmake && ctest --test-dir build/cmake
# Options: PIKASEKO256_AVX512 (ON on x86-64 when the compiler accepts the flags).
# With PIKASEKO256_AVX512=OFF the library contains only the portable code and
# PIKASEKO256_NO_AVX512 is defined for everything that links it.
cmake_minimum_required(VERSION 3.16)
project(pikaseko256 VERSION 0.0.1 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckCCompilerFlag)
include(CMakeDependentOption)
set(_pikaseko_x86_64 OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(CMAKE_REQUIRED_FLAGS "-mavx512f -mavx512bw -mvaes")
  check_c_compiler_flag("-mavx512f -mavx512bw -mvaes" PIKASEKO256_HAVE_AVX512_FLAGS)
  unset(CMAKE_REQUIRED_FLAGS)
  if(PIKASEKO256_HAVE_AVX512_FLAGS)
    set(_pikaseko_x86_64 ON)
  endif()
endif()
option(PIKASEKO256_AVX512 "Build the AVX-512 + VAES implementation" ${_pikaseko_x86_64})
option(PIKASEKO256_BUILD_TESTS "Build the tests, bench and tools" ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(PIKASEKO256_WARNINGS -Wall -Wextra)
endif()

add_library(pikaseko256 STATIC src/pikaseko256_portable.c)
target_include_directories(pikaseko256
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(pikaseko256 PRIVATE ${PIKASEKO256_WARNINGS})
if(PIKASEKO256_AVX512)
  target_sources(pikaseko256 PRIVATE src/pikaseko256_avx512.c)
  set(_avx_flags -mavx512f -mavx512bw -mvaes)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # gcc: code hoisting and tree PRE make the step spill registers.
    list(APPEND _avx_flags -fno-code-hoisting -fno-tree-pre)
  endif()
  string(REPLACE ";" " " _avx_flags "${_avx_flags}")
  set_source_files_properties(src/pikaseko256_avx512.c PROPERTIES COMPILE_FLAGS "${_avx_flags}")
else()
  target_compile_definitions(pikaseko256 PUBLIC PIKASEKO256_NO_AVX512)
endif()

if(PIKASEKO256_BUILD_TESTS)
  enable_testing()
  function(pikaseko256_program name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pikaseko256)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
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
  pikaseko256_program(bench_portable bench/bench_portable.c)
  if(PIKASEKO256_AVX512)
    pikaseko256_program(bench bench/bench.c)  # x86 only (rdtsc)
  endif()

  add_test(NAME test COMMAND pikaseko256_test)
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

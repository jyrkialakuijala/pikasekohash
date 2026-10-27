# Copyright (c) 2026 Jyrki Alakuijala
# SPDX-License-Identifier: MIT
# ctest helper: run PROGRAM (under EMULATOR, if set, for cross builds), write its output
# to OUTPUT, and compare it with EXPECTED.
execute_process(COMMAND ${EMULATOR} ${PROGRAM} OUTPUT_FILE ${OUTPUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${PROGRAM} exited with ${rc}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol ${OUTPUT} ${EXPECTED} RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
  message(FATAL_ERROR "${OUTPUT} differs from ${EXPECTED}")
endif()
message(STATUS "test_vectors.txt reproduced")

# Copyright (c) 2026 Jyrki Alakuijala
# SPDX-License-Identifier: MIT
# PikasekoHash-256. Targets: all (default), test, test-python, bench, bench-portable, battery, fuzz, clean.
#   make                     library, tests, tools: on x86-64 the AVX-512, AVX2 and AES-NI
#                            code, on AArch64 the ARMv8 AES code, and the portable code
#   make PORTABLE=1          portable code only (the default on other targets); the programs
#                            are built with -DPIKASEKO256_PORTABLE_ONLY
#   make test                tests; implementations the CPU lacks are skipped
#   make test RUN=qemu-s390x run the test programs under an emulator (cross builds)
# Variables: CC, AR, CFLAGS, LDFLAGS (e.g. LDFLAGS=-static), PORTABLE, RUN.
CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?=
RUN ?=
ISA = -mavx512f -mavx512bw -mvaes
# gcc: code hoisting and tree PRE make the step spill registers; switched off for the AVX-512 file.
AVXOPT = $(shell $(CC) -fno-code-hoisting -fno-tree-pre -E -x c /dev/null >/dev/null 2>&1 && echo -fno-code-hoisting -fno-tree-pre)
INC = -Iinclude -Isrc

# Target: x86-64, little-endian AArch64, or other (portable-only by default).
MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
ARCH := $(if $(filter x86_64-% amd64-%,$(MACHINE)),x86,$(if $(filter aarch64-% arm64-%,$(MACHINE)),arm64,other))
ifeq ($(ARCH),other)
PORTABLE ?= 1
else
PORTABLE ?= 0
endif

BENCH =
ifneq ($(PORTABLE),0)
OBJ = build/pikaseko256_portable.o build/pikaseko256_dispatch.o
DEFS = -DPIKASEKO256_PORTABLE_ONLY
else ifeq ($(ARCH),x86)
OBJ = build/pikaseko256_avx512.o build/pikaseko256_avx2.o build/pikaseko256_aesni.o build/pikaseko256_portable.o build/pikaseko256_dispatch.o
DEFS =
BENCH = build/bench
else ifeq ($(ARCH),arm64)
OBJ = build/pikaseko256_neon.o build/pikaseko256_portable.o build/pikaseko256_dispatch.o
DEFS =
else
$(error PORTABLE=0 needs an x86-64 or AArch64 target)
endif
BIN = build/test build/vectors $(BENCH) build/bench_portable build/battery build/diffusion build/verify_step_collisions
LIB = build/libpikaseko256.a

all: $(BIN)

build:
	mkdir -p build

# Rebuild everything when the build mode changes.
MODE = PORTABLE=$(PORTABLE) CC=$(CC) AR=$(AR) CFLAGS=$(CFLAGS) LDFLAGS=$(LDFLAGS)
build/mode: FORCE | build
	@echo '$(MODE)' | cmp -s - $@ || echo '$(MODE)' > $@

build/pikaseko256_avx512.o: src/pikaseko256_avx512.c src/pikaseko256_consts.h include/pikaseko256.h include/pikaseko256_analysis.h build/mode | build
	$(CC) $(CFLAGS) $(AVXOPT) $(ISA) $(INC) -c $< -o $@

SIMD_DEPS = src/pikaseko256_simd.h src/pikaseko256_impl.h src/pikaseko256_consts.h include/pikaseko256.h include/pikaseko256_analysis.h build/mode
build/pikaseko256_avx2.o: src/pikaseko256_avx2.c $(SIMD_DEPS) | build
	$(CC) $(CFLAGS) -mavx2 -mvaes -maes $(INC) -c $< -o $@

build/pikaseko256_aesni.o: src/pikaseko256_aesni.c $(SIMD_DEPS) | build
	$(CC) $(CFLAGS) -maes $(INC) -c $< -o $@

build/pikaseko256_neon.o: src/pikaseko256_neon.c $(SIMD_DEPS) | build
	$(CC) $(CFLAGS) -march=armv8-a+crypto $(INC) -c $< -o $@

build/pikaseko256_portable.o: src/pikaseko256_portable.c src/pikaseko256_consts.h include/pikaseko256.h include/pikaseko256_analysis.h build/mode | build
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/pikaseko256_dispatch.o: src/pikaseko256_dispatch.c src/pikaseko256_impl.h include/pikaseko256.h include/pikaseko256_analysis.h build/mode | build
	$(CC) $(CFLAGS) $(DEFS) $(INC) -c $< -o $@

$(LIB): $(OBJ)
	rm -f $@
	$(AR) rcs $@ $^

HDRS = include/pikaseko256.h include/pikaseko256_analysis.h build/mode
build/test: test/test.c $(HDRS) $(LIB)
	$(CC) $(CFLAGS) $(DEFS) $(INC) $< $(LIB) $(LDFLAGS) -o $@
build/vectors: test/vectors.c $(HDRS) $(LIB)
	$(CC) $(CFLAGS) $(DEFS) $(INC) $< $(LIB) $(LDFLAGS) -o $@
build/bench: bench/bench.c $(HDRS) $(LIB)
	$(CC) $(CFLAGS) $(DEFS) $(INC) $< $(LIB) $(LDFLAGS) -o $@
build/bench_portable: bench/bench_portable.c $(HDRS) $(LIB)
	$(CC) $(CFLAGS) $(DEFS) $(INC) $< $(LIB) $(LDFLAGS) -o $@
build/battery: tools/battery.c $(HDRS) $(LIB)
	$(CC) $(CFLAGS) $(DEFS) -D_POSIX_C_SOURCE=200809L $(INC) $< $(LIB) $(LDFLAGS) -lm -o $@
build/diffusion: tools/diffusion.c $(HDRS) $(LIB)
	$(CC) $(CFLAGS) $(DEFS) $(INC) $< $(LIB) $(LDFLAGS) -o $@
build/verify_step_collisions: tools/verify_step_collisions.c $(HDRS) $(LIB)
	$(CC) $(CFLAGS) $(DEFS) $(INC) $< $(LIB) $(LDFLAGS) -o $@

test: build/test build/vectors build/verify_step_collisions
	$(RUN) ./build/test
	$(RUN) ./build/vectors | diff - test/test_vectors.txt && echo "test_vectors.txt reproduced"
	$(RUN) ./build/verify_step_collisions analysis/data/step_collision_t*.txt

ifneq ($(BENCH),)
bench: build/bench
	./build/bench
	./build/bench --keyed
else
bench: bench-portable
endif

bench-portable: build/bench_portable
	$(RUN) ./build/bench_portable
	$(RUN) ./build/bench_portable --keyed

battery: build/battery
	$(RUN) ./build/battery

# The Python reference (ref/pikaseko256.py, from SPEC.md) against the test vectors.
test-python:
	python3 ref/pikaseko256.py --checksum

# Fuzzing (clang with libFuzzer): every implementation against the portable one, one-shot
# and streaming, under ASan and UBSan. make fuzz FUZZTIME=60
FUZZ_CC ?= clang
FUZZTIME ?= 60
FZ = -O1 -g -std=c11 -fsanitize=address,undefined -fno-sanitize-recover=all $(INC)
FUZZ_SRC = src/pikaseko256_portable.c src/pikaseko256_dispatch.c
ifeq ($(PORTABLE)-$(ARCH),0-x86)
FUZZ_SIMD = $(FUZZ_CC) $(FZ) -mavx512f -mavx512bw -mvaes -c src/pikaseko256_avx512.c -o build/fuzzobj/avx512.o && \
            $(FUZZ_CC) $(FZ) -mavx2 -mvaes -maes -c src/pikaseko256_avx2.c -o build/fuzzobj/avx2.o && \
            $(FUZZ_CC) $(FZ) -maes -c src/pikaseko256_aesni.c -o build/fuzzobj/aesni.o
else ifeq ($(PORTABLE)-$(ARCH),0-arm64)
FUZZ_SIMD = $(FUZZ_CC) $(FZ) -march=armv8-a+crypto -c src/pikaseko256_neon.c -o build/fuzzobj/neon.o
else
FUZZ_SIMD = true
endif
build/fuzz: test/fuzz.c $(wildcard src/*.c src/*.h include/*.h) build/mode | build
	rm -rf build/fuzzobj && mkdir -p build/fuzzobj
	$(FUZZ_SIMD)
	$(FUZZ_CC) $(FZ) $(DEFS) -fsanitize=fuzzer $< $(FUZZ_SRC) $$(ls build/fuzzobj/*.o 2>/dev/null) -o $@
fuzz: build/fuzz
	mkdir -p build/fuzz-corpus
	./build/fuzz -max_total_time=$(FUZZTIME) -max_len=5000 build/fuzz-corpus

clean:
	rm -rf build

.PHONY: all test test-python bench bench-portable battery fuzz clean FORCE

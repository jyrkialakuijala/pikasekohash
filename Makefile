# Copyright (c) 2026 Jyrki Alakuijala
# SPDX-License-Identifier: MIT
# PikasekoHash-256. Targets: all (default), test, bench, bench-portable, battery, clean.
#   make                     AVX-512 + portable library, tests, tools (x86-64)
#   make PORTABLE=1          portable code only (default on non-x86-64 targets); the
#                            library then has no AVX-512 functions and the programs are
#                            built with -DPIKASEKO256_NO_AVX512
#   make test                tests; on a CPU without AVX-512 + VAES only the portable tests run
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

# Portable-only by default when the compiler does not target x86-64.
TARGET_ARCH_X86_64 := $(shell $(CC) -dumpmachine 2>/dev/null | grep -q -E '^(x86_64|amd64)' && echo 1)
ifeq ($(TARGET_ARCH_X86_64),1)
PORTABLE ?= 0
else
PORTABLE ?= 1
endif

ifeq ($(PORTABLE),0)
OBJ = build/pikaseko256_avx512.o build/pikaseko256_avx2.o build/pikaseko256_aesni.o build/pikaseko256_portable.o build/pikaseko256_dispatch.o
DEFS =
BENCH = build/bench
else
OBJ = build/pikaseko256_portable.o build/pikaseko256_dispatch.o
DEFS = -DPIKASEKO256_NO_AVX512
BENCH =
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

ifeq ($(PORTABLE),0)
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

clean:
	rm -rf build

.PHONY: all test bench bench-portable battery clean FORCE

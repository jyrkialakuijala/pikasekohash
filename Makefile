CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
ISA = -mavx512f -mavx512bw -mvaes
# gcc: code hoisting and tree PRE make the step spill registers; switched off for the AVX-512 file.
AVXOPT = $(shell $(CC) -fno-code-hoisting -fno-tree-pre -E -x c /dev/null >/dev/null 2>&1 && echo -fno-code-hoisting -fno-tree-pre)
INC = -Iinclude -Isrc
OBJ = build/pikaseko256_avx512.o build/pikaseko256_portable.o
BIN = build/test build/vectors build/bench build/battery build/diffusion build/verify_step_collisions

all: $(BIN)

build:
	mkdir -p build

build/pikaseko256_avx512.o: src/pikaseko256_avx512.c src/pikaseko256_consts.h include/pikaseko256.h | build
	$(CC) $(CFLAGS) $(AVXOPT) $(ISA) $(INC) -c $< -o $@

build/pikaseko256_portable.o: src/pikaseko256_portable.c src/pikaseko256_consts.h include/pikaseko256.h | build
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/libpikaseko256.a: $(OBJ)
	ar rcs $@ $^

build/test: test/test.c build/libpikaseko256.a
	$(CC) $(CFLAGS) $(INC) $< build/libpikaseko256.a -o $@
build/vectors: test/vectors.c build/libpikaseko256.a
	$(CC) $(CFLAGS) $(INC) $< build/libpikaseko256.a -o $@
build/bench: bench/bench.c build/libpikaseko256.a
	$(CC) $(CFLAGS) $(INC) $< build/libpikaseko256.a -o $@
build/battery: tools/battery.c build/libpikaseko256.a
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(INC) $< build/libpikaseko256.a -lm -o $@
build/diffusion: tools/diffusion.c build/libpikaseko256.a
	$(CC) $(CFLAGS) $(INC) $< build/libpikaseko256.a -o $@
build/verify_step_collisions: tools/verify_step_collisions.c build/libpikaseko256.a
	$(CC) $(CFLAGS) $(INC) $< build/libpikaseko256.a -o $@

test: build/test build/vectors build/verify_step_collisions
	./build/test
	./build/vectors | diff - test/test_vectors.txt && echo "test_vectors.txt reproduced"
	./build/verify_step_collisions analysis/data/step_collision_t*.txt

bench: build/bench
	./build/bench
	./build/bench --keyed

battery: build/battery
	./build/battery

clean:
	rm -rf build

.PHONY: all test bench battery clean

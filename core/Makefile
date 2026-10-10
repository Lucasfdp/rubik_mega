# rubik_mega/core -- Makefile
#
# make test   builds and runs every tests/*.c as its own binary (plain
#             cc, native libc -- same split rubik itself makes between
#             `make test` and `make bonus`).
# make wasm   builds the WebAssembly module with Zig's bundled clang
#             (see DECISIONS.md, "WASM toolchain: Zig, not Emscripten" --
#             no system install, no network dependency beyond PyPI,
#             already fetched into toolchain/ the first time this ran).
# make clean  removes build output.

CC          := cc
CFLAGS      := -Wall -Wextra -Werror -std=c11 -Iinclude
BUILD       := build

# Native test build: everything except platform/ (that's WASM-only --
# the native binary uses libc's real malloc/free).
NATIVE_SRC  := $(shell find src -name '*.c' -not -path 'src/platform/*')
TEST_SRCS   := $(wildcard tests/*.c)
TEST_BINS   := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRCS))

# WASM build: the same core sources plus the freestanding allocator
# (src/platform/wasm_alloc.c) that stands in for libc's.
ZIG         := python3 -m ziglang
WASM_SRC    := $(NATIVE_SRC) src/platform/wasm_alloc.c
WASM_OUT    := $(BUILD)/rubik_mega.wasm
WASM_FLAGS  := --target=wasm32-freestanding -nostdlib \
               -Wl,--no-entry -Wl,--export-dynamic \
               -Wall -Wextra -std=c11 -Iinclude -O2

.PHONY: test wasm clean ensure-zig

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done

$(BUILD)/%: tests/%.c $(NATIVE_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(NATIVE_SRC) $< -o $@

wasm: ensure-zig $(WASM_OUT)

# Installs Zig (pip, user-local, no root) the first time this runs, same
# spirit as rubik's own `make bonus` auto-fetching raylib -- so `make
# wasm` works from a clean machine with no manual setup step. See
# DECISIONS.md, "WASM toolchain: Zig, not Emscripten".
ensure-zig:
	@$(ZIG) version >/dev/null 2>&1 || pip install --user --quiet ziglang

$(WASM_OUT): $(WASM_SRC)
	@mkdir -p $(BUILD)
	$(ZIG) cc $(WASM_FLAGS) -o $@ $(WASM_SRC)

clean:
	rm -rf $(BUILD)

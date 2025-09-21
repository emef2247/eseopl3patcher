CC       = gcc
CC_WIN   = x86_64-w64-mingw32-gcc
CFLAGS   = -O2 -Wall -Iinclude -Isrc/opl3 -Isrc/vgm -Isrc/opll

BUILD_DIR = build

TEST_DETUNE ?= 0
TEST_EXTRA_ARGS ?= --convert-ym2413

# Source files
SRCS = $(wildcard src/opl3/*.c) \
       $(wildcard src/vgm/*.c)  \
       $(wildcard src/opll/*.c) \
       $(wildcard src/*.c)

# Targets
TARGET      = $(BUILD_DIR)/eseopl3patcher
TARGET_WIN  = $(BUILD_DIR)/eseopl3patcher.exe

# Default build
all: $(TARGET)

# Ensure build directory exists
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Linux build
$(TARGET): $(SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Windows (cross) build
win: $(SRCS) | $(BUILD_DIR)
	$(CC_WIN) $(CFLAGS) -o $(TARGET_WIN) $^ -lm

# Release: move built binaries into release_temp/
release: $(TARGET)
	@mkdir -p release_temp
	@if [ -f $(TARGET) ]; then cp -f $(TARGET) release_temp/; fi
	@if [ -f $(TARGET_WIN) ]; then cp -f $(TARGET_WIN) release_temp/; fi
	@echo "Copied binaries to release_temp/"

# Clean
clean:
	rm -rf $(BUILD_DIR) release_temp

# ---- Equivalence / Baseline Tests ----
# scripts/test_vgm_equiv.sh は $(TARGET) を引数に取る想定
.PHONY: test-equivalence baseline-update baseline-init

test-equivalence: $(TARGET)
	@DETUNE=$(TEST_DETUNE) EXTRA_ARGS="$(TEST_EXTRA_ARGS)" scripts/test_vgm_equiv.sh $(TARGET)

baseline-update: $(TARGET)
	@DETUNE=$(TEST_DETUNE) EXTRA_ARGS="$(TEST_EXTRA_ARGS)" scripts/test_vgm_equiv.sh $(TARGET) --update-baseline

baseline-init: $(TARGET)
	@DETUNE=$(TEST_DETUNE) EXTRA_ARGS="$(TEST_EXTRA_ARGS)" scripts/test_vgm_equiv.sh $(TARGET) --init-baseline


.PHONY: all win release clean
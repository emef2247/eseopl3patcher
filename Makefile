CC       = gcc
CC_WIN   = x86_64-w64-mingw32-gcc

# 追加: ユーザー定義マクロ/フラグ
CPPFLAGS ?=
USER_DEFINES ?=
CPPFLAGS += $(USER_DEFINES)

CFLAGS   = -O2 -Wall -Iinclude -Isrc/opl3 -Isrc/vgm -Isrc/opll

BUILD_DIR = build

TEST_DETUNE ?= 0
TEST_EXTRA_ARGS ?= --convert-ym2413

SRCS = $(wildcard src/opl3/*.c) \
       $(wildcard src/vgm/*.c)  \
       $(wildcard src/opll/*.c) \
       $(wildcard src/*.c)

TARGET      = $(BUILD_DIR)/eseopl3patcher
TARGET_WIN  = $(BUILD_DIR)/eseopl3patcher.exe

all: $(TARGET)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(TARGET): $(SRCS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ -lm

win: $(SRCS) | $(BUILD_DIR)
	$(CC_WIN) $(CPPFLAGS) $(CFLAGS) -o $(TARGET_WIN) $^ -lm

release: $(TARGET)
	@mkdir -p release_temp
	@if [ -f $(TARGET) ]; then cp -f $(TARGET) release_temp/; fi
	@if [ -f $(TARGET_WIN) ]; then cp -f $(TARGET_WIN) release_temp/; fi
	@echo "Copied binaries to release_temp/"

clean:
	rm -rf $(BUILD_DIR) release_temp

.PHONY: test-equivalence baseline-update baseline-init

test-equivalence: $(TARGET)
	@DETUNE=$(TEST_DETUNE) EXTRA_ARGS="$(TEST_EXTRA_ARGS)" scripts/test_vgm_equiv.sh $(TARGET)

baseline-update: $(TARGET)
	@DETUNE=$(TEST_DETUNE) EXTRA_ARGS="$(TEST_EXTRA_ARGS)" scripts/test_vgm_equiv.sh $(TARGET) --update-baseline

baseline-init: $(TARGET)
	@DETUNE=$(TEST_DETUNE) EXTRA_ARGS="$(TEST_EXTRA_ARGS)" scripts/test_vgm_equiv.sh $(TARGET) --init-baseline

# 便利ターゲット
.PHONY: tl0 nogate tl0-nogate print-flags
tl0:
	@$(MAKE) clean
	@$(MAKE) all USER_DEFINES="-DOPLL_DEBUG_FORCE_CAR_TL_ZERO=1"

nogate:
	@$(MAKE) clean
	@$(MAKE) all USER_DEFINES="-DOPLL_MIN_GATE_SAMPLES=0"

tl0-nogate:
	@$(MAKE) clean
	@$(MAKE) all USER_DEFINES="-DOPLL_DEBUG_FORCE_CAR_TL_ZERO=1 -DOPLL_MIN_GATE_SAMPLES=0"

print-flags:
	@echo "CC             = $(CC)"
	@echo "CC_WIN         = $(CC_WIN)"
	@echo "CPPFLAGS       = $(CPPFLAGS)"
	@echo "CFLAGS         = $(CFLAGS)"
	@echo "USER_DEFINES   = $(USER_DEFINES)"
	@echo "TEST_DETUNE    = $(TEST_DETUNE)"
	@echo "TEST_EXTRA_ARGS= $(TEST_EXTRA_ARGS)"

.PHONY: all win release clean

SPECTRO_SCRIPT = scripts/wav_spectrogram.py

.PHONY: vgm2wav spectrogram
vgm2wav: $(TARGET)
	# VGM -> WAV (vgmplay のオプションは環境により異なる場合あり)
	# 例: VGMPlay -o out.wav -l 1 test.vgm
	VGMPlay -o out.wav -l 1 test.vgm

spectrogram:
	python3 $(SPECTRO_SCRIPT) out.wav --out out_spec.png --max_freq 8000 --n_fft 4096 --hop 512
	@echo "Generated: out_spec.png"

# Gate sweep one-shot
# Usage:
#   make gate-sweep IN=tests/equiv/inputs/ym2413_scale_chromatic.vgm BASE=tests/equiv/inputs/ym2413_scale_chromatic.wav LABEL=scale
.PHONY: gate-sweep
gate-sweep: $(TARGET)
	@bash scripts/auto_analyze_vgm.sh -i "$(IN)" -b "$(BASE)" -l "$(LABEL)" --adv-onset --adv-env

# Quick: choose gates inline
#   make gate-sweep-gates IN=... GATES="8192 10240 12288"
.PHONY: gate-sweep-gates
gate-sweep-gates: $(TARGET)
	@bash scripts/auto_analyze_vgm.sh -i "$(IN)" -b "$(BASE)" -l "$(LABEL)" -g "$(GATES)" --adv-onset --adv-env
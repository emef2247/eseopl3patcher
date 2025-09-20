# =========================================
# Multi-variant build Makefile for eseopl3patcher
# Variants:
#   m1s0t0 : RATE_MAP_MODE=1, SHAPEFIX=off,  MOD_TL_ADJ=off
#   m1s1t0 : RATE_MAP_MODE=1, SHAPEFIX=on,   MOD_TL_ADJ=off
#   m2s0t0 : RATE_MAP_MODE=2, SHAPEFIX=off,  MOD_TL_ADJ=off
#   m2s1t0 : RATE_MAP_MODE=2, SHAPEFIX=on,   MOD_TL_ADJ=off
#
# (Adjust TL_ADJ=on の variant が欲しければ行を追加してください)
#
# Build:
#   make            -> build all variants
#   make clean
#
# Override loader / converter_init() はソース内に既に実装済み前提
# =========================================

# C compiler / flags
CC      ?= gcc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -pedantic -DNDEBUG
CFLAGS += -D_POSIX_C_SOURCE=200809L

# 追加でデバッグしたい場合: make DEBUG=1
ifeq ($(DEBUG),1)
  CFLAGS := -O0 -g -std=c99 -Wall -Wextra -pedantic -DDEBUG
endif

# 依存ライブラリやリンクオプション（必要に応じて追加）
LDFLAGS ?=
LDLIBS += m

# ソース探索
SRC_DIRS := src src/opl3 src/opll src/vgm
SRCS     := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
HDRS     := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.h))

# 出力ディレクトリ（任意）
BIN_DIR  := bin
OBJ_DIR  := build

# 生成したいバリアント名
VARIANTS := m1s0t0 m1s1t0 m2s0t0 m2s1t0
BIN_PREFIX := eseopl3patcher_

# -----------------------------------------
# Variant-specific macro definitions
# （必要に応じて OPLL_FORCE_MIN_ATTACK_RATE 等も加える）
# -----------------------------------------
VARIANT_DEFS_m1s0t0 = -DOPLL_RATE_MAP_MODE=1 -DOPLL_ENABLE_ENVELOPE_SHAPE_FIX=0 -DOPLL_ENABLE_MOD_TL_ADJ=0
VARIANT_DEFS_m1s1t0 = -DOPLL_RATE_MAP_MODE=1 -DOPLL_ENABLE_ENVELOPE_SHAPE_FIX=1 -DOPLL_ENABLE_MOD_TL_ADJ=0
VARIANT_DEFS_m2s0t0 = -DOPLL_RATE_MAP_MODE=2 -DOPLL_ENABLE_ENVELOPE_SHAPE_FIX=0 -DOPLL_ENABLE_MOD_TL_ADJ=0
VARIANT_DEFS_m2s1t0 = -DOPLL_RATE_MAP_MODE=2 -DOPLL_ENABLE_ENVELOPE_SHAPE_FIX=1 -DOPLL_ENABLE_MOD_TL_ADJ=0

# ここで追加カスタマイズ例:
# VARIANT_DEFS_m1s1t1 = -DOPLL_RATE_MAP_MODE=1 -DOPLL_ENABLE_ENVELOPE_SHAPE_FIX=1 -DOPLL_ENABLE_MOD_TL_ADJ=1

# 共通で常に有効にしておきたいマクロ（後方互換など）
COMMON_DEFS := -DOPLL_ENABLE_RATE_MAP=1

# -----------------------------------------
# Object build (non-variant). Variant別に再コンパイルしたい
# 場合はパターンルール内で直接 $(SRCS) をコンパイルするシンプル方式にする
# （ビルド速度が問題なら後で “共通オブジェクト + ラッパ” 戦略へ改善）
# -----------------------------------------

# Default target
.PHONY: all
all: prep $(VARIANTS:%=$(BIN_DIR)/$(BIN_PREFIX)%)

.PHONY: prep
prep:
	@mkdir -p $(BIN_DIR)

# Pattern rule for each variant
# $* は “m1s0t0”等
$(BIN_DIR)/$(BIN_PREFIX)%: $(SRCS) $(HDRS) | prep
	@echo "[BUILD] Variant $*"
	$(CC) $(CFLAGS) $(COMMON_DEFS) $(VARIANT_DEFS_$*) $(SRCS) -o $@ $(LDFLAGS) -l$(LDLIBS)
	@echo "[OK] $@"

# Convenience target to list macros used
.PHONY: show
show:
	@for v in $(VARIANTS); do \
	  eval defs=\$$(echo VARIANT_DEFS_$${v}); \
	  echo "$$v => $$defs"; \
	done

# Clean
.PHONY: clean
clean:
	@rm -rf $(BIN_DIR)
	@echo "[CLEAN] Removed $(BIN_DIR)"

# Distclean (もし将来中間 build/ があれば追加)
.PHONY: distclean
distclean: clean
	@rm -rf $(OBJ_DIR)
	@echo "[DISTCLEAN] Removed $(OBJ_DIR)"

# Individual variant explicit targets (optional syntactic sugar)
$(VARIANTS): %: $(BIN_DIR)/$(BIN_PREFIX)%
	@true

# Install (任意)
.PHONY: install
install: all
	@echo "(Install step not implemented; copy binaries from $(BIN_DIR)/ manually)"
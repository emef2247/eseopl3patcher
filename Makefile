# eseopl3patcher Makefile (YMFM optional integration)
# Usage:
#   make                  # YMFMなしでビルド（常時通る: トレースはNO-OP）
#   make USE_YMFM=1 -j    # YMFM有効（scripts/setup_ymfm.sh 実行後）
#   ESEOPL3_YMFM_TRACE=1 ./build/bin/eseopl3patcher <args>  # YMFMトレース出力（USE_YMFM=1時）

# Compilers
CC       := gcc
CXX      := g++

# Feature toggles
USE_YMFM ?= 0   # 0: off (default, builds always) / 1: on (requires third_party/ymfm)

# User-provided preprocessor defines (e.g., USER_DEFINES="-DENABLE_FOO=1")
USER_DEFINES ?=

# Common flags
CPPFLAGS  := $(USER_DEFINES)
CFLAGS    := -O2 -Wall -Wextra -std=c11   -MMD -MP
CXXFLAGS  := -O2 -Wall -Wextra -std=c++17 -MMD -MP
LDFLAGS   :=
LDLIBS    := -lm   # <= add libm for floor/log10/log2


# Include paths
INC_DIRS := include src/opl3 src/opll src/vgm
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

# YMFM (optional)
ifeq ($(USE_YMFM),1)
  CPPFLAGS  += -DUSE_YMFM=1
  CXXFLAGS  += -Ithird_party/ymfm/src
  # C側にも ymfm_bridge のヘッダを見せるため共通インクルードを追加
  INC_FLAGS += -Iinclude
else
  CPPFLAGS  += -DUSE_YMFM=0
endif

# Directories
BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

# Target
TARGET := $(BIN_DIR)/eseopl3patcher

# Sources
SRCS_C   := $(wildcard src/*.c) \
            $(wildcard src/opl3/*.c) \
            $(wildcard src/opll/*.c) \
            $(wildcard src/vgm/*.c)

SRCS_CPP :=
ifeq ($(USE_YMFM),1)
  # YMFM C bridge
  SRCS_CPP += src/ymfm_bridge/ymfm_c_api.cpp
endif

# Object files (preserve directory structure under OBJ_DIR)
OBJS_C    := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS_C))
OBJS_CPP  := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS_CPP))

# Dependency files
DEPS := $(OBJS_C:.o=.d) $(OBJS_CPP:.o=.d)

# Default target
.PHONY: all
all: $(TARGET)

# Create directories
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p "$@"

# Pattern rules for C/C++
# Ensure object subdirectories exist
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	$(CC)  $(CPPFLAGS) $(CFLAGS)   $(INC_FLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INC_FLAGS) -c $< -o $@

# Link
# If YMFM is enabled, link with C++ linker to pull in libstdc++ automatically.
$(TARGET): $(BIN_DIR) $(OBJS_C) $(OBJS_CPP)
ifeq ($(USE_YMFM),1)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS_C) $(OBJS_CPP) $(LDLIBS)
else
	$(CC)  $(LDFLAGS) -o $@ $(OBJS_C)            $(LDLIBS)
endif
	@echo "[OK] Built $@"

# Convenience: fetch YMFM
.PHONY: setup-ymfm
setup-ymfm:
	@bash scripts/setup_ymfm.sh

# Clean
.PHONY: clean
clean:
	@rm -rf "$(BUILD_DIR)"

# Print variables (debug)
.PHONY: vars
vars:
	@echo "USE_YMFM=$(USE_YMFM)"
	@echo "CC=$(CC)"
	@echo "CXX=$(CXX)"
	@echo "CPPFLAGS=$(CPPFLAGS)"
	@echo "CFLAGS=$(CFLAGS)"
	@echo "CXXFLAGS=$(CXXFLAGS)"
	@echo "INC_FLAGS=$(INC_FLAGS)"
	@echo "SRCS_C count=$(words $(SRCS_C))"
	@echo "SRCS_CPP count=$(words $(SRCS_CPP))"
	@echo "TARGET=$(TARGET)"

# Include auto-generated dependencies
# (only if files exist, to avoid first-run errors)
-include $(DEPS)
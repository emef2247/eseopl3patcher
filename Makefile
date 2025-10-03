# eseopl3patcher Makefile (YMFM optional integration)
# Usage:
#   make
#   make USE_YMFM=1 -j
#   ESEOPL3_YMFM_TRACE=1 ./build/bin/eseopl3patcher <args>

CC       := gcc
CXX      := g++

USE_YMFM ?= 0
USER_DEFINES ?=

CPPFLAGS  := $(USER_DEFINES)
CPPFLAGS  += -D_POSIX_C_SOURCE=200809L
CFLAGS    := -O2 -Wall -Wextra -std=c11   -MMD -MP
CXXFLAGS  := -O2 -Wall -Wextra -std=c++17 -MMD -MP
LDFLAGS   :=
LDLIBS    := -lm

INC_DIRS  := include src/opl3 src/opll src/vgm
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

ifeq ($(USE_YMFM),1)
  CPPFLAGS  += -DUSE_YMFM=1
  CXXFLAGS  += -Ithird_party/ymfm/src
  INC_FLAGS += -Iinclude
else
  CPPFLAGS  += -DUSE_YMFM=0
endif

BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

TARGET := $(BIN_DIR)/eseopl3patcher

# Sources (raw) then dedup with $(sort)
SRCS_C_RAW := $(wildcard src/*.c) \
              $(wildcard src/opl3/*.c) \
              $(wildcard src/opll/*.c) \
              $(wildcard src/vgm/*.c)

SRCS_C   := $(sort $(SRCS_C_RAW))
SRCS_CPP :=

ifeq ($(USE_YMFM),1)
  # YMFM C bridge
  SRCS_CPP += src/ymfm_bridge/ymfm_c_api.cpp
  # YMFM core sources (ymfm_fm.cpp は該当リビジョンに無いため含めない)
  SRCS_CPP += third_party/ymfm/src/ymfm_opl.cpp \
              third_party/ymfm/src/ymfm_pcm.cpp \
              third_party/ymfm/src/ymfm_ssg.cpp \
              third_party/ymfm/src/ymfm_misc.cpp \
              third_party/ymfm/src/ymfm_adpcm.cpp
endif

# Objects (dedup just in case)
OBJS_C    := $(sort $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS_C)))
OBJS_CPP  := $(sort $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS_CPP)))

DEPS := $(OBJS_C:.o=.d) $(OBJS_CPP:.o=.d)

.PHONY: all
all: $(TARGET)

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p "$@"

# Generic C rule
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	$(CC)  $(CPPFLAGS) $(CFLAGS)   $(INC_FLAGS) -c $< -o $@

# Generic C++ rule
$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INC_FLAGS) -c $< -o $@

# YMFM third_party explicit rule (robust)
$(OBJ_DIR)/third_party/ymfm/src/%.o: third_party/ymfm/src/%.cpp | $(OBJ_DIR)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Ithird_party/ymfm/src $(INC_FLAGS) -c $< -o $@

$(TARGET): $(BIN_DIR) $(OBJS_C) $(OBJS_CPP)
ifeq ($(USE_YMFM),1)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS_C) $(OBJS_CPP) $(LDLIBS)
else
	$(CC)  $(LDFLAGS) -o $@ $(OBJS_C)            $(LDLIBS)
endif
	@echo "[OK] Built $@"

.PHONY: setup-ymfm
setup-ymfm:
	@bash scripts/setup_ymfm.sh

.PHONY: clean
clean:
	@rm -rf "$(BUILD_DIR)"

.PHONY: vars
vars:
	@echo "USE_YMFM=$(USE_YMFM)"
	@echo "SRCS_C (n=$(words $(SRCS_C)))"
	@printf "%s\n" $(SRCS_C)
	@echo "SRCS_CPP (n=$(words $(SRCS_CPP)))"
	@printf "%s\n" $(SRCS_CPP)
	@echo "OBJS_C (n=$(words $(OBJS_C)))"
	@printf "%s\n" $(OBJS_C)
	@echo "OBJS_CPP (n=$(words $(OBJS_CPP)))"
	@printf "%s\n" $(OBJS_CPP)

-include $(DEPS)
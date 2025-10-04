CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
            -DESEOPL3_OPLL_TRACE \
            -DESEOPL3_OPLL_VCD \
            -I../../third_party/ymfm_instrumented/src \
            -I../../third_party/ymfm_instrumented/examples/vgmrender

# YMFM core sources
YMFM_SRC_DIR := ../../third_party/ymfm_instrumented/src
YMFM_SRCS := \
  ymfm_adpcm.cpp \
  ymfm_misc.cpp \
  ymfm_opl.cpp \
  ymfm_opm.cpp \
  ymfm_opn.cpp \
  ymfm_opq.cpp \
  ymfm_opz.cpp \
  ymfm_pcm.cpp \
  ymfm_ssg.cpp

# Example helper (inflate / util) —— grepで特定したらここに追加
# 例: em_inflate.cpp や miniz.c 等
EXAMPLE_HELPERS := \
../../third_party/ymfm_instrumented/examples/vgmrender/em_inflate.cpp

VGMT_RENDER := ../../third_party/ymfm_instrumented/examples/vgmrender/vgmrender.cpp

SRC := $(addprefix $(YMFM_SRC_DIR)/,$(YMFM_SRCS)) $(EXAMPLE_HELPERS) $(VGMT_RENDER)
OBJ := $(SRC:.cpp=.o)
BIN := vgmrender_opll_trace

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
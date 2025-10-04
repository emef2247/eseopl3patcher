CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I../../third_party/ymfm_upstream/src
LDFLAGS  := 
SRC := main.cpp vgm_parser.cpp csv_writer.cpp opll_probe.cpp \
       ../../third_party/ymfm_upstream/src/ymfm_opl.cpp ../../third_party/ymfm_upstream/src/ymfm_adpcm.cpp  ../../third_party/ymfm_upstream/src/ymfm_pcm.cpp
OBJ := $(SRC:.cpp=.o)
BIN := ym2413_vgm_trace

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
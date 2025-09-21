CC=gcc
CFLAGS=-O2 -Wall -Iinclude -Isrc/opl3 -Isrc/vgm
SRCS=$(wildcard src/opl3/*.c) $(wildcard src/vgm/*.c)  $(wildcard src/opll/*.c) $(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)
TARGET=eseopl3patcher

# Windows cross-compile settings
CC_WIN=x86_64-w64-mingw32-gcc
TARGET_WIN=eseopl3patcher.exe

# Default: build for Linux
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Windows build
win: $(SRCS)
	$(CC_WIN) $(CFLAGS) -o $(TARGET_WIN) $^ -lm

# Clean build artifacts
clean:
	rm -f $(TARGET) $(TARGET_WIN) src/opl3/*.o src/vgm/*.o src/*.o

release: eseopl3patcher eseopl3patcher.exe
	@mkdir -p release_temp
	@if [ -f eseopl3patcher ]; then mv -f eseopl3patcher release_temp/; fi
	@if [ -f eseopl3patcher.exe ]; then mv -f eseopl3patcher.exe release_temp/; fi
	@echo "Moved eseopl3patcher(.exe) to release_temp/"

.PHONY: all release win clean
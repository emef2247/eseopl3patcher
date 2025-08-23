CC=gcc
CFLAGS=-O2 -Wall -Iinclude
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)
TARGET=eseopl3patcher

# Windows cross-compile settings
CC_WIN=x86_64-w64-mingw32-gcc
TARGET_WIN=eseopl3patcher.exe

# Default: build for Linux
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

# Windows build
win: $(SRCS)
	$(CC_WIN) $(CFLAGS) -o $(TARGET_WIN) $^

# Clean build artifacts
clean:
	rm -f $(TARGET) $(TARGET_WIN) src/*.o

.PHONY: all win clean
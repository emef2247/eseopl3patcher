# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2 -I./src

# Object files
OBJS = src/main.o src/vgm_helpers.o src/opl3_convert.o src/gd3_util.o src/vgm_header.o

# Target
TARGET = eseopl3patcher

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Pattern rules
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean

clean:
	rm -f $(OBJS) $(TARGET)
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
TARGET = chip8

all: $(TARGET)

$(TARGET): chip8.c
	$(CC) $(CFLAGS) -o $(TARGET) chip8.c

clean:
	rm -f $(TARGET)

.PHONY: all clean

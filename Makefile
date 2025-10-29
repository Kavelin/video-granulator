# if you don't have make
# use 
# $ gcc main.c buffer.c window.c -o build/vidgrains $(pkg-config --cflags --libs sdl2 libavformat libavcodec libswscale libavutil)
# instead :3

TARGET := vidgrains
BUILD_DIR := build
SOURCES := main.c buffer.c window.c
OBJECTS := $(addprefix $(BUILD_DIR)/, $(SOURCES:.c=.o))

PKG_LIBS := $(shell pkg-config --libs sdl2 libavformat libavcodec libswscale libavutil)
PKG_CFLAGS := $(shell pkg-config --cflags sdl2 libavformat libavcodec libswscale libavutil)
CC := gcc
CFLAGS := -Wall -Wextra -g $(PKG_CFLAGS)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJECTS) -o $(BUILD_DIR)/$(TARGET) $(PKG_LIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

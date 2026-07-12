# if you don't have make
# use 
# $ gcc main.c buffer.c window.c -o build/vidgrains $(pkg-config --cflags --libs sdl2 libavformat libavcodec libswscale libavutil)
# instead :3

TARGET := vidgrains
BUILD_DIR := build
SOURCES := main.c buffer.c window.c
OBJECTS := $(addprefix $(BUILD_DIR)/, $(SOURCES:.c=.o))

WASM_BUILD_DIR := web/build
WASM_TARGET := $(WASM_BUILD_DIR)/vidgrains.html

PKG_LIBS := $(shell pkg-config --libs sdl2 libavformat libavcodec libswscale libavutil)
PKG_CFLAGS := $(shell pkg-config --cflags sdl2 libavformat libavcodec libswscale libavutil)
CC := gcc
CFLAGS := -Wall -Wextra -g $(PKG_CFLAGS)

EMCC ?= emcc
EMCFLAGS := -Wall -Wextra -O2 -sUSE_SDL=2 -sFORCE_FILESYSTEM=1 -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1 \
	-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap'] \
	-sEXPORTED_FUNCTIONS=['_main','_video_granulator_init','_video_granulator_run','_video_granulator_shutdown']
WASM_CFLAGS := $(shell pkg-config --cflags sdl2 libavformat libavcodec libswscale libavutil)
WASM_LIBS := $(shell pkg-config --libs sdl2 libavformat libavcodec libswscale libavutil)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJECTS) -o $(BUILD_DIR)/$(TARGET) $(PKG_LIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: wasm
wasm:
	@mkdir -p $(WASM_BUILD_DIR)
	$(EMCC) $(SOURCES) $(WASM_CFLAGS) $(EMCFLAGS) $(WASM_LIBS) --shell-file web/shell.html -o $(WASM_TARGET)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(WASM_BUILD_DIR)

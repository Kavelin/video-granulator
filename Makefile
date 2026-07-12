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
	-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','HEAPU8'] \
	-sEXPORTED_FUNCTIONS=['_prepare_frame_buffers','_get_frame_buffer_pointer','_set_frame_count','_video_granulator_run','_video_granulator_shutdown','_set_granulator_spray','_set_granulator_step','_set_granulator_grain_frames','_set_granulator_overlay']
WASM_SOURCES := main.c window.c

$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJECTS) -o $(BUILD_DIR)/$(TARGET) $(PKG_LIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: wasm
wasm:
	@mkdir -p $(WASM_BUILD_DIR)
	$(EMCC) $(WASM_SOURCES) $(EMCFLAGS) --shell-file web/shell.html -o $(WASM_TARGET)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(WASM_BUILD_DIR)

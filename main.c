// Video granulator using SDL in the browser, with frame pixels streamed in from JavaScript.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <SDL2/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define VG_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define VG_KEEPALIVE
#endif

#include "frame.h"
#include "window.h"

typedef struct {
    FrameRGB *frames;
    int frame_capacity;
    int frame_count;
    int width;
    int height;
    double fps;
    SDL_Window *win;
    SDL_Renderer *renderer;
    SDL_Texture **textures;
    int texture_count;
    int quit;
    int start;
    int grain_frame;
    int grain_frames;
    int spray;
    int overlay;
    int step;
    Uint32 next_frame_tick;
    Uint32 frame_interval_ms;
} VideoGranulatorState;

static VideoGranulatorState g_state = {0};

static void video_granulator_free_frames(FrameRGB *frames, int frame_count) {
    if (!frames) {
        return;
    }

    for (int i = 0; i < frame_count; ++i) {
        free(frames[i].pixels);
        frames[i].pixels = NULL;
        frames[i].linesize = 0;
    }
    free(frames);
}

static void video_granulator_cleanup_state(VideoGranulatorState *state) {
    if (!state) {
        return;
    }

    cleanup_window(state->textures, state->texture_count, state->renderer, state->win);
    free(state->textures);
    state->textures = NULL;
    state->texture_count = 0;
    state->renderer = NULL;
    state->win = NULL;

    video_granulator_free_frames(state->frames, state->frame_capacity);
    state->frames = NULL;
    state->frame_capacity = 0;
    state->frame_count = 0;
    state->width = 0;
    state->height = 0;
    state->fps = 0.0;
    state->quit = 0;
    state->start = 0;
    state->grain_frame = 0;
    state->grain_frames = 0;
    state->spray = 0;
    state->overlay = 1;
    state->step = 0;
    state->next_frame_tick = 0;
    state->frame_interval_ms = 0;
}

VG_KEEPALIVE void prepare_frame_buffers(int count, int w, int h, double video_fps) {
    VideoGranulatorState *state = &g_state;

    if (count <= 0 || w <= 0 || h <= 0) {
        fprintf(stderr, "Invalid frame buffer request.\n");
        return;
    }

    video_granulator_cleanup_state(state);

    state->frames = (FrameRGB *)calloc((size_t)count, sizeof(FrameRGB));
    if (!state->frames) {
        fprintf(stderr, "Failed to allocate frame metadata.\n");
        return;
    }

    size_t pixel_bytes = (size_t)w * (size_t)h * 3u;
    for (int i = 0; i < count; ++i) {
        state->frames[i].pixels = (uint8_t *)malloc(pixel_bytes);
        if (!state->frames[i].pixels) {
            fprintf(stderr, "Failed to allocate frame buffer %d.\n", i);
            video_granulator_free_frames(state->frames, i);
            state->frames = NULL;
            return;
        }
        state->frames[i].linesize = w * 3;
    }

    state->frame_capacity = count;
    state->frame_count = 0;
    state->width = w;
    state->height = h;
    state->fps = video_fps > 0.0 ? video_fps : 30.0;
    state->frame_interval_ms = state->fps > 0.0 ? (Uint32)(1000.0 / state->fps) : 33U;
    if (state->frame_interval_ms == 0) {
        state->frame_interval_ms = 1;
    }
    state->start = 0;
    state->grain_frame = 0;
    state->quit = 0;
    state->next_frame_tick = SDL_GetTicks();
}

VG_KEEPALIVE uint8_t *get_frame_buffer_pointer(int index) {
    VideoGranulatorState *state = &g_state;

    if (!state->frames || index < 0 || index >= state->frame_capacity) {
        return NULL;
    }

    return state->frames[index].pixels;
}

VG_KEEPALIVE void set_frame_count(int actual_count) {
    VideoGranulatorState *state = &g_state;

    if (actual_count < 0) {
        actual_count = 0;
    }
    if (actual_count > state->frame_capacity) {
        actual_count = state->frame_capacity;
    }
    state->frame_count = actual_count;
}

static int video_granulator_prepare_renderer(VideoGranulatorState *state) {
    const int MAX_OVERLAY = 40;

    if (!state || !state->frames || state->frame_count <= 0 || state->width <= 0 || state->height <= 0) {
        fprintf(stderr, "Frame buffers are not prepared.\n");
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        video_granulator_cleanup_state(state);
        return 3;
    }

    if (create_window_and_renderer("Video Granulator", state->width, state->height, &state->win, &state->renderer) != 0) {
        fprintf(stderr, "Failed to create window and renderer.\n");
        video_granulator_cleanup_state(state);
        return 4;
    }

    state->texture_count = MAX_OVERLAY;
    state->textures = (SDL_Texture **)malloc((size_t)state->texture_count * sizeof(SDL_Texture *));
    if (!state->textures) {
        fprintf(stderr, "Failed to allocate mem for textures\n");
        video_granulator_cleanup_state(state);
        return 1;
    }

    for (int i = 0; i < state->texture_count; ++i) {
        state->textures[i] = SDL_CreateTexture(
            state->renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            state->width,
            state->height
        );
        if (!state->textures[i]) {
            fprintf(stderr, "Failed to create texture %d: %s\n", i, SDL_GetError());
            video_granulator_cleanup_state(state);
            return 1;
        }
        SDL_SetTextureBlendMode(state->textures[i], SDL_BLENDMODE_ADD);
    }

    state->grain_frames = 5;
    state->spray = 0;
    state->overlay = 1;
    state->step = 1;
    state->start = 0;
    state->grain_frame = 0;
    state->quit = 0;
    state->next_frame_tick = SDL_GetTicks();

    srand((unsigned int)time(NULL));
    return 0;
}

static void video_granulator_loop(void *arg) {
    VideoGranulatorState *state = (VideoGranulatorState *)arg;
    if (!state || state->quit) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return;
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            state->quit = 1;
        }
    }

    if (state->quit) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return;
    }

    Uint32 now = SDL_GetTicks();
    if (now < state->next_frame_tick) {
        return;
    }
    state->next_frame_tick = now + state->frame_interval_ms;

    int index = (state->start + state->grain_frame) % state->frame_count;

    SDL_RenderClear(state->renderer);
    Uint8 alpha = (Uint8)(255 / state->overlay);
    for (int i = 0; i < state->overlay; ++i) {
        int curSpray = state->spray > 0 ? rand() % state->spray * 2 - state->spray : 0;
        int curFrame = (index + i + curSpray + state->frame_count) % state->frame_count;
        SDL_SetTextureAlphaMod(state->textures[i], alpha);
        SDL_UpdateTexture(
            state->textures[i],
            NULL,
            state->frames[curFrame].pixels,
            state->frames[curFrame].linesize
        );
        SDL_RenderCopy(state->renderer, state->textures[i], NULL, NULL);
    }
    SDL_RenderPresent(state->renderer);

    state->grain_frame++;
    if (state->grain_frame >= state->grain_frames) {
        state->grain_frame = 0;
        state->start = (state->start + state->grain_frames * state->step) % state->frame_count;
    }
}

VG_KEEPALIVE void set_granulator_spray(int spray) {
    g_state.spray = spray < 0 ? 0 : spray;
}

VG_KEEPALIVE void set_granulator_step(int step) {
    g_state.step = step;
}

VG_KEEPALIVE void set_granulator_overlay(int overlay) {
    g_state.overlay = overlay;
}

VG_KEEPALIVE void set_granulator_grain_frames(int grain_frames) {
    if (grain_frames > 0 && grain_frames <= g_state.frame_count) {
        g_state.grain_frames = grain_frames;
        // Reset local frame index to prevent immediate out-of-bound spikes
        g_state.grain_frame = 0; 
    }
}

VG_KEEPALIVE int video_granulator_run(void) {
    int rc = video_granulator_prepare_renderer(&g_state);
    if (rc != 0) {
        return rc;
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(video_granulator_loop, &g_state, 0, 1);
    return 0;
#else
    while (!g_state.quit) {
        video_granulator_loop(&g_state);
        SDL_Delay(g_state.frame_interval_ms);
    }
    video_granulator_cleanup_state(&g_state);
    return 0;
#endif
}

VG_KEEPALIVE void video_granulator_shutdown(void) {
#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#endif
    video_granulator_cleanup_state(&g_state);
}

int main(int argc, char **argv) {
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    return 0;
#else
    return 1;
#endif
}

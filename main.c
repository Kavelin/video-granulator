// Video granulator using ffmpeg and SDL

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

#include "buffer.h"
#include "window.h"
#include "frame.h"

typedef struct {
    FrameRGB *frames;
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

    cleanup_frames(state->frames, state->frame_count);
    state->frames = NULL;
    state->frame_count = 0;
    state->quit = 0;
    state->start = 0;
    state->grain_frame = 0;
    state->next_frame_tick = 0;
    state->frame_interval_ms = 0;
}

static int video_granulator_prepare(VideoGranulatorState *state, const char *filename) {
    if (!state || !filename) {
        fprintf(stderr, "Missing filename.\n");
        return 1;
    }

    video_granulator_cleanup_state(state);

    const int GRAIN_FRAMES = 5;
    const int SPRAY = 0;
    const int OVERLAY = 1;
    const int STEP = 1;

    if (buffer((char *)filename, &state->frames, &state->frame_count, &state->width, &state->height, &state->fps) != 0) {
        fprintf(stderr, "Failed to buffer video frames.\n");
        return 2;
    }

    if (GRAIN_FRAMES > state->frame_count) {
        fprintf(stderr, "Grain frame is longer than video!\n");
        video_granulator_cleanup_state(state);
        return 5;
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

    state->texture_count = OVERLAY;
    state->textures = (SDL_Texture **)malloc((size_t)state->texture_count * sizeof(SDL_Texture *));
    if (!state->textures) {
        fprintf(stderr, "Failed to allocate mem for textures\n");
        video_granulator_cleanup_state(state);
        return 1;
    }

    for (int i = 0; i < state->texture_count; i++) {
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

    state->grain_frames = GRAIN_FRAMES;
    state->spray = SPRAY;
    state->overlay = OVERLAY;
    state->step = STEP;
    state->start = 0;
    state->grain_frame = 0;
    state->quit = 0;
    state->frame_interval_ms = state->fps > 0.0 ? (Uint32)(1000.0 / state->fps) : 33U;
    if (state->frame_interval_ms == 0) {
        state->frame_interval_ms = 1;
    }
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
    for (int i = 0; i < state->overlay; i++) {
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

VG_KEEPALIVE int video_granulator_init(const char *filename) {
    return video_granulator_prepare(&g_state, filename);
}

VG_KEEPALIVE int video_granulator_run(const char *filename) {
    int rc = video_granulator_prepare(&g_state, filename);
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
    video_granulator_cleanup_state(&g_state);
}

int main(int argc, char **argv) {
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input-video\n", argv[0]);
        return 1;
    }
    return video_granulator_run(argv[1]);
#endif
}

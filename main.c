// main.c
// Simple video "granulator" / glitch player using FFmpeg + SDL2.
// Decodes the whole video into memory (RGB24) then plays random grains.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>


#include "buffer.h"
#include "window.h"
#include "frame.h"


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input-video\n", argv[0]);
        return 1;
    }

    char *filename = argv[1];
    FrameRGB *frames;
    int frame_count, width, height;
    double fps;

    if (buffer(filename, &frames, &frame_count, &width, &height, &fps) != 0) {
        fprintf(stderr, "Failed to buffer video frames.\n");
        return 2;
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        cleanup_frames(frames, frame_count);
        return 14;
    }

    SDL_Window *win;
    SDL_Renderer *renderer;
    if (create_window_and_renderer("Video Granulator", width, height, &win, &renderer) != 0) {
        fprintf(stderr, "Failed to create window and renderer.\n");
        cleanup_frames(frames, frame_count);
        return 15;
    }

    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!tex) {
        fprintf(stderr, "SDL_CreateTexture error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(win);
        SDL_Quit();
        cleanup_frames(frames, frame_count);
        return 17;
    }


    
    const int GRAIN_FRAMES = 8; // length of each grain (frames)
    //const int SPRAY = 20;
    const int RANDOM_SEED = (int)time(NULL);
    srand(RANDOM_SEED);

    int quit = 0;
    SDL_Event ev;
    const Uint32 frame_delay_ms = (Uint32)(1000.0 / fps + 0.5);

    while (!quit) {
        // choose a random start index such that we have GRAIN_FRAMES contiguous frames
        int max_start = frame_count - GRAIN_FRAMES;
        if (max_start <= 0) { // if video shorter than grain size, just play sequentially
            // play sequentially once
            for (int i = 0; i < frame_count && !quit; ++i) {
                // handle events
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) quit = 1;
                }
                SDL_UpdateTexture(tex, NULL, frames[i].pixels, frames[i].linesize);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, tex, NULL, NULL);
                SDL_RenderPresent(renderer);
                SDL_Delay(frame_delay_ms);
            }
        }

        int start = rand() % (max_start + 1);

        for (int g = 0; g < GRAIN_FRAMES && !quit; ++g) {
            int idx = start + g;
            // event handling each frame
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) quit = 1;
            }

            // update texture with frame
            SDL_UpdateTexture(tex, NULL, frames[idx].pixels, frames[idx].linesize);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, tex, NULL, NULL);
            SDL_RenderPresent(renderer);

            SDL_Delay(frame_delay_ms);
        }

    }

    // cleanup
    cleanup_window(tex, renderer, win);
    cleanup_frames(frames, frame_count);
    fprintf(stderr, "Exited cleanly.\n");
    return 0;
}

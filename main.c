// Video granulator using ffmpeg and SDL


#include <stdio.h>
#include <stdlib.h>
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

    const int GRAIN_FRAMES = 5;
    const int SPRAY = 0;
    const int OVERLAY = 1;
    const int STEP = 1;

    if (buffer(filename, &frames, &frame_count, &width, &height, &fps) != 0) {
        fprintf(stderr, "Failed to buffer video frames.\n");
        return 2;
    }
    
    if (GRAIN_FRAMES > frame_count) {
        fprintf(stderr, "Grain frame is longer than video!\n");
        cleanup_frames(frames, frame_count);
        return 5;
    }


    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        cleanup_frames(frames, frame_count);
        return 3;
    }

    SDL_Window *win;
    SDL_Renderer *renderer;
    if (create_window_and_renderer("Video Granulator", width, height, &win, &renderer) != 0) {
        fprintf(stderr, "Failed to create window and renderer.\n");
        cleanup_frames(frames, frame_count);
        return 4;
    }

    
    

    int quit = 0;
    SDL_Event ev;

    SDL_Texture **textures = (SDL_Texture **)malloc(OVERLAY * sizeof(SDL_Texture *));
    if (!textures) {
        fprintf(stderr, "Failed to allocate mem for textures\n");
        cleanup_frames(frames, frame_count);
        cleanup_window(NULL, 0, renderer, win);
        return 1;
    }
    for (int i = 0; i < OVERLAY; i++) {
        textures[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!textures[i]) {
            fprintf(stderr, "Failed to create texture %d: %s\n", i, SDL_GetError());
            for (int j = 0; j < i; j++) {
                SDL_DestroyTexture(textures[j]);
            }
            free(textures);
            cleanup_frames(frames, frame_count);
            cleanup_window(NULL, 0, renderer, win);
            return 1;
        }
        SDL_SetTextureBlendMode(textures[i], SDL_BLENDMODE_ADD);
    }

    srand((int)time(NULL));
    int start = 0;
    while (!quit) {

        start += (GRAIN_FRAMES * STEP) % frame_count;

        
        for (int g = 0; g < GRAIN_FRAMES && !quit; ++g) {
            int index = (start + g) % frame_count;

            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) quit = 1;
            }
            SDL_RenderClear(renderer);
            Uint8 alpha = (Uint8) (255 / OVERLAY);
            for (int i = 0; i < OVERLAY; i++) {
                int curSpray = SPRAY > 0 ? rand() % SPRAY * 2 - SPRAY : 0;
                int curFrame = (index + i + curSpray + frame_count) % frame_count;
                SDL_SetTextureAlphaMod(textures[i], alpha);
                SDL_UpdateTexture(textures[i], 
                    NULL, 
                    frames[curFrame].pixels, 
                    frames[curFrame].linesize
                );
                SDL_RenderCopy(renderer, textures[i], NULL, NULL);
            }
            SDL_RenderPresent(renderer);

            SDL_Delay((Uint32)(1000.0 / fps));
        }

    }

    // cleanup
    cleanup_window(textures, OVERLAY, renderer, win);
    cleanup_frames(frames, frame_count);
    fprintf(stderr, "Exited cleanly.\n");
    return 0;
}

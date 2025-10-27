
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

void cleanup_window(SDL_Texture *tex, SDL_Renderer *renderer, SDL_Window *win) {
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

int create_window_and_renderer(const char* title, int width, int height,
                                SDL_Window **out_win, SDL_Renderer **out_renderer) {
    SDL_Window *win = SDL_CreateWindow(title,
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        *out_win = NULL;
        *out_renderer = NULL;
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        *out_win = NULL;
        *out_renderer = NULL;
        return 2;
    }

    *out_win = win;
    *out_renderer = renderer;
    return 0;
}
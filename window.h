#ifndef WINDOW_H
#define WINDOW_h

void cleanup_window(SDL_Texture *tex, SDL_Renderer *renderer, SDL_Window *win);

int create_window_and_renderer(const char* title, int width, int height,
                                SDL_Window **out_win, SDL_Renderer **out_renderer);


#endif
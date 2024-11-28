#ifndef SDLSTUFF
#define SDLSTUFF

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <SDL.h>

#define PLATES 10

typedef struct {
    float x;
    float y;
} point_t;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    float offx, offy;
    float zoom;
    float avgd;
    size_t width, height, stride;
    size_t window_width, window_height;
    int seed;
    int prev, current;
    point_t *points;
    point_t plates[PLATES];
    point_t vel[PLATES];
    size_t points_amount;

    bool quit;
} state_t;

extern state_t *state;

#define ASSERT(cond, msg, ...)                                               \
    do {                                                                     \
        if(!(cond)) {                                                        \
            fprintf(stderr, "assert: " msg "\n" __VA_OPT__(, ) __VA_ARGS__); \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while(0);

void initsdl(void);
void sdlrender(void);
void deinitsdl(void);

float SDL_GetMS(void);

#endif

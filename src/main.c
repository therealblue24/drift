#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_scancode.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <SDL.h>
#include "sdlstuff.h"
#include "stb_perlin.h"
#include "stb_image_write.h"

state_t *state = NULL;

uint32_t stpx(int x, int y, uint32_t col)
{
    state->pixels[x + y * state->width] = col;
    return state->pixels[x + y * state->width];
}

uint32_t stpx_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    union {
        uint32_t raw;
        struct {
            uint8_t r, g, b, a;
        };
    } c;
    c.a = 255;
    c.r = r;
    c.g = g;
    c.b = b;
    return stpx(x, y, c.raw);
}

void preinit(void)
{
    state = calloc(1, sizeof(state_t));
    ASSERT(state, "failed to allocate state!");
    state->width = 800;
    state->height = 600;
    state->window_width = 800;
    state->window_height = 600;
    state->prev = 0;
    state->current = 0;

    state->stride = state->width * 4;
    state->pixels = calloc(state->height, state->stride);
    ASSERT(state->pixels, "failed to allocate pixels!");
}

void postdeinit(void)
{
    free(state->pixels);
    free(state);
}

float noise_(float x, float y, float z, int se)
{
    float n = stb_perlin_noise3_seed(x, y, z, 0, 0, 0, se);
    float np = stb_perlin_noise3_seed(x, y, z, 0, 0, 0, se + 1);
    if(np < 0.5)
        np = 0.5;
    float n2 = stb_perlin_turbulence_noise3(x, y, z, 2.0, 0.5, 6, se);
    float n3 = stb_perlin_ridge_noise3(x, y, z, 2.0, 1.0, 1.0, 6, se);
    float n4 = stb_perlin_fbm_noise3(x, y, z, 2.5, 0.5, 6, se);
    float m = n3 * n;
    //return (n + n2 + m) / 3;
    float m0 = (2 * n) + n2 + m;
    float f0 = (m0 + m + n4) * np;
    float f1 =
        stb_perlin_turbulence_noise3(x, y, z, 2.0, 0.5, /*offset 1.2,*/ 6, se);
    float f2 = stb_perlin_ridge_noise3(x, y, z, 2.0, 0.5, 1.4, 6, se);
    float f = (f1 + f2 + f0) / 8;
    return f;
}

float ss(float x, float n)
{
    return powf(M_E, -75 * (x - n) * (x - n));
}

float ss2g(float x)
{
    return (tanhf(x) / 2) + .5;
}

float ss2(float x, float n)
{
    return ss2g(10 * (x - n + 0.07));
}

float noise(float x, float y, float z, int se)
{
    float n0 = stb_perlin_turbulence_noise3(x, y, z, 2.1, 0.6, 6, se);
    float n1 =
        stb_perlin_ridge_noise3(2 * x, 2 * y, 2 * z, 1.9, 0.4, 1.1, 6, se + 1);
    float n2 = stb_perlin_fbm_noise3(4 * x, 4 * y, 4 * z, 2.0, 0.5, 6, se + 2);
    float n = n0 + (n1 / 2) + (n2 / 4);
    n /= (1 + .5 + .25);
    float f = stb_perlin_noise3_seed(x, y, z, 0, 0, 0, se + 3);
    n *= f;
    n += stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 6, se + 4);
    n /= 2;
    n = ss(n, 0.2);
    float f2 =
        stb_perlin_noise3_seed(0.5 * x, 0.5 * y, 0.5 * z, 0, 0, 0, se + 5);
    float f3 = stb_perlin_noise3_seed(2 * x, 2 * y, 2 * z, 0, 0, 0, se + 6);
    n = ss((n + f2 + f3) / 3, 0.4);

    /* n = water factor */
    float w = (1 - n); /* mutliply by */

    float final = w * stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 6, se + 7);
    return ss2(ss2(final, 0.15) * noise_(x, y, z, se + 8), 0.25);
}

float mountain(float x, float y, float z, int se)
{
    float m = stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 6, se);
    return m;
}

void update_noise(float z)
{
    for(int x = 0; x < state->width; x++) {
        for(int y = 0; y < state->height; y++) {
            float nx = x / (float)state->width;
            float ny = y / (float)state->height;
            nx *= 2, ny *= 2;
            float n = noise(nx, ny, z, state->seed);
            if(n < 0)
                n = 0;
            if(n > 1)
                n = 1;
            float g = ss2(n, 0.7);
            float w = ss2(1 - n, 0.7);
            float m = g + w;
            const uint8_t sea[3] = { 20, 245, 207 };
            /*g /= m;
			w /= m;*/
            const uint8_t mountainc[3] = { 80, 115, 89 };
            float imountain = g * ss2(mountain(nx, ny, z, state->seed), 0.35);
            float gc = g * 255;
            float bc = w * 255;
            float rc = (mountainc[0] * imountain);
            gc = (imountain * mountainc[1]) + ((1 - imountain) * gc);
            bc = (imountain * mountainc[2]) + ((1 - imountain) * bc);

            float s = 1 - m;

            rc = (m * rc) + (s * sea[0]);
            gc = (m * gc) + (s * sea[1]);
            bc = (m * bc) + (s * sea[2]);

            stpx_rgb(x, y, rc, gc, bc);
        }
    }
}

void init(void)
{
    state->seed = rand();
    update_noise(0.0);
}

int frame(void)
{
    SDL_Event ev;
    state->current = false;

    static int dont = 1;
    static float t = 0;
    while(SDL_PollEvent(&ev)) {
        switch(ev.type) {
        case SDL_QUIT:
            state->quit = true;
            return EXIT_FAILURE;
            break;
        case SDL_KEYUP:
            if(ev.key.keysym.sym == SDLK_SPACE) {
                dont = 1;
                stbi_write_png("out.png", state->width, state->height, 4,
                               state->pixels, state->stride);
            } else {
                if(ev.key.keysym.sym == SDLK_q)
                    dont = 0;
                if(ev.key.keysym.sym == SDLK_w)
                    dont = 1;
            }

            break;
        }
    }

    if(!dont) {
        t += 1. / 45.;
        update_noise(t);
    }

    return EXIT_SUCCESS;
}
void deinit(void)
{
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    preinit();
    initsdl();
    init();

    float now = SDL_GetMS();
    float last_frame = -1.0f;
    while(!state->quit) {
        if(now - last_frame > 1. / 45.) {
            last_frame = now;
            if(frame()) {
                fprintf(stderr, "ERROR: Failed to frame\n");
                goto escape;
            }
            if(state->quit) {
                goto escape;
            }

            sdlrender();
        }
        now = SDL_GetMS();
    }
escape:
    deinitsdl();

    deinit();
    postdeinit();
    return 0;
}

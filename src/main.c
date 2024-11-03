#include "SDL_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>
#include "sdlstuff.h"
#include "stb_perlin.h"
#include "stb_image_write.h"
#include "ffmpeg.h"

state_t *state = NULL;

#define WIDTH 400
#define HEIGHT 300

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
    state->width = WIDTH;
    state->height = HEIGHT;
    state->window_width = 800;
    state->window_height = 600;
    state->prev = 0;
    state->zoom = 1;
    state->offx = state->offy = 0;
    state->current = 0;

    state->stride = state->width * 4;
    state->pixels = calloc(state->height, state->stride);
    ASSERT(state->pixels, "failed to allocate pixels!");
}

void setres(size_t w, size_t h)
{
    state->width = w;
    state->height = h;
    free(state->pixels);
    state->stride = state->width * 4;
    state->pixels = calloc(state->height, state->stride);
    ASSERT(state->pixels, "failed to allocate pixels!");
}

void postdeinit(void)
{
    free(state->pixels);
    free(state);
}

/* Original noise algorithim */
static inline float noise_(float x, float y, float z, int se)
{
    // 2 noise maps
    float n = stb_perlin_noise3_seed(x, y, z, 0, 0, 0, se);
    float np = stb_perlin_noise3_seed(x, y, z, 0, 0, 0, se + 1);
    // limit the 2nd
    if(np < 0.5) {
        np = 0.5;
    }
    // generate 3 special noise maps
    float n2 = stb_perlin_turbulence_noise3(x, y, z, 2.0, 0.5, 6, se);
    float n3 = stb_perlin_ridge_noise3(x, y, z, 2.0, 1.0, 1.0, 6, se);
    float n4 = stb_perlin_fbm_noise3(x, y, z, 2.5, 0.5, 6, se);
    // overlay
    float m = n3 * n;
    float m0 = (2 * n) + n2 + m;
    // i genuienly forgot what all this meant it's been a long time
    float f0 = (m0 + m + n4) * np;
    float f1 = stb_perlin_turbulence_noise3(x, y, z, 2.0, 0.5, 6, se);
    float f2 = stb_perlin_ridge_noise3(x, y, z, 2.0, 0.5, 1.4, 6, se);
    float f = (f1 + f2 + f0) / 8;
    return f;
}

// Generates a gaussian around n
static inline float ss(float x, float n)
{
    return powf(M_E, -75 * (x - n) * (x - n));
}

static inline float ss2g(float x)
{
    return (tanhf(x) / 2) + .5;
}

// Generates 0 before n and 1 after n (smooth)
static inline float ss2(float x, float n)
{
    return ss2g(10 * (x - n + 0.07));
}

/* Main noise function */
float noise(float x, float y, float z, int se)
{
    // generate FBM noise from special noisemaps
    float n0 = stb_perlin_turbulence_noise3(x, y, z, 2.1, 0.6, 6, se);
    float n1 =
        stb_perlin_ridge_noise3(2 * x, 2 * y, 2 * z, 1.9, 0.4, 1.1, 6, se + 1);
    float n2 = stb_perlin_fbm_noise3(4 * x, 4 * y, 4 * z, 2.0, 0.5, 6, se + 2);
    float n = n0 + (n1 / 2) + (n2 / 4);
    n /= (1 + .5 + .25);
    // noise shenatigans
    float f = stb_perlin_noise3_seed(x, y, z, 0, 0, 0, se + 3);
    n *= f;
    n += stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 6, se + 4);
    n /= 2;
    // peak @ 0.2
    n = ss(n, 0.2);
    // overlay more noisemaps to generate water factor
    float f2 =
        stb_perlin_noise3_seed(0.5 * x, 0.5 * y, 0.5 * z, 0, 0, 0, se + 5);
    float f3 = stb_perlin_noise3_seed(2 * x, 2 * y, 2 * z, 0, 0, 0, se + 6);
    // peak @ 0.4 (1.2 technically)
    n = ss((n + f2 + f3) / 3, 0.4);

    /* n = water factor */
    float w = (1 - n); /* mutliply by */

    // final _smooth_ map
    float final = w * stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 6, se + 7);
    // make it softer
    float soft = ss2(ss2(final, 0.15) * noise_(x, y, z, se + 8), 0.25);

    // then make it more landlike
    float rough = (0.4 * soft +
                   0.6 * stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 6, se + 9));
    return (soft + rough) / 2;
}

/* noisemap for the mountains (very simple) */
static inline float mountain(float x, float y, float z, int se)
{
    float m = stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 6, se);
    return m;
}

/* noisemap for snow regions (very simple) */
static inline float snow(float x, float y, float z, int se)
{
    float m = stb_perlin_fbm_noise3(x, y, z, 2.0, 0.5, 2, se);
    float n =
        stb_perlin_turbulence_noise3(2 * x, 2 * y, 2 * z, 2.0, 0.5, 4, se + 1);
    return m * m * n;
}

/* water coloration noise */
static inline float watercol(float x, float y, float z, int se)
{
    float n = stb_perlin_noise3_seed(1.2 * x, 1.2 * y, 4 * z, 0, 0, 0, se);
    return (n + 1) / 2;
}

/* water depth noise */
static inline float waterdepth(float x, float y, float z, int se)
{
    float n =
        (stb_perlin_fbm_noise3(0.5 * x, 0.5 * y, 0.5 * z, 2.0, 0.5, 6, se) +
         1) /
        2;
    float m =
        (stb_perlin_turbulence_noise3(x, y, z, 2.0, 0.5, 4, se + 1) + 1) / 2;
    return (n + m) / 2;
}

/* desert biome noise */
static inline float desertnoise(float x, float y, float z, int se)
{
    float n0 = stb_perlin_noise3_seed(x, y, 2 * z, 0, 0, 0, se);
    float n1 = stb_perlin_noise3_seed(x, y, 3 * z, 0, 0, 0, se + 1);
    return ss2((n0 + 1) / 2, 0.6125);
}

/* mix 2 colors */
static inline void mix(float a1, float b1, float c1, float a2, float b2,
                       float c2, float t, float *r, float *g, float *b)
{
    *r = ((1 - t) * a1) + t * a2;
    *g = ((1 - t) * b1) + t * b2;
    *b = ((1 - t) * c1) + t * c2;
}

void update_raw(float z)
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
        }
    }
}

float fclamp(float v, float l, float h)
{
    if(v < l)
        v = l;
    if(v > h)
        v = h;
    return v;
}

void update_noise(float z, int mode)
{
    for(int x = 0; x < state->width; x++) {
        for(int y = 0; y < state->height; y++) {
            float zoomfactor = sqrtf(fabsf(state->zoom)) * fabsf(state->zoom);
            /* x, y coords */
            float nx = x / (float)(state->width);
            float ny = y / (float)(state->height);
            /* scale them */
            nx *= 2 * zoomfactor, ny *= 2 * zoomfactor;
            nx += state->offx, ny += state->offy;
            /* main noise */
            float n = noise(nx, ny, z, state->seed);
            /* limit */
            if(n < 0)
                n = 0;
            if(n > 1)
                n = 1;
            /* g = land
             * w = water
             * m = mismatch factor
             */
            if(!mode) {
                float g = ss2(n, 0.7); // make land peak above 0.7
                float w =
                    ss2(1 - n, 0.7); // make water peak above 0.7 for (1-n)
                float rg = fclamp(n - 0.35, 0, 1);
                float rw = fclamp((1 - n) - 0.35, 0, 1);
                float rm = rg + rw;
                //rg /= rm;
                //rw /= rm;
                float m = g + w;
                g /= m; // fix land and water ratios
                w /= m;

                const uint8_t sea[3] = { 20, 245, 207 };
                const uint8_t mountainc[3] = { 80, 115, 89 };
                const uint8_t water_col[3] = { 5, 97, 245 };
                //const uint8_t watercol2[3] = { 13, 180, 222 };
                const uint8_t watercol2[3] = { 21, 24, 176 };
                /* calculate mountain noise, water color noise, snow noise */
                float rmt = mountain(nx, ny, z, state->seed);
                float watercolv = watercol(nx, ny, z, state->seed + 21);
                float snowv = snow(nx, ny, z, state->seed + 1);
                /* make mountain noise peak at 0.35 and above, multiply by land factor */
                float imountain = g * ss2(rmt, 0.35);

                /* gc = green color, bc = blue color, rc = red color */
                float gc = 0;
                float bc = 0;
                // float rc = (mountainc[0] * imountain);
                float rc = 0;
                /* wr = water red, wg = water green, wb = water blue */
                float wr = 0, wg = 0, wb = 0;
                float wd = waterdepth(nx, ny, z, state->seed + 41) *
                           0.9; /* water depth */
                wd = ((0.25 * wd) + (0.75 * rw));

                /* mix water colors based on water noise */
                mix(wr, wg, wb, water_col[0], water_col[1], water_col[2],
                    w * watercolv, &wr, &wg, &wb);
                mix(wr, wg, wb, watercol2[0], watercol2[1], watercol2[2],
                    w * (1 - watercolv), &wr, &wg, &wb);
                wr *= 1 - (wd * wd);
                wg *= 1 - (wd * wd);
                wb *= 1 - (wd * wd);
                /* add water color to the image */
                mix(rc, gc, bc, wr, wg, wb, w, &rc, &gc, &bc);

                /* add land color to the image */
                const uint8_t land[3] = { 0, 254, 48 };
                const uint8_t desert[3] = { 230, 211, 126 };
                float landcol[3] = { 0, 254, 48 };
                float desertv = desertnoise(nx, ny, z, state->seed + 12345);
                mix(landcol[0], landcol[1], landcol[2], desert[0], desert[1],
                    desert[2], desertv, &landcol[0], &landcol[1], &landcol[2]);
                mix(rc, gc, bc, landcol[0], landcol[1], landcol[2], g, &rc, &gc,
                    &bc);

                // gc = (imountain * mountainc[1]) + ((1 - imountain) * gc);
                // bc = (imountain * mountainc[2]) + ((1 - imountain) * bc);

                /* add mountain color to the image */
                mix(rc, gc, bc, mountainc[0], mountainc[1], mountainc[2],
                    imountain, &rc, &gc, &bc);
                /* add snow color to the image */
                mix(rc, gc, bc, 255, 255, 255, snowv * g, &rc, &gc, &bc);
                /* calculate sea/shoreline factor */
                float s = 1 - (ss2(g * m, 0.5) +
                               ss2(w * m, 0.5)); /* calculate sea amount */

                /* mix sea/shoreline color to image */
                mix(rc, gc, bc, sea[0], sea[1], sea[2], s * 0.5, &rc, &gc, &bc);

                /*
            float s = 1 - (m);
            float as = (m);

            rc = (as * rc) + (s * sea[0]);
            gc = (as * gc) + (s * sea[1]);
            bc = (as * bc) + (s * sea[2]);
            */
                /* upload to image */
                stpx_rgb(x, y, rc, gc, bc);
            } else {
                stpx_rgb(x, y, n * 255, n * 255, n * 255);
            }
            /*
            n *= 100;
            uint8_t rc = 0;
            uint8_t gc = 0;
            uint8_t bc = 0;
            const uint8_t mountainc[3] = { 80, 115, 89 };
            float imountain =
                (n > 50) * ss2(mountain(nx, ny, z, state->seed), 0.35);
            if(n > 50) {
                rc = 0;
                gc = n + 155;
                bc = 0;
            } else {
                rc = 0;
                gc = 0;
                bc = n + 155;
            }

            rc = (1 - imountain) * rc + imountain * mountainc[0];
            gc = (1 - imountain) * gc + imountain * mountainc[1];
            bc = (1 - imountain) * bc + imountain * mountainc[2];
            stpx_rgb(x, y, rc, gc, bc);
            */
        }
    }
}

void init(void)
{
    state->seed = rand();
    update_noise(0.0, 0); /* init noise at t=0 */
}

int frame(void)
{
    SDL_Event ev;
    state->current = false;

    static int dont = 1;
    static float t = 0;
    static float d = 1;
    static int recording = 0;
    static int moving = 0;
    static int didsmth = 0;
    static int mode = 0;
    static FFMPEG *ffmpeg = NULL;
    while(SDL_PollEvent(&ev)) {
        switch(ev.type) {
        case SDL_MOUSEBUTTONDOWN: {
            moving = 1;
            break;
        }
        case SDL_MOUSEBUTTONUP: {
            moving = 0;
            break;
        }
        case SDL_MOUSEWHEEL: {
            SDL_MouseWheelEvent mev = ev.wheel;
            float s = mev.preciseY / 60;
            state->zoom -= s;
            if(state->zoom <= 0.00001)
                state->zoom = 0.00001;
            didsmth = 1;
            break;
        }
        case SDL_MOUSEMOTION: {
            SDL_MouseMotionEvent mev = ev.motion;
            if(moving) {
                state->offx -= (mev.xrel / (float)state->width) * state->zoom;
                state->offy -= (mev.yrel / (float)state->height) * state->zoom;
                didsmth = 1;
            }
            break;
        }
        case SDL_QUIT:
            state->quit = true;
            return EXIT_FAILURE;
            break;
        case SDL_KEYUP:
            if(ev.key.keysym.sym == SDLK_1) {
                mode = 1;
                didsmth = 1;
            } else if(ev.key.keysym.sym == SDLK_2) {
                mode = 0;
                didsmth = 1;
            }
            if(ev.key.keysym.sym == SDLK_p) {
                if(!recording) {
                    ffmpeg = ffmpeg_start_rendering(WIDTH, HEIGHT, 12);
                    recording = 1;
                    printf("Starting to record!\n");
                } else {
                    ffmpeg_end_rendering(ffmpeg);
                    ffmpeg = NULL;
                    recording = 0;
                    printf("Stopped recording!\n");
                }
            }
            if(ev.key.keysym.sym == SDLK_e) {
                d = 1;
            } else if(ev.key.keysym.sym == SDLK_r) {
                d = -1;
            }
            if(ev.key.keysym.sym == SDLK_x) {
                state->quit = true;
                return EXIT_FAILURE;
                break;
            }
            if(ev.key.keysym.sym == SDLK_SPACE) {
                dont = 1;
                SDL_HideWindow(state->window);
                SDL_PumpEvents();
                printf("What do you want to output the capture to: ");
                char b[256] = { 0 };
                fgets(b, 256, stdin);
                for(int i = 0; i < 256; i++) {
                    if(!isprint(b[i]))
                        b[i] = 0;
                    if(b[i] == '\r' || b[i] == '\n')
                        b[i] = 0;
                }
                setres(10, 10);
                sdlrender();
                setres(1280, 960);
                update_noise(t, mode);
                stbi_write_png(b, state->width, state->height, 4, state->pixels,
                               state->stride);
                setres(WIDTH, HEIGHT);
                update_noise(t, mode);

                SDL_ShowWindow(state->window);
                SDL_PumpEvents();
            } else {
                if(ev.key.keysym.sym == SDLK_q)
                    dont = 0;
                if(ev.key.keysym.sym == SDLK_w)
                    dont = 1;
            }

            break;
        }
    }

    /* controls:
        q:       run
        w:     pause
        e:  forwards
        r: backwards
        p:    record
        space:  save
    */
    if(!dont) {
        didsmth = 0;
        t += (d * 1.) / 60.;
        update_noise(t, mode);
        if(recording) {
            ffmpeg_send_frame(ffmpeg, state->pixels, WIDTH, HEIGHT);
        }
    }
    if(didsmth)
        update_noise(t, mode);
    didsmth = 0;

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
        if(now - last_frame > 1. / 60.) {
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

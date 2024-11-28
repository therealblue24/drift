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

float randf(void)
{
    float r = rand() / (float)RAND_MAX;
    r -= 0.5;
    r *= 2;
    return r;
}

int ssx(float x)
{
    float l = x * WIDTH;
    if(l < 0)
        l = 0;
    if(l > WIDTH)
        l = WIDTH;
    l = floorf(l);
    return (int)l;
}

int ssy(float y)
{
    float l = y * HEIGHT;
    if(l < 0)
        l = 0;
    if(l > HEIGHT)
        l = HEIGHT;
    l = floorf(l);
    return (int)l;
}

uint32_t stpx(int _x, int _y, uint32_t col)
{
    int x = _x;
    int y = _y;
    if(x < 0)
        x = 0;
    if(y < 0)
        y = 0;
    if(x >= state->width)
        x = (state->width) - 1;
    if(y >= state->height)
        y = (state->height) - 1;
    state->pixels[x + y * state->width] = col;
    return state->pixels[x + y * state->width];
}

float dis(float x0, float y0, float x1, float y1)
{
    float x = x0 - x1;
    float y = y0 - y1;
    return sqrtf((x * x) + (y * y));
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

/* mix 2 colors */
static inline void mix(float a1, float b1, float c1, float a2, float b2,
                       float c2, float t, float *r, float *g, float *b)
{
    *r = ((1 - t) * a1) + t * a2;
    *g = ((1 - t) * b1) + t * b2;
    *b = ((1 - t) * c1) + t * c2;
}

void pointdraw(int x, int y, int rad, uint8_t r, uint8_t g, uint8_t b)
{
    for(int _x = -rad; _x <= rad; _x++) {
        for(int _y = -rad; _y <= rad; _y++) {
            stpx_rgb(x + _x, y + _y, r, g, b);
        }
    }
    stpx_rgb(x, y, r, g, b);
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
    state->points_amount = 0;
    state->current = 0;

    state->stride = state->width * 4;
    state->pixels = calloc(state->height, state->stride);
    ASSERT(state->pixels, "failed to allocate pixels!");
    state->points = calloc(state->height * sizeof(point_t), state->width);
    ASSERT(state->points, "failed to allocate points!");
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
    state->points_amount = 0;
    for(int x = 0; x < state->width; x++) {
        for(int y = 0; y < state->height; y++) {
            float zoomfactor = sqrtf(fabsf(state->zoom)) * fabsf(state->zoom);
            /* x, y coords */
            float _nx = x / (float)(state->width);
            float _ny = y / (float)(state->height);
            float nx = _nx;
            float ny = _ny;
            /* scale them */
            nx *= 2 * zoomfactor, ny *= 2 * zoomfactor;
            nx += state->offx, ny += state->offy;
            /* main noise */
            float n = noise(nx, ny, z, state->seed);
            float island = ss2(n, 0.4);
            island = ss(island * island, 1.05);
            /* limit */
            if(n < 0)
                n = 0;
            if(n > 1)
                n = 1;
            /*
            n = ss2(n, 0.7);
            int b = (n > 0.5) * 255;
            stpx_rgb(x, y, b, b, b);
            */

            n *= 75;
            n += (island * 25);

            uint8_t rc = 0, gc = 0, bc = 0;
            if(n > 50) {
                rc = 0;
                gc = n + 154;
                state->points[state->points_amount].x = _nx;
                state->points[state->points_amount].y = _ny;
                state->points_amount++;
                bc = 0;
            } else {
                rc = 0;
                gc = 0;
                bc = n + 154;
            }

            stpx_rgb(x, y, rc, gc, bc);
        }
    }
}

int voronoi(int _x, int _y)
{
    int x = _x % WIDTH;
    int y = _y % HEIGHT;
    float fx = x / (float)WIDTH;
    float fy = y / (float)HEIGHT;
    int n = 0;
    for(int i = 0; i < PLATES; i++) {
        if(dis(fx, fy, state->plates[i].x, state->plates[i].y) <
           dis(fx, fy, state->plates[n].x, state->plates[n].y))
            n = i;
    }
    return n;
}

void sim(float avgd)
{
    memset(state->pixels, 0, state->width * state->height * 4);
    for(int x = 0; x < WIDTH; x++) {
        for(int y = 0; y < HEIGHT; y++) {
            stpx_rgb(x, y, 0, 0, 255);
        }
    }
    for(size_t i = 0; i < state->points_amount; i++) {
        int farest = 0;
        int nearest = 0;
        float pointx = state->points[i].x;
        float pointy = state->points[i].y;
        float velx = 0;
        float vely = 0;
        for(int j = 0; j < PLATES; j++) {
            float cd =
                dis(pointx, pointy, state->plates[j].x, state->plates[j].y);
            if(cd > dis(pointx, pointy, state->plates[farest].x,
                        state->plates[farest].y)) {
                farest = j;
            }
            if(cd < dis(pointx, pointy, state->plates[nearest].x,
                        state->plates[nearest].y)) {
                nearest = j;
            }
        }
        const float fd = dis(pointx, pointy, state->plates[farest].x,
                             state->plates[farest].y);
        for(int j = 0; j < PLATES; j++) {
            const float F = (0.75 / (float)PLATES);
            float d =
                dis(pointx, pointy, state->plates[j].x, state->plates[j].y);
            if(j == nearest) {
                velx += 0.05 * state->vel[j].x;
                vely += 0.05 * state->vel[j].y;
            } else {
                if(d != fd) {
                    d /= fd;
                    d *= d;
                    d *= F;
                    velx += (d * state->vel[j].x);
                    vely += (d * state->vel[j].y);
                } else {
                    velx += (F * F * state->vel[j].x);
                    vely += (F * F * state->vel[j].y);
                }
            }
        }
        pointx += velx;
        pointy += vely;
        pointx = fmodf(pointx, 1.0);
        pointy = fmodf(pointy, 1.0);
        if(pointx > 1.0) {
            pointx -= 1.0;
        }
        if(pointx < 0.0) {
            pointx += 1.0;
        }
        if(pointy > 1.0) {
            pointy -= 1.0;
        }
        if(pointy < 0.0) {
            pointy += 1.0;
        }
        state->points[i].x = pointx;
        state->points[i].y = pointy;
        int px = ssx(pointx);
        int py = ssy(pointy);
        pointdraw(px, py, 1, 0, 255, 0);
    }
    for(int i = 0; i < PLATES; i++) {
        state->plates[i].x += 0.07 * state->vel[i].x;
        state->plates[i].y += 0.07 * state->vel[i].y;
        state->plates[i].x = fmodf(state->plates[i].x, 1.0);
        state->plates[i].y = fmodf(state->plates[i].y, 1.0);
        state->vel[i].x += randf() / (5000 / ((49 * state->vel[i].x)) + 1);
        state->vel[i].y += randf() / (5000 / ((49 * state->vel[i].y)) + 1);

        pointdraw(ssx(state->plates[i].x), ssy(state->plates[i].y), 4, 255, 255,
                  255);
    }

    float avgc = 0;
    for(size_t i = 0; i < state->points_amount; i++) {
        size_t nxt = (i - 1) % state->points_amount;
        float p1x = state->points[i].x;
        float p1y = state->points[i].y;
        float p2x = state->points[nxt].x;
        float p2y = state->points[nxt].y;
        float d = dis(p1x, p1y, p2x, p2y);
        avgc += (d / (float)state->points_amount);
    }

    float repel = 1 / (avgc / avgd);

    for(size_t i = 0; i < state->points_amount; i++) {
        size_t nxt = (i - 1) % state->points_amount;
        float point1x = state->points[i].x;
        float point2x = state->points[nxt].x;
        float point1y = state->points[i].y;
        float point2y = state->points[nxt].y;

        float d = dis(point1x, point1y, point2x, point2y);
        float r = (avgd / avgc) * (1 / (d + 0.00001));

        /*       if(r_ > 1000)
            r = 2;
*/
        point1x += 0.00005 * r * randf();
        point2x += 0.00005 * r * randf();
        point1y += 0.00005 * r * randf();
        point2y += 0.00005 * r * randf();

        point1x = fmodf(point1x, 1.0);
        point1y = fmodf(point1y, 1.0);
        if(point1x > 1.0) {
            point1x -= 1.0;
        }
        if(point1x < 0.0) {
            point1x += 1.0;
        }
        if(point1y > 1.0) {
            point1y -= 1.0;
        }
        if(point1y < 0.0) {
            point1y += 1.0;
        }
        point2x = fmodf(point2x, 1.0);
        point2y = fmodf(point2y, 1.0);
        if(point2x > 1.0) {
            point2x -= 1.0;
        }
        if(point2x < 0.0) {
            point2x += 1.0;
        }
        if(point2y > 1.0) {
            point2y -= 1.0;
        }
        if(point2y < 0.0) {
            point2y += 1.0;
        }
        state->points[i].x = point1x;
        state->points[nxt].x = point2x;
        state->points[i].y = point1y;
        state->points[nxt].y = point2y;
    }
    for(int x = 0; x < WIDTH; x++) {
        for(int y = 0; y < HEIGHT; y++) {
            int x0 = x;
            int y0 = y + 1;
            int x1 = x;
            int y1 = y - 1;
            int x2 = x - 1;
            int y2 = y;
            int x3 = x + 1;
            int y3 = y;
            int x4 = x;
            int y4 = y;
            int v0 = voronoi(x0, y0);
            int v1 = voronoi(x1, y1);
            int v2 = voronoi(x2, y2);
            int v3 = voronoi(x3, y3);
            int v4 = voronoi(x4, y4);
            int f = 0;
            if(v1 != v4)
                f++;
            if(v2 != v4)
                f++;
            if(v3 != v4)
                f++;
            if(v0 != v4)
                f++;
            if(f)
                stpx_rgb(x, y, 255, 0, 0);
        }
    }
}

void init(void)
{
    for(int i = 0; i < PLATES; i++) {
        int rand1 = rand();
        int rand2 = rand();
        float r1 = (rand1) / (float)RAND_MAX;
        float r2 = (rand2) / (float)RAND_MAX;
        state->plates[i].x = r1;
        state->plates[i].y = r2;
    }
    for(int i = 0; i < PLATES; i++) {
        state->vel[i].x = randf() / 100;
        state->vel[i].y = randf() / 100;
    }
    state->seed = rand();
    update_noise(0.0, 0); /* init noise at t=0 */
    for(int i = 0; i < PLATES; i++) {
        stpx_rgb(ssx(state->plates[i].x), ssy(state->plates[i].y), 255, 255,
                 255);
    }

    float avgd = 0;
    for(size_t i = 0; i < state->points_amount; i++) {
        size_t nxt = (i - 1) % state->points_amount;
        float p1x = state->points[i].x;
        float p1y = state->points[i].y;
        float p2x = state->points[nxt].x;
        float p2y = state->points[nxt].y;
        float d = dis(p1x, p1y, p2x, p2y);
        avgd += (d / (float)state->points_amount);
    }
    state->avgd = avgd;

    sim(avgd);
}

int frame(void)
{
    SDL_Event ev;
    state->current = false;

    static int dont = 1;
    static float t = 0;
    static float d = 0;
    static int recording = 0;
    static int moving = 0;
    static int didsmth = 0;
    static int mode = 0;
    static FFMPEG *ffmpeg = NULL;

    d = 0;

    while(SDL_PollEvent(&ev)) {
        switch(ev.type) {
            /*
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
        */
        case SDL_QUIT:
            state->quit = true;
            return EXIT_FAILURE;
            break;
        case SDL_KEYUP: /*
            if(ev.key.keysym.sym == SDLK_1) {
                mode = 1;
                didsmth = 1;
            } else if(ev.key.keysym.sym == SDLK_2) {
                mode = 0;
                didsmth = 1;
            }*/
            if(ev.key.keysym.sym == SDLK_p) {
                if(!recording) {
                    ffmpeg = ffmpeg_start_rendering(WIDTH, HEIGHT, 24);
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
            } /*
            } else if(ev.key.keysym.sym == SDLK_r) {
                d = -1;
            }*/
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

    if(!dont) {
        sim(state->avgd);
        if(recording)
            ffmpeg_send_frame(ffmpeg, state->pixels, WIDTH, HEIGHT);
    }

    /* controls:
        q:       run
        w:     pause
        e:  forwards
        r: backwards
        p:    record
        space:  save
    */
    /*
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

    */

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

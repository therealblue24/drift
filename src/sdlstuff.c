#include "sdlstuff.h"

void initsdl()
{
	SDL_Init(SDL_INIT_EVERYTHING);
	state->window = SDL_CreateWindow(
		"Drift", SDL_WINDOWPOS_CENTERED_DISPLAY(0),
		SDL_WINDOWPOS_CENTERED_DISPLAY(0), state->width, state->height,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	ASSERT(state->window, "failed to create SDL window!");
	state->renderer = SDL_CreateRenderer(state->window, -1,
										 SDL_RENDERER_ACCELERATED |
											 SDL_RENDERER_PRESENTVSYNC |
											 SDL_RENDERER_TARGETTEXTURE);
	ASSERT(state->renderer, "failed to create SDL renderer!");
	state->texture = SDL_CreateTexture(state->renderer,
									   SDL_PIXELFORMAT_ABGR8888,
									   SDL_TEXTUREACCESS_STREAMING,
									   state->width, state->height);
	ASSERT(state->texture, "failed to create SDL texture!");
}

void sdlrender()
{
	ASSERT(!SDL_UpdateTexture(state->texture, NULL, state->pixels,
							  state->stride),
		   "failed to update SDL texture!: %s", SDL_GetError());
	ASSERT(!SDL_RenderCopyEx(state->renderer, state->texture, NULL, NULL, 0, 0,
							 SDL_FLIP_NONE),
		   "failed to copy SDL texture!");
	SDL_RenderPresent(state->renderer);
}

void deinitsdl()
{
	SDL_DestroyTexture(state->texture);
	SDL_DestroyRenderer(state->renderer);
	SDL_DestroyWindow(state->window);
	SDL_Quit();
}

float SDL_GetMS()
{
	return (float)SDL_GetTicks64() / 1000.f;
}

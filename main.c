#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

#define BALL_RADIUS 30
#define DROPRATE 2000
#define MINLENGTH 10

const float rate = .05;
const float G = 1;

struct SDL_Rect screenrect = { 0, 0, 500, 1000 };

struct SDL_MouseMotionEvent mousestate;
SDL_Renderer *ren;
bool dropball;

struct {
	int x, y;
} dropper = { .x = 50, .y = 50 };

struct ball {
	int x, y;
	float vx, vy;
	struct ball *next;
	struct ball *prev;
};

struct ball *balls_first;
struct ball *balls_last;

struct line {
	SDL_Point start;
	SDL_Point end;
	struct line *next;
	struct line *prev;
};

struct line *lines_first;
struct line *lines_last;

bool ismousedown;
SDL_Point mousedown;

// This is not efficient
void
draw_circle(int x, int y, int radius)
{
	SDL_Point start;
	SDL_Point end;
	Uint8 r, g, b, a;
	SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);

	bool started = false;
	double rx, frac;
	int ix;
	for (int i = -radius; i <= radius; i++) {
		start.y = end.y = i;
		rx = sqrt(radius*radius - i*i);
		ix = sqrt(radius*radius - i*i);
		start.x = -sqrt(radius*radius - i*i);
		end.x = sqrt(radius*radius - i*i);
		frac = rx - ix;

		SDL_SetRenderDrawColor(ren, r, g, b, a);
		SDL_RenderDrawLine(ren, x + start.x, y + start.y, x + end.x, y + end.y);
	
		// antialiasing
		SDL_SetRenderDrawColor(ren, r, g, b, a*frac);
		SDL_RenderDrawPoint(ren, x + start.x-1, y + start.y);
		SDL_RenderDrawPoint(ren, x + start.y, y + start.x-1);
		SDL_RenderDrawPoint(ren, x + end.x+1, y + end.y);
		SDL_RenderDrawPoint(ren, x + end.y, y + end.x+1);
		started = false;
	}
}
#define PF_RGBA32 SDL_PIXELFORMAT_RGBA8888

const SDL_Point zero = {0, 0};

void
draw_line(struct line line)
{
	int w = abs(line.start.x - line.end.x);
	int h = abs(line.start.y - line.end.y);
	int len = sqrt(w*w +h*h);
	double angle = 180*atan(((double) h)/w)/M_PI;

	if (line.start.y > line.end.y)
		angle = -angle;

	if (line.start.x > line.end.x)
		angle = 180 - angle;

	SDL_Rect src = {0, 0, len, 5};
	SDL_Rect dest = {line.start.x, line.start.y, len, 2};
	SDL_Texture *tex = SDL_CreateTexture(ren, PF_RGBA32, SDL_TEXTUREACCESS_TARGET, src.w, src.h);
	if (tex == NULL) {
		SDL_Log("%s warning: failed to create texture", __func__);
		return;
	}

	SDL_SetRenderTarget(ren, tex);
	SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
	SDL_RenderClear(ren);

	SDL_SetRenderTarget(ren, NULL);
	SDL_RenderCopyEx(ren, tex, NULL, &dest, angle, &zero, SDL_FLIP_NONE);
	SDL_DestroyTexture(tex);
}

void *
xcalloc(int size)
{
	void *ptr = calloc(1, size);
	if (ptr == NULL)
		SDL_Quit();

	return ptr;
}

void
ball_add(int x, int y)
{
	if (!balls_last) {
		balls_first = xcalloc(sizeof(*balls_first));
		balls_first->x = x;
		balls_first->y = y;
		balls_last = balls_first;
	} else {
		balls_last->next = xcalloc(sizeof(*balls_first));
		balls_last->next->prev = balls_last;
		balls_last = balls_last->next;
		balls_last->x = x;
		balls_last->y = y;
	}
}

void
line_add(int x1, int y1, int x2, int y2)
{
	if (!lines_last) {
		lines_first = xcalloc(sizeof(*lines_first));
		lines_first->start.x = x1;
		lines_first->start.y = y1;
		lines_first->end.x = x2;
		lines_first->end.y = y2;
		lines_last = lines_first;
	} else {
		lines_last->next = xcalloc(sizeof(*lines_first));
		lines_last->next->prev = lines_last;
		lines_last = lines_last->next;
		lines_last->start.x = x1;
		lines_last->start.y = y1;
		lines_last->end.x = x2;
		lines_last->end.y = y2;
	}
}

void
line_del(struct line *line)
{
	if (line == lines_first) {
		lines_first = lines_first->next;
	} else {
		line->prev->next = line->next;
	}

	if (line == lines_last) {
		lines_last = lines_last->prev;
	} else {
		line->next->prev = line->prev;
	}
	free(line);
}

void
ball_del(struct ball *ball)
{
	if (ball == balls_first) {
		balls_first = balls_first->next;
	} else {
		ball->prev->next = ball->next;
	}

	if (ball == balls_last) {
		balls_last = balls_last->prev;
	} else {
		ball->next->prev = ball->prev;
	}
	free(ball);
}

void
ball_update(struct ball *ball, unsigned int delta)
{
	ball->vy += G*delta*rate;
	ball->x += ball->vx*delta*rate;
	ball->y += ball->vy*delta*rate;
}

SDL_Rect result;

unsigned int
setdropball(unsigned int interval, void *param)
{
	bool *dropball = param;
	*dropball = true;
	return interval;
}


const struct SDL_Rect dropper_srcrect = { 0, 0, 2*BALL_RADIUS + 10 + 2, 2*BALL_RADIUS + 10 + 3};
SDL_Texture *
render_dropper()
{
	int pitch = SDL_BYTESPERPIXEL(PF_RGBA32)*dropper_srcrect.w;
	SDL_Texture *tex = SDL_CreateTexture(ren, PF_RGBA32, SDL_TEXTUREACCESS_STATIC, dropper_srcrect.w, dropper_srcrect.h);
	if (tex == NULL) {
		SDL_Log("%s: couldn't create texture: %s", __func__, SDL_GetError());
		goto err1;
	}

	Uint32 *pixels = calloc(1, dropper_srcrect.w * dropper_srcrect.h * 4);
	if (pixels == NULL) {
		SDL_Log("%s: pixels couldn't be allocated", __func__);
		goto err2;
	}
	
	SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
	draw_circle(BALL_RADIUS + 5, BALL_RADIUS + 5, BALL_RADIUS + 5);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	draw_circle(BALL_RADIUS + 5, BALL_RADIUS + 5, BALL_RADIUS);
	SDL_RenderReadPixels(ren, &dropper_srcrect, PF_RGBA32, pixels, pitch);
	SDL_Log("%08x%08x%08x%08x", pixels[0], pixels[1], pixels[2], pixels[3]);

	SDL_UpdateTexture(tex, &dropper_srcrect, pixels, pitch);
	
	return tex;
	
err3:
	SDL_DestroyTexture(tex);
	tex = NULL;
err2:
	free(pixels);
err1:
	return NULL;
}

SDL_Rect dropper_dstrect;
int
main(int argc, char *argv[])
{
	bool running;
	unsigned int then, now, delta;
	SDL_TimerID balltimer;
	SDL_Rect ballrect;

	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) != 0) {
		SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
		return 1;
	}

	SDL_Window *win = SDL_CreateWindow("Soundpong", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screenrect.w, screenrect.h, SDL_WINDOW_RESIZABLE);
	if (win == NULL) {
		SDL_Log("Unable to create window: %s", SDL_GetError());
		goto err1;
	}
	
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (ren == NULL) {
		SDL_Log("Unable to create renderer: %s", SDL_GetError());
		goto err2;
	}
	
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
	
	SDL_Texture *dropper_texture = render_dropper();
	if (dropper_texture == NULL) {
		SDL_Log("Unable to create dropper texture: %s", SDL_GetError());
		goto err3;
	}
	SDL_SetTextureBlendMode(dropper_texture, SDL_BLENDMODE_BLEND);

	dropper_dstrect = (SDL_Rect){ dropper.x - (BALL_RADIUS + 5), dropper.y - (BALL_RADIUS + 5), 2*BALL_RADIUS+10, 2*BALL_RADIUS+10 };
	balltimer = SDL_AddTimer(DROPRATE, setdropball, &dropball);

	running = true;
	SDL_Event e;
	while (running) {
		while(SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_MOUSEMOTION:
				mousestate = e.motion;
				break;
			case SDL_MOUSEBUTTONDOWN:
				ismousedown = true;
				mousedown.x = e.button.x;
				mousedown.y = e.button.y;
				break;
			case SDL_MOUSEBUTTONUP:
				ismousedown = false;
				if ((mousedown.x - e.button.x)*(mousedown.x - e.button.x) + (mousedown.y - e.button.y)*(mousedown.y - e.button.y) > MINLENGTH*MINLENGTH)
					line_add(mousedown.x, mousedown.y, e.button.x, e.button.y);
				break;
			case SDL_QUIT:
				running = false;
			case SDL_WINDOWEVENT: {
				switch (e.window.event) {
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				case SDL_WINDOWEVENT_RESIZED:
					screenrect.w = e.window.data1;
					screenrect.h = e.window.data2;
				}
				}
				break;
			}
		}

		if (dropball) {
			dropball = false;
			ball_add(dropper.x, dropper.y);
		}

		now = SDL_GetTicks();
		delta = now - then;
		if (delta < 5)
			continue;

		then = now;
		
		/* update state */
		for (struct ball *ball = balls_first; ball != NULL; ball = ball->next) {
			ballrect = (struct SDL_Rect){ball->x - BALL_RADIUS, ball->y - BALL_RADIUS, ball->x + BALL_RADIUS, ball->y + BALL_RADIUS};
			if (!SDL_HasIntersection(&ballrect, &screenrect)) {
				ball_del(ball);
				continue;
			}

			ball_update(ball, delta);
		}

		/* render */
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);

		SDL_RenderCopy(ren, dropper_texture, &dropper_srcrect, &dropper_dstrect);

		SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);

		for (struct line *line = lines_first; line != NULL; line = line->next) {
			draw_line(*line);
		}

		if (ismousedown)
			SDL_RenderDrawLine(ren, mousedown.x, mousedown.y, e.button.x, e.button.y);

		for (struct ball *ball = balls_first; ball != NULL; ball = ball->next) {
			draw_circle(ball->x, ball->y, BALL_RADIUS);
		}

		SDL_RenderPresent(ren);
	}

	SDL_RemoveTimer(balltimer);

	SDL_DestroyTexture(dropper_texture);
err3:
	SDL_DestroyRenderer(ren);
err2:
	SDL_DestroyWindow(win);
err1:
	SDL_Quit();
}

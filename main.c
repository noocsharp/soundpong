#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

#define BALL_RADIUS 30
#define DROPRATE 2000

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

void
draw_circle_aa(Uint32 *pixels, int w, int h, int radius)
{
	double x, y, frac;
	int xi, yi;
	int al1, al2;
	int ffd = round(radius / sqrt(2));
	for (int i = 0; i <= ffd; i++) {
		y = sqrt(radius*radius - i*i);
		yi = y;
		frac = y - floor(y);
		al2 = 0xff*frac;
		al1 = 0xff*(1-frac);
		pixels[(yi+h/2)*w + i+w/2] = 0xFFFFFF00 + al1;
		pixels[(yi+h/2+1)*w + i+w/2] = 0xFFFFFF00 + al2;
		pixels[(h/2-yi)*w + i+w/2] = 0xFFFFFF00 + al1;
		pixels[(h/2-yi-1)*w + i+w/2] = 0xFFFFFF00 + al2;
		pixels[(h/2-yi)*w + w/2 - i] = 0xFFFFFF00 + al1;
		pixels[(h/2-yi-1)*w + w/2 - i] = 0xFFFFFF00 + al2;
		pixels[(h/2+yi)*w + w/2 - i] = 0xFFFFFF00 + al1;
		pixels[(h/2+yi+1)*w + w/2 - i] = 0xFFFFFF00 + al2;
	}
	for (int i = 0; i <= ffd; i++) {
		x = sqrt(radius*radius - i*i);
		xi = x;
		frac = x - floor(x);
		al1 = 0xff*frac;
		al2 = 0xff*(1-frac);
		pixels[(i+h/2)*w + w/2 + xi] = 0xFFFFFF00 + al2;
		pixels[(i+h/2)*w + w/2 + xi+1] = 0xFFFFFF00 + al1;
		pixels[(h/2-i)*w + w/2 + xi] = 0xFFFFFF00 + al2;
		pixels[(h/2-i)*w + w/2 + xi+1] = 0xFFFFFF00 + al1;
		pixels[(h/2-i)*w + w/2 - xi] = 0xFFFFFF00 + al2;
		pixels[(h/2-i)*w + w/2 - xi-1] = 0xFFFFFF00 + al1;
		pixels[(h/2+i)*w + w/2 - xi] = 0xFFFFFF00 + al2;
		pixels[(h/2+i)*w + w/2 - xi-1] = 0xFFFFFF00 + al1;
	}
}

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
		SDL_Log("start, end: %d, %d", start.x, end.x);
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
	SDL_Log("%s", __func__);
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
	SDL_Log("%s", __func__);
	if (!lines_last) {
		lines_first = xcalloc(sizeof(*lines_first));
		lines_first->start.x = x1;
		lines_first->start.y = y1;
		lines_first->start.x = x2;
		lines_first->start.y = y2;
		lines_last = lines_first;
	} else {
		lines_last->next = xcalloc(sizeof(*lines_first));
		lines_last->next->prev = lines_last;
		lines_last = lines_last->next;
		lines_last->start.x = x1;
		lines_last->start.y = y1;
		lines_last->start.x = x2;
		lines_last->start.y = y2;
	}
}

void
line_del(struct line *line)
{
	SDL_Log("%s", __func__);
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
	SDL_Log("%s", __func__);
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

// averages RGBA8888 colors
Uint32
avg4(Uint32 a, Uint32 b, Uint32 c, Uint32 d)
{
	Uint8 *ac = (Uint8 *) &a;
	Uint8 *bc = (Uint8 *) &b;
	Uint8 *cc = (Uint8 *) &c;
	Uint8 *dc = (Uint8 *) &d;
	
	Uint8 red = (ac[0] + bc[0] + cc[0] + dc[0]) / 4;
	Uint8 green = (ac[1] + bc[1] + cc[1] + dc[1]) / 4;
	Uint8 blue = (ac[2] + bc[2] + cc[2] + dc[2]) / 4;
	Uint8 alpha = (ac[3] + bc[3] + cc[3] + dc[3]) / 4;
	
	return (alpha << 24) + (blue << 16) + (green << 8) + red;
}

Uint32
avg6(Uint32 a, Uint32 b, Uint32 c, Uint32 d, Uint32 e, Uint32 f)
{
	Uint8 *ac = (Uint8 *) &a;
	Uint8 *bc = (Uint8 *) &b;
	Uint8 *cc = (Uint8 *) &c;
	Uint8 *dc = (Uint8 *) &d;
	Uint8 *ec = (Uint8 *) &e;
	Uint8 *fc = (Uint8 *) &f;
	
	Uint8 red = (ac[0] + bc[0] + cc[0] + dc[0] + ec[0] + fc[0]) / 6;
	Uint8 green = (ac[1] + bc[1] + cc[1] + dc[1] + ec[1] + fc[1]) / 6;
	Uint8 blue = (ac[2] + bc[2] + cc[2] + dc[2] + ec[2] + fc[2]) / 6;
	Uint8 alpha = (ac[3] + bc[3] + cc[3] + dc[3] + ec[3] + fc[3]) / 6;
	
	return (alpha << 24) + (blue << 16) + (green << 8) + red;
}

Uint32
avg9(Uint32 a, Uint32 b, Uint32 c, Uint32 d, Uint32 e, Uint32 f, Uint32 g, Uint32 h, Uint32 i)
{
	Uint8 *ac = (Uint8 *) &a;
	Uint8 *bc = (Uint8 *) &b;
	Uint8 *cc = (Uint8 *) &c;
	Uint8 *dc = (Uint8 *) &d;
	Uint8 *ec = (Uint8 *) &e;
	Uint8 *fc = (Uint8 *) &f;
	Uint8 *gc = (Uint8 *) &g;
	Uint8 *hc = (Uint8 *) &h;
	Uint8 *ic = (Uint8 *) &i;
	
	Uint8 red = (ac[0] + bc[0] + cc[0] + dc[0] + ec[0] + fc[0] + gc[0] + hc[0] + ic[0]) / 9;
	Uint8 green = (ac[1] + bc[1] + cc[1] + dc[1] + ec[1] + fc[1] + gc[1] + hc[1] + ic[1]) / 9;
	Uint8 blue = (ac[2] + bc[2] + cc[2] + dc[2] + ec[2] + fc[2] + gc[2] + hc[2] + ic[2]) / 9;
	Uint8 alpha = (ac[3] + bc[3] + cc[3] + dc[3] + ec[3] + fc[3] + gc[3] + hc[3] + ic[3]) / 9;
	
	return (alpha << 24) + (blue << 16) + (green << 8) + red;
}

// assumes RGBA8888
Uint32 *
smooth(Uint32 *old, int w, int h)
{
	int p = w*4;
	SDL_Log("size: %d", h*p);
	Uint32 *new = calloc(1, h*p);
	if (new == NULL)
		return NULL;
	// we could use a rolling thing for efficiency
	// corners
	new[0]           = avg4(old[0], old[1], old[w], old[w+1]);
	new[w-1]         = avg4(old[w-1], old[w-2], old[w+w-1], old[w+w-2]);
	new[w*(h-1)]     = avg4(old[w*(h-1)], old[w*(h-1)+1], old[w*(h-2)], old[w*(h-2)+1]);
	new[w*(h-1)+w-1] = avg4(old[w*(h-1)], old[w*(h-1)+1], old[w*(h-2)], old[w*(h-2)+1]);

	// tow and bottom edges
	for (int i = 1; i < w - 1; i++) {
		new[i] = avg6(old[i-1], old[i], old[i+1], old[w+i-1], old[w+i], old[w+i+1]);
		new[w*(h-1)+i] = avg6(old[w*(h-1)+i-1], old[w*(h-1)+i], old[w*(h-1)+i+1], old[w*(h-2)+i-1], old[w*(h-2)+i], old[w*(h-2)+i+1]);
	}

	// left and right edges
	for (int i = 1; i < h - 1; i++) {
		new[w*i] = avg6(old[w*(i-1)], old[w*i], old[w*(i+1)], old[1+w*(i-1)], old[1+w*i], old[1+w*(i+1)]);
		new[w-1+w*i] = avg6(old[w*(i-1)+w-1], old[w*i+w-1], old[w*(i+1)+w-1], old[w*(i-1)+w-2], old[w*i+w-2], old[w*(i+1)+w-2]);
	}
	
	// middle
	for (int i = 1; i < w - 1; i++) {
		for (int j = 1; j < h - 1; j++) {
			new[w*j+i] = avg9(old[w*(j-1)+i-1], old[w*(j-1)+i], old[w*(j-1)+i+1],
                              old[w*j+i-1],     old[w*j+i],     old[w*j+i+1],
                              old[w*(j+1)+i-1], old[w*(j+1)+i], old[w*(j+1)+i+1]);
		}
	}

	free(old);
	return new;
}

#define PF_RGBA32 SDL_PIXELFORMAT_RGBA8888

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
	
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
	draw_circle(BALL_RADIUS + 5, BALL_RADIUS + 5, BALL_RADIUS + 4);
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

	dropper_dstrect = (SDL_Rect){ dropper.x - (BALL_RADIUS + 5), dropper.y - (BALL_RADIUS + 5), 2*BALL_RADIUS+10 + 1, 2*BALL_RADIUS+10 + 1 };
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
	
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		
		SDL_RenderCopy(ren, dropper_texture, &dropper_srcrect, &dropper_dstrect);
		
		SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
		for (struct ball *ball = balls_first; ball != NULL; ball = ball->next) {
			ballrect = (struct SDL_Rect){ball->x - BALL_RADIUS, ball->y - BALL_RADIUS, ball->x + BALL_RADIUS, ball->y + BALL_RADIUS};
			if (!SDL_HasIntersection(&ballrect, &screenrect)) {
				ball_del(ball);
				continue;
			}
			
			ball_update(ball, delta);
			
			//draw_circle(ball->x, ball->y, BALL_RADIUS);
		}
		
		if (ismousedown)
			SDL_RenderDrawLine(ren, mousedown.x, mousedown.y, mousestate.x, mousestate.y);

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

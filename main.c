#include <fluidsynth.h>
#include <SDL2/SDL.h>
#include <stdbool.h>
#include "SDL2_gfxPrimitives.h"
#include <stdio.h>

#define BALL_RADIUS 11
#define DROPRATE 2000
#define MINLENGTH 20

#define LOWEST 45
#define HIGHEST 100

const float rate = .01;
const float G = 1;

struct SDL_Rect screenrect = { 0, 0, 500, 1000 };

struct SDL_MouseMotionEvent mousestate;
SDL_Renderer *ren;
bool dropball;

fluid_synth_t *fsynth;

SDL_Point dropper = { .x = 100, .y = 100 };
SDL_Point old_dropper;

struct ball {
	float x, y;
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

SDL_Point *selected;

struct line *lines_first;
struct line *lines_last;

bool ismousedown;
SDL_Point mousedown;

#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define BETWEEN(x, a, b) (((x) <= MAX((a), (b))) && (x >= MIN((a), (b))))
#define INRADIUS(a, b, c) ((a)*(a) + (b)*(b) <= (c)*(c))

SDL_Point
ball_intersects_line(struct ball *ball, struct line *line)
{
	/* we calculate the distance from  the center of the ball to the line*/
	float a = -(line->end.y - line->start.y);
	float b = line->end.x - line->start.x;
	float c = line->start.x*(line->end.y - line->start.y) - line->start.y*(line->end.x - line->start.x);
	float x = (b*(b*ball->x - a*ball->y) - a*c) / (a*a + b*b);
	float y = (a*(-b*ball->x + a*ball->y) - b*c) / (a*a + b*b);
	float dist = fabsf(a*ball->x + b*ball->y + c) / sqrtf(a*a+b*b);

	/* if the distance between the center of the circle and the intersection is less
	 * than the radius, then the line and circle intersect AND the intersection is on
	 * the finite segment */
	if (BETWEEN(x, line->start.x, line->end.x) && BETWEEN(y, line->start.y, line->end.y)) {
		if (dist <= BALL_RADIUS)
			return (SDL_Point){x, y};
	} else { // otherwise check if the endpoints intersect
		if (INRADIUS(ball->x - line->start.x, ball->y - line->start.y, BALL_RADIUS))
			return (SDL_Point){line->start.x, line->start.y};
		if (INRADIUS(ball->x - line->end.x, ball->y - line->end.y, BALL_RADIUS))
			return (SDL_Point){line->end.x, line->end.y};
	}

	return (SDL_Point){-1, -1};
}

#define DEG(x) (180*((x)/M_PI))

void
play_vec(float vx, float vy)
{
	int val = sqrt(vx*vx + vy*vy) / 2 + LOWEST;
	if (fluid_synth_noteon(fsynth, 0, val, 50) == FLUID_FAILED)
	fluid_synth_noteoff(fsynth, 0, val);
}

bool
ball_bounce(struct ball *ball, struct line *line)
{
	float vx = ball->vx, vy = ball->vy;
	SDL_Point i = ball_intersects_line(ball, line);
	if (i.x < 0 && i.y < 0)
		return false;

	play_vec(vx, vy);

	// if we intersect and endpoint, just bounce in the opposite direction
	if ((i.x == line->start.x && i.y == line->start.y) || (i.x == line->end.x && i.y == line->end.y)) {
		ball->vx = -ball->vx;
		ball->vy = -ball->vy;
		return true;
	}

	float dy = line->end.y - line->start.y;
	float dx = line->end.x - line->start.x;
	float nx = dy/sqrt(dx*dx + dy*dy), ny = -dx/sqrt(dx*dx + dy*dy);
	float coef = (vx * nx + vy * ny) / (nx*nx + ny*ny);
	float ux = coef * nx;
	float uy = coef * ny;
	float wx = vx - ux;
	float wy = vy - uy;
	ball->vx = wx - ux;
	ball->vy = wy - uy;
	return true;
}

#define PF_RGBA32 SDL_PIXELFORMAT_RGBA8888

const SDL_Point zero = {0, 0};

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
	if (ball == NULL)
		return;
	if (ball->next == NULL && ball->prev == NULL) {
		balls_first = balls_last = NULL;
		free(ball);
		return;
	}

	if (ball == balls_first) {
		balls_first = balls_first->next;
		balls_first->prev = NULL;
	} else {
		ball->prev->next = ball->next;
	}

	if (ball == balls_last) {
		balls_last = balls_last->prev;
		balls_last->next = NULL;
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

int
main(int argc, char *argv[])
{
	bool running;
	unsigned int then, now, delta;
	SDL_TimerID balltimer;
	SDL_Rect ballrect;
	fluid_settings_t *fsettings;
	fluid_audio_driver_t *fadriver;
	int sfid;
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_AUDIO) != 0) {
		SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
		return 1;
	}

	SDL_Window *win = SDL_CreateWindow("Soundpong", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screenrect.w, screenrect.h, SDL_WINDOW_RESIZABLE);
	if (win == NULL) {
		SDL_Log("Unable to create window: %s", SDL_GetError());
		goto err1;
	}

	fsettings = new_fluid_settings();
	if (!fsettings) {
		SDL_Log("Unable to create fluid settings");
		goto err2;
	}

	fsynth = new_fluid_synth(fsettings);
	if (!fsynth) {
		SDL_Log("Unable to create fluid synth");
		goto err3;
	}
	
	if ((sfid = fluid_synth_sfload(fsynth, "/home/nihal/instruments/soundfonts/free/Xylophone-MediumMallets-SF2-20200706/Xylophone-MediumMallets-20200706.sf2", true)) == FLUID_FAILED) {
		SDL_Log("Unable to load soundfont");
		goto err4;
	}

	fluid_sfont_t *sfont = fluid_synth_get_sfont_by_id(fsynth, sfid);
	if (sfont == NULL) {
		SDL_Log("coudn't load sfont by id");
		return 1;
	}

	fluid_sfont_iteration_start(sfont);
	fluid_preset_t *fpreset = fluid_sfont_iteration_next(sfont);
	int bank = fluid_preset_get_banknum(fpreset);
	int prog = fluid_preset_get_num(fpreset);
	fluid_synth_activate_tuning(fsynth, 0, bank, prog, true);

	fluid_settings_setstr(fsettings, "audio.driver", "sdl2");
	fadriver = new_fluid_audio_driver(fsettings, fsynth);
	if (!fadriver) {
		SDL_Log("Unable to create fluid synth driver");
		goto err5;
	}

	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (ren == NULL) {
		SDL_Log("Unable to create renderer: %s", SDL_GetError());
		goto err6;
	}
	
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

	balltimer = SDL_AddTimer(DROPRATE, setdropball, &dropball);

	then = SDL_GetTicks();
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
				if (selected) {
					selected->x = e.button.x;
					selected->y = e.button.y;
					selected = NULL;
				} else if (!INRADIUS(mousedown.x - e.button.x, mousedown.y - e.button.y, MINLENGTH)) {
					line_add(mousedown.x, mousedown.y, e.button.x, e.button.y);
				}
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

			for (struct line *line = lines_first; line != NULL; line = line->next) {
				if (ball_bounce(ball, line)) // returns true if it has bounced, can only bounce off of one line.
					break;
			}

			ball_update(ball, delta);
		}

		/* render */
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);

		if (ismousedown && !selected && INRADIUS(dropper.x - mousedown.x, dropper.y - mousedown.y, BALL_RADIUS + 5)) {
			selected = &dropper;
			old_dropper.x = dropper.x;
			old_dropper.y = dropper.y;
			dropper.x = e.button.x;
			dropper.y = e.button.y;
		} else if (ismousedown && selected == &dropper && INRADIUS(old_dropper.x - mousedown.x, old_dropper.y - mousedown.y, BALL_RADIUS + 5)) {
			dropper.x = e.button.x;
			dropper.y = e.button.y;
		}

		aaFilledEllipseColor(ren, dropper.x, dropper.y, BALL_RADIUS + 5, BALL_RADIUS + 5, 0xFFFFFFFF);
		aaFilledEllipseColor(ren, dropper.x, dropper.y, BALL_RADIUS, BALL_RADIUS, 0xFF000000);

		SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);

		for (struct line *line = lines_first; line != NULL; line = line->next) {
			if (ismousedown) {
				if (INRADIUS(line->start.x - mousedown.x, line->start.y - mousedown.y, 5)) {
					selected = &line->start;
				} else if (INRADIUS(line->end.x - mousedown.x, line->end.y - mousedown.y, 5)) {
					selected = &line->end;
				}
			}

			if (selected == &line->start) {
				line->start.x = e.button.x;
				line->start.y = e.button.y;
			} else if (selected == &line->end) {
				line->end.x = e.button.x;
				line->end.y = e.button.y;
			}
			thickLineColor(ren, line->start.x, line->start.y, line->end.x, line->end.y, 3, 0xFFFFFFFF);
		}

		if (ismousedown && !selected) {
			thickLineColor(ren, mousedown.x, mousedown.y, e.button.x, e.button.y, 3, 0xFFFFFFFF);
		}

		for (struct ball *ball = balls_first; ball != NULL; ball = ball->next) {
			aaFilledEllipseColor(ren, ball->x, ball->y, BALL_RADIUS, BALL_RADIUS, 0xFFFFFFFF);
		}

		SDL_RenderPresent(ren);
	}

	SDL_RemoveTimer(balltimer);

err7:
	SDL_DestroyRenderer(ren);
err6:
	delete_fluid_audio_driver(fadriver);
err5:
	fluid_synth_sfunload(fsynth, sfid, true);
err4:
	delete_fluid_synth(fsynth);
err3:
	delete_fluid_settings(fsettings);
err2:
	SDL_DestroyWindow(win);
err1:
	SDL_Quit();
}

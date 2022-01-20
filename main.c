#ifdef SOUND
#include <fluidsynth.h>
#endif

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

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

#ifdef SOUND
fluid_synth_t *fsynth;
#endif

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
struct line *selected_line;

struct line *lines_first;
struct line *lines_last;

bool ismousedown;
SDL_Point mousedown;
SDL_Point mousepos;

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
#ifdef SOUND
	if (fluid_synth_noteon(fsynth, 0, val, 50) == FLUID_FAILED)
	fluid_synth_noteoff(fsynth, 0, val);
#endif
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

bool running;
unsigned int then, now, delta;
SDL_TimerID balltimer;
SDL_Rect ballrect;

#ifdef SOUND
	fluid_settings_t *fsettings;
	fluid_audio_driver_t *fadriver;
#endif
	int sfid;
void
loop()
{
	SDL_Event e;
	while(SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_MOUSEMOTION:
			mousestate = e.motion;
			mousepos.x = e.motion.x;
			mousepos.y = e.motion.y;
			break;
		case SDL_MOUSEBUTTONDOWN:
			ismousedown = true;
			mousedown.x = e.button.x;
			mousedown.y = e.button.y;
			mousepos.x = e.button.x;
			mousepos.y = e.button.y;
			break;
		case SDL_MOUSEBUTTONUP:
			ismousedown = false;
			mousepos.x = e.button.x;
			mousepos.y = e.button.y;
			if (selected) {
				if (INRADIUS(selected_line->start.x - selected_line->end.x, selected_line->start.y - selected_line->end.y, MINLENGTH)) {
					line_del(selected_line);
					selected = NULL;
				} else {
					selected->x = e.button.x;
					selected->y = e.button.y;
					selected = NULL;
				}
				selected_line = NULL;
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
		return;

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

	if (ismousedown) {
		if (!selected && INRADIUS(dropper.x - mousedown.x, dropper.y - mousedown.y, BALL_RADIUS + 5)) {
			selected = &dropper;
			old_dropper.x = dropper.x;
			old_dropper.y = dropper.y;
			dropper.x = mousepos.x;
			dropper.y = mousepos.y;
		} else if (selected == &dropper && INRADIUS(old_dropper.x - mousedown.x, old_dropper.y - mousedown.y, BALL_RADIUS + 5)) {
			dropper.x = mousepos.x;
			dropper.y = mousepos.y;
		}
	}

	aaFilledEllipseColor(ren, dropper.x, dropper.y, BALL_RADIUS + 5, BALL_RADIUS + 5, 0xFFFFFFFF);
	aaFilledEllipseColor(ren, dropper.x, dropper.y, BALL_RADIUS, BALL_RADIUS, 0xFF000000);

	SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);

	for (struct line *line = lines_first; line != NULL; line = line->next) {
		if (INRADIUS(line->start.x - mousepos.x, line->start.y - mousepos.y, 5)) {
			aaFilledEllipseColor(ren, line->start.x, line->start.y, 5, 5, 0x80FFFFFF);
		} else if (INRADIUS(line->end.x - mousepos.x, line->end.y - mousepos.y, 5)) {
			aaFilledEllipseColor(ren, line->end.x, line->end.y, 5, 5, 0x80FFFFFF);
		}

		if (ismousedown) {
			if (INRADIUS(line->start.x - mousedown.x, line->start.y - mousedown.y, 5)) {
				selected = &line->start;
			} else if (INRADIUS(line->end.x - mousedown.x, line->end.y - mousedown.y, 5)) {
				selected = &line->end;
			}
		}

		if (selected == &line->start) {
			line->start.x = mousepos.x;
			line->start.y = mousepos.y;
			selected_line = line;
		} else if (selected == &line->end) {
			line->end.x = mousepos.x;
			line->end.y = mousepos.y;
			selected_line = line;
		}
		thickLineColor(ren, line->start.x, line->start.y, line->end.x, line->end.y, 3, 0xFFFFFFFF);
	}

	if (ismousedown && !selected) {
		thickLineColor(ren, mousedown.x, mousedown.y, mousepos.x, mousepos.y, 3, 0xFFFFFFFF);
	}

	for (struct ball *ball = balls_first; ball != NULL; ball = ball->next) {
		aaFilledEllipseColor(ren, ball->x, ball->y, BALL_RADIUS, BALL_RADIUS, 0xFFFFFFFF);
	}

	SDL_RenderPresent(ren);
}

int
main(int argc, char *argv[])
{
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

#ifdef SOUND
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

	if ((sfid = fluid_synth_sfload(fsynth, "assets/Xylophone-MediumMallets-20200706.sf2", true)) == FLUID_FAILED) {
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
#endif

	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (ren == NULL) {
		SDL_Log("Unable to create renderer: %s", SDL_GetError());
		goto err6;
	}

	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

	balltimer = SDL_AddTimer(DROPRATE, setdropball, &dropball);

	then = SDL_GetTicks();
	running = true;

#ifdef EMSCRIPTEN
	emscripten_set_main_loop_arg(&loop, NULL, 0, 1);
#else
	while (running) {
		loop();
	}
#endif

	SDL_RemoveTimer(balltimer);

err7:
	SDL_DestroyRenderer(ren);
err6:
#ifdef SOUND
	delete_fluid_audio_driver(fadriver);
err5:
	fluid_synth_sfunload(fsynth, sfid, true);
err4:
	delete_fluid_synth(fsynth);
err3:
	delete_fluid_settings(fsettings);
err2:
#endif
	SDL_DestroyWindow(win);
err1:
	SDL_Quit();
}

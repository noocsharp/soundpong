SRC = main.c SDL2_gfxPrimitives.c SDL2_rotozoom.c
OBJ = $(SRC:%.c=%.o)
EXE = pong
LIBS = -lm -lfluidsynth -lSDL2

all: $(OBJ)
	$(CC) -g -o $(EXE) $(OBJ) $(LIBS)

web:
	emcc -O2 -I/home/nihal/fluidsynth/include -I/home/nihal/fluidsynth/build/include -DSOUND main.c  SDL2_gfxPrimitives.c libfluidsynth.a -s USE_SDL=2 --preload-file assets -o soundpong.html --shell-file minimal_shell.html

.c.o:
	$(CC) -DSOUND -g $< -c

clean:
	rm -rf $(OBJ) $(EXE)

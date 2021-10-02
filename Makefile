SRC = main.c SDL2_gfxPrimitives.c SDL2_rotozoom.c
OBJ = $(SRC:%.c=%.o)
EXE = pong
LIBS = -lSDL2 -lm -lfluidsynth -lSDL2_gfx

all: $(OBJ)
	$(CC) -g -o $(EXE) $(OBJ) $(LIBS)

.c.o:
	$(CC) -g $< -c

clean:
	rm -rf $(OBJS) $(EXE)

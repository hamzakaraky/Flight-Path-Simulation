CC     = gcc
CFLAGS = -Wall -Wextra -std=c99

# Detect OS automatically
ifeq ($(OS),Windows_NT)
    CC_SIM = /c/msys64/usr/bin/gcc
    IFLAGS = -I/c/msys64/ucrt64/include
    LFLAGS = -L/c/msys64/ucrt64/lib
    LIBS   = -lraylib -lwinmm -lgdi32 -lm -lpthread
    BIN    = sim.exe
else
    CC_SIM = gcc
    IFLAGS =
    LFLAGS =
    LIBS   = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
    BIN    = sim
endif

milestone1:
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra

milestone2:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) sim.c -o $(BIN) $(LIBS)

milestone3:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) sim.c -o $(BIN) $(LIBS)

milestone4:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) -DMS4 sim.c -o $(BIN) $(LIBS)

milestone5:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) -DMS5 sim.c -o $(BIN) $(LIBS)

milestone6:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) -DMS6 sim.c -o $(BIN) $(LIBS)

clean:
	rm -f dijkstra sim sim.exe *.o

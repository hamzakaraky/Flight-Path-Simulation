# M1: any gcc works (pure C, no POSIX, no Raylib)
CC      = gcc
# M2-M6: sim.c uses POSIX APIs (fork, pipe, mmap, semaphores) that only
#         the MSYS2 POSIX gcc (usr/bin) supports on Windows.
#         Raylib headers/libs are taken from the UCRT64 package.
CC_SIM  = /c/msys64/usr/bin/gcc
CFLAGS  = -Wall -Wextra -std=c99
IFLAGS  = -I/c/msys64/ucrt64/include
LFLAGS  = -L/c/msys64/ucrt64/lib
LIBS    = -lraylib -lwinmm -lgdi32 -lm -lpthread

all: milestone4

# المراحل القديمة
milestone1:
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra
milestone2:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) sim.c -o sim.exe $(LIBS)
milestone3:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) sim.c -o sim.exe $(LIBS)

# المراحل الجديدة (تطلب بناء نفس البرنامج ولكن بتفعيل خصائص الـ OS)
milestone4:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) -DMS4 sim.c -o sim.exe $(LIBS)
milestone5:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) -DMS5 sim.c -o sim.exe $(LIBS)
milestone6:
	$(CC_SIM) $(CFLAGS) $(IFLAGS) $(LFLAGS) -DMS6 sim.c -o sim.exe $(LIBS)

clean:
	rm -f dijkstra sim sim.exe sim4 sim5 sim6 *.o
CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LIBS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

all: milestone4

# المراحل القديمة بعد التعديل والتنظيف
milestone1:
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra
milestone2:
	$(CC) $(CFLAGS) sim.c -o sim $(LIBS)
milestone3:
	$(CC) $(CFLAGS) sim.c -o sim $(LIBS)

# المراحل الجديدة (تطلب بناء نفس البرنامج ولكن بتفعيل خصائص الـ OS)
milestone4:
	$(CC) $(CFLAGS) -DMS4 sim.c -o sim $(LIBS)
milestone5:
	$(CC) $(CFLAGS) -DMS5 sim.c -o sim $(LIBS)
milestone6:
	$(CC) $(CFLAGS) -DMS6 sim.c -o sim $(LIBS)

clean:
	rm -f dijkstra sim sim4 sim5 sim6 *.o
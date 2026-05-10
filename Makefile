CC = gcc
CFLAGS = -Wall -Wextra -std=c99

milestone1:
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra

milestone2:
	$(CC) $(CFLAGS) graph_gui_.c -o sim -lraylib -lm

milestone3:
	$(CC) $(CFLAGS) sim.c -o sim -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

clean:
	rm -f dijkstra sim *.o
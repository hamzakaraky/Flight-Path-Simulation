CC = gcc
CFLAGS = -Wall -Wextra -std=c99

milestone1:
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra

clean:
	rm -f dijkstra sim *.o
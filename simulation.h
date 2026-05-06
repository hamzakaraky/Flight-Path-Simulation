#ifndef SIMULATION_H
#define SIMULATION_H

typedef struct {
    int id;
    float x, y;
} Node;

typedef struct {
    int src, dest;
    int weight;
} Edge;


void run_simulation(Node path[], int path_size, Edge edges[], int edges_count);

#endif
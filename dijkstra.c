#include <stdio.h>
#include <stdlib.h>

#define INF 1000000000

int minDistance(int dist[], int visited[], int n) {
    int min = INF;
    int minIndex = -1;

    for (int i = 0; i < n; i++) {
        if (!visited[i] && dist[i] < min) {
            min = dist[i];
            minIndex = i;
        }
    }

    return minIndex;
}

void printPath(int parent[], int node) {
    if (parent[node] == -1) {
        printf("%d", node);
        return;
    }

    printPath(parent, parent[node]);
    printf(" -> %d", node);
}

void freeGraph(int **graph, int n) {
    if (graph == NULL) {
        return;
    }

    for (int i = 0; i < n; i++) {
        free(graph[i]);
    }

    free(graph);
}

void runDijkstra(int **graph, int n, int source, int destination) {
    int *dist = malloc(n * sizeof(int));
    int *visited = malloc(n * sizeof(int));
    int *parent = malloc(n * sizeof(int));

    if (dist == NULL || visited == NULL || parent == NULL) {
        printf("Memory allocation failed\n");
        free(dist);
        free(visited);
        free(parent);
        return;
    }

    for (int i = 0; i < n; i++) {
        dist[i] = INF;
        visited[i] = 0;
        parent[i] = -1;
    }

    dist[source] = 0;

    for (int count = 0; count < n; count++) {
        int u = minDistance(dist, visited, n);

        if (u == -1) {
            break;
        }

        visited[u] = 1;

        for (int v = 0; v < n; v++) {
            if (!visited[v] &&
                graph[u][v] != INF &&
                dist[u] != INF &&
                dist[u] + graph[u][v] < dist[v]) {

                dist[v] = dist[u] + graph[u][v];
                parent[v] = u;
            }
        }
    }

    if (dist[destination] == INF) {
        printf("No path found\n");
    } else {
        printPath(parent, destination);
        printf("\n%d\n", dist[destination]);
    }

    free(dist);
    free(visited);
    free(parent);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: ./dijkstra <file_name>\n");
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        printf("Error opening file\n");
        return 1;
    }

    int n, m;

    if (fscanf(file, "%d %d", &n, &m) != 2) {
        printf("Invalid input\n");
        fclose(file);
        return 1;
    }

    if (n <= 0 || m < 0) {
        printf("Invalid input\n");
        fclose(file);
        return 1;
    }

    int **graph = malloc(n * sizeof(int *));
    if (graph == NULL) {
        printf("Memory allocation failed\n");
        fclose(file);
        return 1;
    }

    for (int i = 0; i < n; i++) {
        graph[i] = malloc(n * sizeof(int));
        if (graph[i] == NULL) {
            printf("Memory allocation failed\n");
            freeGraph(graph, i);
            fclose(file);
            return 1;
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            graph[i][j] = INF;
        }
    }

    for (int i = 0; i < m; i++) {
        int src, dst, weight;

        if (fscanf(file, "%d %d %d", &src, &dst, &weight) != 3) {
            printf("Invalid input\n");
            freeGraph(graph, n);
            fclose(file);
            return 1;
        }

        if (src < 0 || dst < 0 || weight < 0 || src >= n || dst >= n) {
            printf("Invalid input\n");
            freeGraph(graph, n);
            fclose(file);
            return 1;
        }

        graph[src][dst] = weight;
    }

    int source, destination;

    if (fscanf(file, "%d %d", &source, &destination) != 2) {
        printf("Invalid input\n");
        freeGraph(graph, n);
        fclose(file);
        return 1;
    }

    if (source < 0 || destination < 0 || source >= n || destination >= n) {
        printf("Invalid input\n");
        freeGraph(graph, n);
        fclose(file);
        return 1;
    }

    if (source == destination) {
        printf("%d\n0\n", source);
    } else {
        runDijkstra(graph, n, source, destination);
    }

    freeGraph(graph, n);
    fclose(file);

    return 0;
}
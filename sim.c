#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <raylib.h>

#define INF 1000000000
#define MAX_NODES 50
#define MAX_TRAVELERS 20
#define SCREEN_W 1280
#define SCREEN_H 800
#define NODE_RADIUS 26
#define ARROW_HEAD 12
#define JUMP_TIME 0.3f
#define WAIT_TIME 1.0f
#define PI 3.14159265358979323846f

/* =====================================================================
 * BUILD / MILESTONE LEGEND
 * ===================================================================== */

/* Adjacency-matrix graph: n nodes, m edges; weight[u][v] == INF means
 * "no direct edge u -> v" (see loadGraph). */
typedef struct {
    int n;
    int m;
    int weight[MAX_NODES][MAX_NODES];
} Graph;

/* One traveler's start/end node, as parsed from the input file's
 * traveler section by loadGraph. */
typedef struct {
    int source;
    int destination;
} TravelerDef;

/* Traveler animation state */
typedef enum {
    STATE_ACTIVE = 1,
    STATE_WAITING,
    STATE_BLOCKED,
    STATE_FINISHED,
} TravelerStateType;

/* Tags on an IPCMessage (MS5+) */
typedef enum {
    EVENT_NONE = 0,
    EVENT_MOVE,
    EVENT_ARRIVE,
    EVENT_WAIT,
    EVENT_DONE,
    EVENT_VACATE,
} EventType;

/* MS5/MS6/MS7: fixed-size message a child process writes down its pipe */
typedef struct {
    pid_t pid;
    int traveler_id;
    int current_node;
    int next_node;
    float x;
    float y;
    int state;
    int event;
} IPCMessage;

/* Parent-side bookkeeping for one traveler */
typedef struct {
    int id;
    pid_t pid;
    Color color;
    float x;
    float y;
    int source;
    int destination;
    int current_node;
    int next_node;
    int path[MAX_NODES];
    int path_len;
    int path_index;
    int jump_count;
    int state;
    bool finished;
    double last_time;
} Traveler;

/* MS6 only: one semaphore per graph node */
static sem_t *node_sems = NULL;

/* ===================== MILESTONE 6: NODE LOCKING (SEMAPHORES) ===================== */

#ifdef MS6
static bool initNodeSemaphores(int n) {
    node_sems = mmap(NULL, sizeof(sem_t) * MAX_NODES,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS,
                     -1, 0);
    if (node_sems == MAP_FAILED) {
        perror("mmap");
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (sem_init(&node_sems[i], 1, 1) != 0) {
            perror("sem_init");
            return false;
        }
    }
    return true;
}

static void destroyNodeSemaphores(int n) {
    if (node_sems == NULL) return;
    for (int i = 0; i < n; i++) {
        sem_destroy(&node_sems[i]);
    }
    munmap(node_sems, sizeof(sem_t) * MAX_NODES);
    node_sems = NULL;
}
#endif

/* ===================== MILESTONE 7: SCHEDULING (FCFS / SJF) ===================== */
#ifdef MS7
typedef enum {
    SCHED_FCFS = 0,
    SCHED_SJF,
} SchedAlgo;

static SchedAlgo g_sched = SCHED_FCFS;

typedef struct {
    int occupied;
    int queue[MAX_TRAVELERS];
    int arrival_time[MAX_TRAVELERS];
    int queue_len;
    sem_t mutex;
    sem_t gate[MAX_TRAVELERS];
} NodeQueue;

static NodeQueue *node_queues = NULL;

static bool initNodeQueues(int n) {
    node_queues = mmap(NULL, sizeof(NodeQueue) * MAX_NODES,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS,
                        -1, 0);
    if (node_queues == MAP_FAILED) {
        perror("mmap");
        return false;
    }
    for (int i = 0; i < n; i++) {
        node_queues[i].occupied = 0;
        node_queues[i].queue_len = 0;
        if (sem_init(&node_queues[i].mutex, 1, 1) != 0) {
            perror("sem_init");
            return false;
        }
        for (int t = 0; t < MAX_TRAVELERS; t++) {
            node_queues[i].arrival_time[t] = 0;
            if (sem_init(&node_queues[i].gate[t], 1, 0) != 0) {
                perror("sem_init");
                return false;
            }
        }
    }
    return true;
}

static void destroyNodeQueues(int n) {
    if (node_queues == NULL) return;
    for (int i = 0; i < n; i++) {
        sem_destroy(&node_queues[i].mutex);
        for (int t = 0; t < MAX_TRAVELERS; t++) {
            sem_destroy(&node_queues[i].gate[t]);
        }
    }
    munmap(node_queues, sizeof(NodeQueue) * MAX_NODES);
    node_queues = NULL;
}

static void enterNode(int node, int traveler_id, int burst) {
    NodeQueue *q = &node_queues[node];
    sem_wait(&q->mutex);
    if (!q->occupied) {
        q->occupied = 1;
        sem_post(&q->mutex);
        return;
    }
    q->arrival_time[traveler_id] = burst;
    q->queue[q->queue_len++] = traveler_id;
    sem_post(&q->mutex);
    sem_wait(&q->gate[traveler_id]);
}

static void scheduleNext(int node) {
    NodeQueue *q = &node_queues[node];
    sem_wait(&q->mutex);
    if (q->queue_len == 0) {
        q->occupied = 0;
        sem_post(&q->mutex);
        return;
    }
    int pick = 0;
    if (g_sched == SCHED_SJF) {
        for (int i = 1; i < q->queue_len; i++) {
            if (q->arrival_time[q->queue[i]] < q->arrival_time[q->queue[pick]]) {
                pick = i;
            }
        }
    }
    int traveler_id = q->queue[pick];
    for (int i = pick; i < q->queue_len - 1; i++) {
        q->queue[i] = q->queue[i + 1];
    }
    q->queue_len--;
    sem_post(&q->mutex);
    sem_post(&q->gate[traveler_id]);
}
#endif

/* Utility functions */
static void trimLine(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

static bool loadGraph(const char *path, Graph *g, TravelerDef travelers[], int *travelers_count) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror("fopen");
        return false;
    }

    if (fscanf(file, "%d %d", &g->n, &g->m) != 2) {
        fprintf(stderr, "Invalid graph header\n");
        fclose(file);
        return false;
    }
    if (g->n <= 0 || g->n > MAX_NODES || g->m < 0) {
        fprintf(stderr, "Invalid graph size or edge count\n");
        fclose(file);
        return false;
    }

    for (int i = 0; i < g->n; i++) {
        for (int j = 0; j < g->n; j++) {
            g->weight[i][j] = INF;
        }
    }

    for (int i = 0; i < g->m; i++) {
        int s, d, w;
        if (fscanf(file, "%d %d %d", &s, &d, &w) != 3) {
            fprintf(stderr, "Invalid edge line\n");
            fclose(file);
            return false;
        }
        if (s < 0 || d < 0 || w < 0 || s >= g->n || d >= g->n) {
            fprintf(stderr, "Invalid edge values\n");
            fclose(file);
            return false;
        }
        g->weight[s][d] = w;
    }

    *travelers_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        trimLine(line);
        if (line[0] == '\0') continue;

        if (strncmp(line, "#travelers", 10) == 0) {
            int count = 0;
            if (sscanf(line + 10, "%d", &count) != 1) {
                if (!fgets(line, sizeof(line), file)) {
                    fprintf(stderr, "Invalid travelers section\n");
                    fclose(file);
                    return false;
                }
                trimLine(line);
                if (sscanf(line, "%d", &count) != 1) {
                    fprintf(stderr, "Invalid travelers count\n");
                    fclose(file);
                    return false;
                }
            }
            if (count <= 0 || count > MAX_TRAVELERS) {
                fprintf(stderr, "Invalid travelers count\n");
                fclose(file);
                return false;
            }
            for (int t = 0; t < count; t++) {
                if (!fgets(line, sizeof(line), file)) {
                    fprintf(stderr, "Incomplete travelers list\n");
                    fclose(file);
                    return false;
                }
                trimLine(line);
                if (line[0] == '\0') {
                    t--;
                    continue;
                }
                int src, dst;
                if (sscanf(line, "%d %d", &src, &dst) != 2) {
                    fprintf(stderr, "Invalid traveler data\n");
                    fclose(file);
                    return false;
                }
                if (src < 0 || dst < 0 || src >= g->n || dst >= g->n) {
                    fprintf(stderr, "Invalid traveler nodes\n");
                    fclose(file);
                    return false;
                }
                travelers[*travelers_count].source = src;
                travelers[*travelers_count].destination = dst;
                (*travelers_count)++;
            }
            fclose(file);
            return true;
        }

        int src, dst;
        if (sscanf(line, "%d %d", &src, &dst) == 2) {
            if (src < 0 || dst < 0 || src >= g->n || dst >= g->n) {
                fprintf(stderr, "Invalid traveler nodes\n");
                fclose(file);
                return false;
            }
            travelers[0].source = src;
            travelers[0].destination = dst;
            *travelers_count = 1;
            fclose(file);
            return true;
        }

        int count;
        if (sscanf(line, "%d", &count) == 1) {
            if (count <= 0 || count > MAX_TRAVELERS) {
                fprintf(stderr, "Invalid travelers count\n");
                fclose(file);
                return false;
            }
            for (int t = 0; t < count; t++) {
                char tline[256];
                while (fgets(tline, sizeof(tline), file)) {
                    trimLine(tline);
                    if (tline[0] != '\0') break;
                }
                int tsrc, tdst;
                if (sscanf(tline, "%d %d", &tsrc, &tdst) != 2) {
                    fprintf(stderr, "Invalid traveler data\n");
                    fclose(file);
                    return false;
                }
                if (tsrc < 0 || tdst < 0 || tsrc >= g->n || tdst >= g->n) {
                    fprintf(stderr, "Invalid traveler nodes\n");
                    fclose(file);
                    return false;
                }
                travelers[*travelers_count].source = tsrc;
                travelers[*travelers_count].destination = tdst;
                (*travelers_count)++;
            }
            fclose(file);
            return true;
        }

        fprintf(stderr, "Invalid traveler or section header\n");
        fclose(file);
        return false;
    }

    fprintf(stderr, "Missing traveler section\n");
    fclose(file);
    return false;
}

static Vector2 nodePosition(int idx, int n, float cx, float cy, float radius) {
    float angle = ((float)idx / (float)n) * 2.0f * PI - PI / 2.0f;
    return (Vector2){cx + radius * cosf(angle), cy + radius * sinf(angle)};
}

static void drawArrow(Vector2 p1, Vector2 p2, Color color) {
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) return;

    float ux = dx / len;
    float uy = dy / len;
    Vector2 start = {p1.x + ux * NODE_RADIUS, p1.y + uy * NODE_RADIUS};
    Vector2 end = {p2.x - ux * NODE_RADIUS, p2.y - uy * NODE_RADIUS};

    DrawLineEx((Vector2){start.x, start.y}, (Vector2){end.x, end.y}, 2.5f, color);

    float angle = atan2f(uy, ux);
    Vector2 tip = {end.x, end.y};
    Vector2 left = {end.x - ARROW_HEAD * cosf(angle - 0.4f), end.y - ARROW_HEAD * sinf(angle - 0.4f)};
    Vector2 right = {end.x - ARROW_HEAD * cosf(angle + 0.4f), end.y - ARROW_HEAD * sinf(angle + 0.4f)};
    DrawTriangle(tip, left, right, color);
}

static void drawBoldText(const char *text, int x, int y, int fontSize, Color color) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            DrawText(text, x + dx, y + dy, fontSize, BLACK);
        }
    }
    DrawText(text, x, y, fontSize, color);
}

static int minDistance(int dist[], int visited[], int n) {
    int min = INF;
    int idx = -1;
    for (int i = 0; i < n; i++) {
        if (!visited[i] && dist[i] < min) {
            min = dist[i];
            idx = i;
        }
    }
    return idx;
}

static int dijkstra(const Graph *g, int src, int dst, int parent[]) {
    int dist[MAX_NODES];
    int visited[MAX_NODES];
    for (int i = 0; i < g->n; i++) {
        dist[i] = INF;
        visited[i] = 0;
        parent[i] = -1;
    }
    dist[src] = 0;

    for (int i = 0; i < g->n; i++) {
        int u = minDistance(dist, visited, g->n);
        if (u == -1) break;
        visited[u] = 1;
        for (int v = 0; v < g->n; v++) {
            if (!visited[v] && g->weight[u][v] != INF &&
                dist[u] != INF && dist[u] + g->weight[u][v] < dist[v]) {
                dist[v] = dist[u] + g->weight[u][v];
                parent[v] = u;
            }
        }
    }
    return dist[dst];
}

static void buildPath(int parent[], int dst, int path[], int *path_len) {
    int tmp[MAX_NODES];
    int count = 0;
    int cur = dst;
    while (cur != -1 && count < MAX_NODES) {
        tmp[count++] = cur;
        cur = parent[cur];
    }
    *path_len = count;
    for (int i = 0; i < count; i++) {
        path[i] = tmp[count - 1 - i];
    }
}

/* ===================== MILESTONES 5/6/7: PIPE-BASED IPC ===================== */
#if defined(MS5) || defined(MS6) || defined(MS7)
static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

static void sleepMs(long ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        ;
    }
}

static bool writeMessage(int fd, const IPCMessage *msg) {
    ssize_t written = write(fd, msg, sizeof(*msg));
    if (written == (ssize_t)sizeof(*msg)) return true;
    if (written == -1 && errno == EINTR) return writeMessage(fd, msg);
    if (written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        sleepMs(50);
        return writeMessage(fd, msg);
    }
    return false;
}

static void sendMove(int fd, pid_t pid, int traveler_id, int current_node, int next_node, float x, float y) {
    IPCMessage msg = {pid, traveler_id, current_node, next_node, x, y, STATE_ACTIVE, EVENT_MOVE};
    writeMessage(fd, &msg);
}

static void sendArrival(int fd, pid_t pid, int traveler_id, int current_node, int next_node, float x, float y) {
    IPCMessage msg = {pid, traveler_id, current_node, next_node, x, y, STATE_ACTIVE, EVENT_ARRIVE};
    writeMessage(fd, &msg);
}

static void sendWait(int fd, pid_t pid, int traveler_id, int current_node, int next_node, float x, float y, int state) {
    IPCMessage msg = {pid, traveler_id, current_node, next_node, x, y, state, EVENT_WAIT};
    writeMessage(fd, &msg);
}

static void sendDone(int fd, pid_t pid, int traveler_id, int current_node, float x, float y) {
    IPCMessage msg = {pid, traveler_id, current_node, -1, x, y, STATE_FINISHED, EVENT_DONE};
    writeMessage(fd, &msg);
}

#ifdef MS7
static void sendVacate(int fd, pid_t pid, int traveler_id, int node) {
    IPCMessage msg = {pid, traveler_id, node, -1, 0.0f, 0.0f, STATE_ACTIVE, EVENT_VACATE};
    writeMessage(fd, &msg);
}
#endif
#endif

#if !defined(MS5) && !defined(MS6) && !defined(MS7)
static double estimateTravelSeconds(int path[], int path_len, const Graph *g) {
    if (path_len <= 1) return 1.0;
    double seconds = 0.0;
    for (int i = 0; i < path_len - 1; i++) {
        seconds += g->weight[path[i]][path[i + 1]] * JUMP_TIME;
    }
    if (path_len > 2) seconds += (path_len - 2) * WAIT_TIME;
    return seconds;
}
#endif

static void initializeTravelers(Traveler travelers[], TravelerDef defs[], int traveler_count, Vector2 positions[]) {
    Color colors[MAX_TRAVELERS] = {MAROON, DARKGREEN, DARKBLUE, ORANGE, PINK, SKYBLUE, VIOLET, YELLOW};
    for (int i = 0; i < traveler_count; i++) {
        travelers[i].id = i;
        travelers[i].pid = 0;
        travelers[i].color = colors[i % 8];
        travelers[i].source = defs[i].source;
        travelers[i].destination = defs[i].destination;
        travelers[i].x = positions[defs[i].source].x;
        travelers[i].y = positions[defs[i].source].y;
        travelers[i].current_node = defs[i].source;
        travelers[i].next_node = defs[i].destination;
        travelers[i].path_len = 0;
        travelers[i].path_index = 0;
        travelers[i].jump_count = 0;
        travelers[i].state = STATE_ACTIVE;
        travelers[i].finished = false;
        travelers[i].last_time = GetTime();
    }
}

#if !defined(MS5) && !defined(MS6) && !defined(MS7)
static void computePathsInParent(Graph *g, Traveler travelers[], int traveler_count) {
    for (int i = 0; i < traveler_count; i++) {
        int parent[MAX_NODES];
        int dist = dijkstra(g, travelers[i].source, travelers[i].destination, parent);
        if (dist == INF) {
            travelers[i].path_len = 0;
            travelers[i].finished = true;
            travelers[i].state = STATE_FINISHED;
            continue;
        }
        buildPath(parent, travelers[i].destination, travelers[i].path, &travelers[i].path_len);
        travelers[i].path_index = 0;
        travelers[i].jump_count = 0;
        travelers[i].current_node = travelers[i].path[0];
        travelers[i].next_node = (travelers[i].path_len > 1) ? travelers[i].path[1] : -1;
        travelers[i].state = STATE_ACTIVE;
    }
}

static void childProcessStage4(int traveler_id, Traveler traveler, Graph *g) {
    (void)traveler_id;
    pid_t pid = getpid();
    printf("[PID=%d] started\n", pid);
    int path_len = traveler.path_len;
    double seconds = 0.0;
    if (path_len > 0) {
        seconds = estimateTravelSeconds(traveler.path, path_len, g);
    } else {
        seconds = WAIT_TIME;
    }
    sleep((unsigned int)ceil(seconds));
    exit(0);
}
#endif

#if defined(MS5) || defined(MS6) || defined(MS7)
static Vector2 blockedOutsidePosition(Vector2 center, int traveler_id) {
    float angle = ((float)(traveler_id % 8) / 8.0f) * 2.0f * PI;
    float radius = NODE_RADIUS * 1.8f;
    return (Vector2){center.x + radius * cosf(angle), center.y + radius * sinf(angle)};
}
#endif

/* ===================== MILESTONES 5/6: PIPE-REPORTING CHILD WITH HANDSHAKE ===================== */
#if defined(MS5) || defined(MS6)
/*
 * [משימה א] - childProcessStage5or6:
 * הוספנו את ack_read_fd שבו הילד קורא באופן חוסם (blocking) את אישור האב.
 */
static void childProcessStage5or6(int traveler_id, TravelerDef traveler, Graph *g, int write_fd, int ack_read_fd, Vector2 positions[]) {
    pid_t pid = getpid();
    int parent[MAX_NODES];
    int path[MAX_NODES];
    int path_len = 0;
    int dist = dijkstra(g, traveler.source, traveler.destination, parent);
    if (dist == INF) {
        sendDone(write_fd, pid, traveler_id, traveler.source, positions[traveler.source].x, positions[traveler.source].y);
        close(write_fd);
        close(ack_read_fd);
        exit(0);
    }
    buildPath(parent, traveler.destination, path, &path_len);

    if (path_len == 0) {
        sendDone(write_fd, pid, traveler_id, traveler.source, positions[traveler.source].x, positions[traveler.source].y);
        close(write_fd);
        close(ack_read_fd);
        exit(0);
    }

    int start = path[0];
#ifdef MS6
    sem_wait(&node_sems[start]);
#endif
    sendArrival(write_fd, pid, traveler_id, start, (path_len > 1) ? path[1] : -1, positions[start].x, positions[start].y);
    
    // [משימה א] המתנה לאישור מהאב על ההגעה הראשונית לצומת
    char approval;
    read(ack_read_fd, &approval, 1);

    if (path_len == 1) {
#ifdef MS6
        sleepMs((long)(WAIT_TIME * 1000.0));
        sem_post(&node_sems[start]);
#endif
        sendDone(write_fd, pid, traveler_id, start, positions[start].x, positions[start].y);
        close(write_fd);
        close(ack_read_fd);
        exit(0);
    }

#ifdef MS6
    sem_post(&node_sems[start]);
#endif

    for (int idx = 0; idx < path_len - 1; idx++) {
        int u = path[idx];
        int v = path[idx + 1];
        int weight = g->weight[u][v];
        Vector2 from = positions[u];
        Vector2 to = positions[v];

        for (int step = 1; step <= weight; step++) {
            float ratio = (float)step / (float)weight;
            float x = from.x + ratio * (to.x - from.x);
            float y = from.y + ratio * (to.y - from.y);
            sendMove(write_fd, pid, traveler_id, u, v, x, y);
            sleepMs((long)(JUMP_TIME * 1000.0));
        }

        if (idx + 1 == path_len - 1) {
#ifdef MS6
            Vector2 outside = blockedOutsidePosition(to, traveler_id);
            sendWait(write_fd, pid, traveler_id, u, v, outside.x, outside.y, STATE_BLOCKED);
            sem_wait(&node_sems[v]);
#endif
            sendArrival(write_fd, pid, traveler_id, v, -1, to.x, to.y);
            
            // [משימה א] המתנה לאישור מהאב בהגעה ליעד
            read(ack_read_fd, &approval, 1);

#ifdef MS6
            sleepMs((long)(WAIT_TIME * 1000.0));
            sem_post(&node_sems[v]);
#endif
            sendDone(write_fd, pid, traveler_id, v, to.x, to.y);
            break;
        }

#ifdef MS6
        Vector2 outside = blockedOutsidePosition(to, traveler_id);
        sendWait(write_fd, pid, traveler_id, u, v, outside.x, outside.y, STATE_BLOCKED);
        sem_wait(&node_sems[v]);
#endif
        sendArrival(write_fd, pid, traveler_id, v, path[idx + 2], to.x, to.y);
        
        // [משימה א] הבן ממתין לאישור מהאב לפני שממשיך לצומת הבא
        read(ack_read_fd, &approval, 1);

#ifdef MS6
        sleepMs((long)(WAIT_TIME * 1000.0));
        sem_post(&node_sems[v]);
#endif
    }

    close(write_fd);
    close(ack_read_fd);
    exit(0);
}
#endif

/* ===================== MILESTONE 7: SCHEDULED-ENTRY CHILD ===================== */
#ifdef MS7
static void childProcessStage7(int traveler_id, TravelerDef traveler, Graph *g, int write_fd, Vector2 positions[]) {
    pid_t pid = getpid();
    int parent[MAX_NODES];
    int path[MAX_NODES];
    int path_len = 0;
    int dist = dijkstra(g, traveler.source, traveler.destination, parent);
    if (dist == INF) {
        sendDone(write_fd, pid, traveler_id, traveler.source, positions[traveler.source].x, positions[traveler.source].y);
        close(write_fd);
        exit(0);
    }
    buildPath(parent, traveler.destination, path, &path_len);

    if (path_len == 0) {
        sendDone(write_fd, pid, traveler_id, traveler.source, positions[traveler.source].x, positions[traveler.source].y);
        close(write_fd);
        exit(0);
    }

    int start = path[0];
    enterNode(start, traveler_id, path_len - 1);
    sendArrival(write_fd, pid, traveler_id, start, (path_len > 1) ? path[1] : -1, positions[start].x, positions[start].y);

    if (path_len == 1) {
        sleepMs((long)(WAIT_TIME * 1000.0));
        sendVacate(write_fd, pid, traveler_id, start);
        sendDone(write_fd, pid, traveler_id, start, positions[start].x, positions[start].y);
        close(write_fd);
        exit(0);
    }

    sendVacate(write_fd, pid, traveler_id, start);

    for (int idx = 0; idx < path_len - 1; idx++) {
        int u = path[idx];
        int v = path[idx + 1];
        int weight = g->weight[u][v];
        Vector2 from = positions[u];
        Vector2 to = positions[v];
        int remaining_hops = path_len - 1 - (idx + 1);

        for (int step = 1; step <= weight; step++) {
            float ratio = (float)step / (float)weight;
            float x = from.x + ratio * (to.x - from.x);
            float y = from.y + ratio * (to.y - from.y);
            sendMove(write_fd, pid, traveler_id, u, v, x, y);
            sleepMs((long)(JUMP_TIME * 1000.0));
        }

        Vector2 outside = blockedOutsidePosition(to, traveler_id);
        sendWait(write_fd, pid, traveler_id, u, v, outside.x, outside.y, STATE_BLOCKED);
        enterNode(v, traveler_id, remaining_hops);

        if (idx + 1 == path_len - 1) {
            sendArrival(write_fd, pid, traveler_id, v, -1, to.x, to.y);
            sleepMs((long)(WAIT_TIME * 1000.0));
            sendVacate(write_fd, pid, traveler_id, v);
            sendDone(write_fd, pid, traveler_id, v, to.x, to.y);
            break;
        }

        sendArrival(write_fd, pid, traveler_id, v, path[idx + 2], to.x, to.y);
        sleepMs((long)(WAIT_TIME * 1000.0));
        sendVacate(write_fd, pid, traveler_id, v);
    }

    close(write_fd);
    exit(0);
}
#endif

/* ===================== PARENT VIEW UPDATE FROM IPC MESSAGES ===================== */
static void updateTravelerFromMessage(Traveler *traveler, const IPCMessage *msg) {
    if (msg->event == EVENT_VACATE) {
#ifdef MS7
        scheduleNext(msg->current_node);
#endif
        return;
    }
    traveler->pid = msg->pid;
    traveler->current_node = msg->current_node;
    traveler->next_node = msg->next_node;
    traveler->x = msg->x;
    traveler->y = msg->y;
    traveler->state = msg->state;
    if (msg->event == EVENT_ARRIVE) {
        printf("[PID=%d] arrived at node %d | next node: %d\n", msg->pid, msg->current_node, msg->next_node);
    } else if (msg->event == EVENT_DONE) {
        traveler->finished = true;
        traveler->state = STATE_FINISHED;
    }
}

#if defined(MS5) || defined(MS6) || defined(MS7)
/*
 * [משימה א] - handlePipeReads:
 * הוספנו את מערך ack_pipes כדי לשלוח אישור ('K') חזרה לילד כאשר הוא מדווח על הגעה לצומת (EVENT_ARRIVE).
 */
static void handlePipeReads(int pipes[][2], int ack_pipes[][2], Traveler travelers[], int traveler_count, bool pipe_open[]) {
    for (int i = 0; i < traveler_count; i++) {
        if (!pipe_open[i]) continue;
        while (true) {
            IPCMessage msg;
            ssize_t bytes = read(pipes[i][0], &msg, sizeof(msg));
            if (bytes == (ssize_t)sizeof(msg)) {
                updateTravelerFromMessage(&travelers[i], &msg);
                
                #if defined(MS5) || defined(MS6)
                // [משימה א] שליחת אישור לילד רק בעת הגעה לצומת (כדי שימשיך)
                if (msg.event == EVENT_ARRIVE) {
                    char token = 'K';
                    write(ack_pipes[i][1], &token, 1);
                }
                #endif
                
                continue;
            }
            if (bytes == 0) {
                pipe_open[i] = false;
                close(pipes[i][0]);
                #if defined(MS5) || defined(MS6)
                close(ack_pipes[i][1]); // סגירת קצה כתיבה של האישור באב
                #endif
                break;
            }
            if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (bytes == -1 && errno == EINTR) {
                continue;
            }
            break;
        }
    }
}
#endif

/* Rendering functions */
static void drawGraph(const Graph *g, Vector2 positions[]) {
    for (int u = 0; u < g->n; u++) {
        for (int v = 0; v < g->n; v++) {
            if (g->weight[u][v] == INF) continue;
            drawArrow(positions[u], positions[v], (Color){140, 140, 160, 255});
            char text[16];
            snprintf(text, sizeof(text), "%d", g->weight[u][v]);
            int textW = MeasureText(text, 18);
            drawBoldText(text,
                         (int)((positions[u].x + positions[v].x) * 0.5f) - textW / 2,
                         (int)((positions[u].y + positions[v].y) * 0.5f) - 10,
                         18,
                         SKYBLUE);
        }
    }
    for (int i = 0; i < g->n; i++) {
        DrawCircle((int)positions[i].x, (int)positions[i].y, NODE_RADIUS, (Color){45, 95, 175, 255});
        DrawCircleLines((int)positions[i].x, (int)positions[i].y, NODE_RADIUS, WHITE);
        char label[12];
        snprintf(label, sizeof(label), "%d", i);
        DrawText(label, (int)(positions[i].x - MeasureText(label, 20) / 2), (int)(positions[i].y - 10), 20, WHITE);
    }
}

static void drawTravelers(Traveler travelers[], int traveler_count) {
    double blink = sin(GetTime() * 6.0);
    for (int i = 0; i < traveler_count; i++) {
        Color fill = travelers[i].color;
        if (travelers[i].state == STATE_WAITING || travelers[i].state == STATE_BLOCKED) {
            fill = YELLOW;
        } else if (travelers[i].state == STATE_FINISHED) {
            fill = GRAY;
        }
        if (travelers[i].state == STATE_BLOCKED && blink > 0.0) {
            DrawCircle((int)travelers[i].x, (int)travelers[i].y, 18, Fade(fill, 0.5f));
        }
        DrawCircle((int)travelers[i].x, (int)travelers[i].y, 12, fill);
        DrawCircleLines((int)travelers[i].x, (int)travelers[i].y, 12, WHITE);
        DrawText(TextFormat("T%d", travelers[i].id), (int)travelers[i].x + 14, (int)travelers[i].y - 8, 16, WHITE);
    }
}

#if !defined(MS5) && !defined(MS6) && !defined(MS7)
static void updateSimpleAnimation(Traveler *traveler, const Graph *g, Vector2 positions[]) {
    if (traveler->finished || traveler->path_len <= 1) {
        if (traveler->path_len <= 1) {
            traveler->finished = true;
            traveler->state = STATE_FINISHED;
        }
        return;
    }
    int idx = traveler->path_index;
    int current = traveler->path[idx];
    int next = traveler->path[idx + 1];
    int weight = g->weight[current][next];
    double now = GetTime();

    if (weight <= 0) {
        traveler->path_index++;
        traveler->current_node = next;
        traveler->next_node = (traveler->path_index < traveler->path_len - 1) ? traveler->path[traveler->path_index + 1] : -1;
        traveler->jump_count = 0;
        traveler->state = STATE_WAITING;
        traveler->last_time = now;
        return;
    }

    if (traveler->state == STATE_ACTIVE) {
        if (now - traveler->last_time >= JUMP_TIME) {
            traveler->jump_count++;
            traveler->last_time = now;
            if (traveler->jump_count > weight) {
                traveler->jump_count = weight;
            }
        }
        float ratio = (float)traveler->jump_count / (float)weight;
        traveler->x = positions[current].x + ratio * (positions[next].x - positions[current].x);
        traveler->y = positions[current].y + ratio * (positions[next].y - positions[current].y);

        if (traveler->jump_count >= weight) {
            traveler->path_index++;
            traveler->current_node = next;
            traveler->next_node = (traveler->path_index < traveler->path_len - 1) ? traveler->path[traveler->path_index + 1] : -1;
            traveler->state = STATE_WAITING;
            traveler->jump_count = 0;
            traveler->last_time = now;
        }
    } else if (traveler->state == STATE_WAITING) {
        if (now - traveler->last_time >= WAIT_TIME) {
            if (traveler->path_index >= traveler->path_len - 1) {
                traveler->finished = true;
                traveler->state = STATE_FINISHED;
            } else {
                traveler->state = STATE_ACTIVE;
                traveler->last_time = now;
            }
        }
    }
}
#endif

/* ===================== MILESTONES 2/3/4: PARENT-DRIVEN ANIMATION LOOP ===================== */
#if !defined(MS5) && !defined(MS6) && !defined(MS7)
static void runStage4(Graph *g, TravelerDef defs[], int traveler_count, Vector2 positions[]) {
    Traveler travelers[MAX_TRAVELERS];
    initializeTravelers(travelers, defs, traveler_count, positions);
    computePathsInParent(g, travelers, traveler_count);

    pid_t child_pids[MAX_TRAVELERS];
    for (int i = 0; i < traveler_count; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            childProcessStage4(i, travelers[i], g);
        }
        child_pids[i] = pid;
        travelers[i].pid = pid;
    }

    InitWindow(SCREEN_W, SCREEN_H, "Flight Path Simulation - Milestone 4");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        bool anyActive = false;
        for (int i = 0; i < traveler_count; i++) {
            if (!travelers[i].finished) {
                anyActive = true;
                updateSimpleAnimation(&travelers[i], g, positions);
            }
        }

        BeginDrawing();
        ClearBackground((Color){18, 24, 40, 255});
        DrawText("Flight Path Simulation - Milestone 4", 16, 16, 20, LIGHTGRAY);
        drawGraph(g, positions);
        drawTravelers(travelers, traveler_count);
        EndDrawing();

        if (!anyActive) break;
    }

    CloseWindow();
    for (int i = 0; i < traveler_count; i++) {
        waitpid(child_pids[i], NULL, 0);
    }
}
#endif

/* ===================== MILESTONES 5/6: PIPE-DRIVEN ANIMATION LOOP WITH HANDSHAKE ===================== */
#if defined(MS5) || defined(MS6)
static void runStage5or6(Graph *g, TravelerDef defs[], int traveler_count, Vector2 positions[]) {
#ifdef MS6
    if (!initNodeSemaphores(g->n)) {
        return;
    }
#endif
    Traveler travelers[MAX_TRAVELERS];
    initializeTravelers(travelers, defs, traveler_count, positions);

    int pipes[MAX_TRAVELERS][2];
    int ack_pipes[MAX_TRAVELERS][2]; // [משימה א] מערך הצינורות החדש לאישורים מהאב לילד
    bool pipe_open[MAX_TRAVELERS];
    pid_t child_pids[MAX_TRAVELERS];

    for (int i = 0; i < traveler_count; i++) {
        // [משימה א] פתיחת שני הצינורות לכל נוסע
        if (pipe(pipes[i]) != 0 || pipe(ack_pipes[i]) != 0) {
            perror("pipe");
            exit(1);
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            // תהליך הילד (Child)
            close(pipes[i][0]);      // סגירת קצה קריאה של האב בילד
            close(ack_pipes[i][1]);  // [משימה א] סגירת קצה כתיבה של האב בילד
            
            // זימון פונקציית הילד עם העברת הצינור החדש
            childProcessStage5or6(i, defs[i], g, pipes[i][1], ack_pipes[i][0], positions);
        }
        
        // תהליך האב (Parent)
        close(pipes[i][1]);      // סגירת קצה כתיבה של הילד באב
        close(ack_pipes[i][0]);  // [משימה א] סגירת קצה קריאה של הילד באב
        
        if (!setNonBlocking(pipes[i][0])) {
            perror("fcntl");
            exit(1);
        }
        child_pids[i] = pid;
        travelers[i].pid = pid;
        pipe_open[i] = true;
    }

    InitWindow(SCREEN_W, SCREEN_H, "Flight Path Simulation - Milestone 5/6");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // [משימה א] קריאת צינורות והעברת מערך האישורים (ack_pipes)
        handlePipeReads(pipes, ack_pipes, travelers, traveler_count, pipe_open);
        bool anyActive = false;
        for (int i = 0; i < traveler_count; i++) {
            if (!travelers[i].finished) anyActive = true;
        }

        BeginDrawing();
        ClearBackground((Color){18, 24, 40, 255});
        DrawText("Flight Path Simulation - Milestone 5/6 (Handshake Mode)", 16, 16, 20, LIGHTGRAY);
        drawGraph(g, positions);
        drawTravelers(travelers, traveler_count);
        EndDrawing();

        if (!anyActive) break;
    }

    CloseWindow();
    for (int i = 0; i < traveler_count; i++) {
        waitpid(child_pids[i], NULL, 0);
        if (pipe_open[i]) {
            close(pipes[i][0]);
        }
        // סגירת צינור האישור שנותר פתוח
        close(ack_pipes[i][1]);
    }
#ifdef MS6
    destroyNodeSemaphores(g->n);
#endif
}
#endif

/* ===================== MILESTONE 7: SCHEDULED ANIMATION LOOP ===================== */
#ifdef MS7
static void runStage7(Graph *g, TravelerDef defs[], int traveler_count, Vector2 positions[]) {
    if (!initNodeQueues(g->n)) {
        return;
    }
    Traveler travelers[MAX_TRAVELERS];
    initializeTravelers(travelers, defs, traveler_count, positions);

    int pipes[MAX_TRAVELERS][2];
    bool pipe_open[MAX_TRAVELERS];
    pid_t child_pids[MAX_TRAVELERS];

    for (int i = 0; i < traveler_count; i++) {
        if (pipe(pipes[i]) != 0) {
            perror("pipe");
            exit(1);
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            close(pipes[i][0]);
            childProcessStage7(i, defs[i], g, pipes[i][1], positions);
        }
        close(pipes[i][1]);
        if (!setNonBlocking(pipes[i][0])) {
            perror("fcntl");
            exit(1);
        }
        child_pids[i] = pid;
        travelers[i].pid = pid;
        pipe_open[i] = true;
    }

    InitWindow(SCREEN_W, SCREEN_H, "Flight Path Simulation - Milestone 7");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // ב-MS7 אין מנגנון Handshake של צינور אישור (ack_pipes) ולכן מועבר NULL כפי שהיה במקור
        handlePipeReads(pipes, NULL, travelers, traveler_count, pipe_open);
        bool anyActive = false;
        for (int i = 0; i < traveler_count; i++) {
            if (!travelers[i].finished) anyActive = true;
        }

        BeginDrawing();
        ClearBackground((Color){18, 24, 40, 255});
        DrawText("Flight Path Simulation - Milestone 7", 16, 16, 20, LIGHTGRAY);
        DrawText(g_sched == SCHED_FCFS ? "Scheduler: FCFS" : "Scheduler: SJF",
                 16, 40, 18, YELLOW);
        drawGraph(g, positions);
        drawTravelers(travelers, traveler_count);
        EndDrawing();

        if (!anyActive) break;
    }

    CloseWindow();
    for (int i = 0; i < traveler_count; i++) {
        waitpid(child_pids[i], NULL, 0);
        if (pipe_open[i]) {
            close(pipes[i][0]);
        }
    }
    destroyNodeQueues(g->n);
}
#endif

/* ===================== ENTRY POINT ===================== */
int main(int argc, char *argv[]) {
    const char *file;

#ifdef MS7
    if (argc == 2) {
        file = argv[1];
    } else if (argc == 4 && strcmp(argv[1], "-schd") == 0) {
        if (strcmp(argv[2], "fcfs") == 0) {
            g_sched = SCHED_FCFS;
        } else if (strcmp(argv[2], "sjf") == 0) {
            g_sched = SCHED_SJF;
        } else {
            fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
            fprintf(stderr, "       %s -schd fcfs|sjf <file_name>\n", argv[0]);
            return 1;
        }
        file = argv[3];
    } else {
        fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
        fprintf(stderr, "       %s -schd fcfs|sjf <file_name>\n", argv[0]);
        return 1;
    }
#else
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
        return 1;
    }
    file = argv[1];
#endif

    Graph graph;
    TravelerDef traveler_defs[MAX_TRAVELERS];
    int traveler_count = 0;
    if (!loadGraph(file, &graph, traveler_defs, &traveler_count)) {
        return 1;
    }
    if (traveler_count <= 0) {
        fprintf(stderr, "No travelers configured in input file\n");
        return 1;
    }

    Vector2 positions[MAX_NODES];
    float cx = SCREEN_W / 2.0f;
    float cy = SCREEN_H / 2.0f - 30.0f;
    float radius = (SCREEN_H < SCREEN_W ? SCREEN_H : SCREEN_W) * 0.36f;
    for (int i = 0; i < graph.n; i++) {
        positions[i] = nodePosition(i, graph.n, cx, cy, radius);
    }

#if defined(MS4)
    runStage4(&graph, traveler_defs, traveler_count, positions);
#elif defined(MS5) || defined(MS6)
    runStage5or6(&graph, traveler_defs, traveler_count, positions);
#elif defined(MS7)
    runStage7(&graph, traveler_defs, traveler_count, positions);
#else
    runStage4(&graph, traveler_defs, traveler_count, positions);
#endif

    return 0;
}
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
 * =====================================================================
 * This single file is compiled once PER MILESTONE, each time with a
 * different preprocessor flag passed to gcc on the command line (see
 * Makefile). The flag controls which #ifdef/#if-defined blocks are
 * compiled in, so each milestone's binary only contains the code for
 * that milestone's IPC/synchronization model. dijkstra.c is a separate,
 * standalone Milestone 1 program and has nothing to do with these flags.
 *
 *   Milestone | Build flag | make target            | run command
 *   ----------+------------+------------------------+----------------------------------
 *   MS1       | (n/a)      | milestone1             | ./dijkstra <file_name>
 *   MS2/MS3   | (none)     | milestone2 / milestone3| ./sim <file_name>
 *   MS4       | -DMS4      | milestone4             | ./sim <file_name>
 *   MS5       | -DMS5      | milestone5             | ./sim <file_name>
 *   MS6       | -DMS6      | milestone6             | ./sim <file_name>
 *   MS7       | -DMS7      | milestone7             | ./sim <file_name>  OR
 *             |            |                        | ./sim -schd fcfs|sjf <file_name>
 *
 * main() dispatch (bottom of this file): main() always calls one of the
 * run*Stage* functions based on the same flags:
 *   -DMS4              -> runStage4()
 *   -DMS5 or -DMS6      -> runStage5or6()
 *   -DMS7              -> runStage7()
 *   (no flag, MS2/MS3) -> runStage4()   [falls into the same #else as MS4]
 *
 * Main functions per milestone:
 *   MS2/MS3 : loadGraph, dijkstra/minDistance/buildPath, computePathsInParent,
 *             childProcessStage4, updateSimpleAnimation, runStage4
 *             (MS2/MS3 predate forking the per-traveler animation into a
 *             child process at all; in this merged file they were folded
 *             into the same code path as MS4 - see the notes below.)
 *   MS4     : same functions as MS2/MS3 above (childProcessStage4 forks one
 *             child per traveler, but the child only sleeps - it sends no
 *             IPC and the parent still does all the path computing/drawing).
 *   MS5     : childProcessStage5or6, runStage5or6, handlePipeReads, plus the
 *             shared pipe-IPC helpers (setNonBlocking, sleepMs, writeMessage,
 *             sendMove/sendArrival/sendWait/sendDone). Children now compute
 *             their own path and report movement over a pipe; no locking.
 *   MS6     : everything MS5 has, plus initNodeSemaphores/destroyNodeSemaphores
 *             and the sem_wait/sem_post pair inside childProcessStage5or6 that
 *             enforces "one traveler per node at a time".
 *   MS7     : childProcessStage7, runStage7, plus the NodeQueue scheduler
 *             (initNodeQueues, destroyNodeQueues, enterNode, scheduleNext)
 *             which replaces MS6's plain semaphore with a parent-managed
 *             FCFS/SJF wait queue per node.
 *
 * Notes:
 *  - Every function guarded by "#if !defined(MS5) && !defined(MS6) &&
 *    !defined(MS7)" compiles under BOTH "no flag" (MS2/MS3) and "-DMS4",
 *    so MS2/MS3 and MS4 share identical code in this file.
 *  - Every function guarded by "#if defined(MS5) || defined(MS6) ||
 *    defined(MS7)" is the pipe-based IPC machinery shared by MS5, MS6 and
 *    MS7, since from MS5 onward the GUI process no longer simulates
 *    movement itself - it only renders state reported by forked children.
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

/* Traveler animation state, used by drawTravelers() to pick a color
 * (yellow while WAITING/BLOCKED at a node, gray once FINISHED) and by
 * the MS2-4 animation/MS5+ IPC code to track progress. */
typedef enum {
    STATE_ACTIVE = 1,
    STATE_WAITING,
    STATE_BLOCKED,
    STATE_FINISHED,
} TravelerStateType;

/* Tags on an IPCMessage (MS5+) describing what the sending child just
 * did; read by updateTravelerFromMessage() in the parent.
 * EVENT_VACATE is MS7-only: it means "I just freed a node, schedule the
 * next waiter" and is handled as a pure side-channel signal rather than
 * a traveler position update (see updateTravelerFromMessage). */
typedef enum {
    EVENT_NONE = 0,
    EVENT_MOVE,
    EVENT_ARRIVE,
    EVENT_WAIT,
    EVENT_DONE,
    EVENT_VACATE,
} EventType;

/* MS5/MS6/MS7: fixed-size message a child process writes down its pipe
 * to report progress; consumed by handlePipeReads()/updateTravelerFromMessage()
 * in the parent. Plain struct (no pointers) so it is safe to write()/read()
 * across the pipe as raw bytes. */
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

/* Parent-side bookkeeping for one traveler: drawing state (color/x/y),
 * plus - MS2/MS3/MS4 only - the precomputed path[] and animation counters
 * (path_index/jump_count/last_time) used by updateSimpleAnimation(). From
 * MS5 onward the child process owns path-walking, so those fields stay
 * unused on the parent side and only id/pid/color/x/y/state/finished
 * actually get updated, via IPC. */
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

/* MS6 only: one semaphore per graph node, used as a binary mutex guarding
 * "at most one traveler occupies this node at a time". Declared
 * unconditionally (and left NULL) so the symbol exists even when MS6 is
 * not the active build; see initNodeSemaphores/destroyNodeSemaphores and
 * the sem_wait/sem_post pair in childProcessStage5or6. */
static sem_t *node_sems = NULL;

/* ===================== MILESTONE 6: NODE LOCKING (SEMAPHORES) ===================== */

#ifdef MS6
/* Milestone 6.
 * Purpose: allocate one POSIX semaphore per node in memory shared across
 * the parent and all forked children, each initialized to 1 (unlocked),
 * so node_sems[i] can be used as a mutex enforcing one traveler per node.
 * Parameters: n - number of nodes in the loaded graph.
 * Returns: true on success; false if mmap or any sem_init call fails.
 * Side effects: mmap's a MAX_NODES-sized sem_t array into shared memory
 * (MAP_SHARED) and stores it in the global node_sems. Must run before
 * fork()ing any children so they inherit the same shared mapping. */
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

/* Milestone 6.
 * Purpose: tear down the per-node semaphores created by initNodeSemaphores
 * and release their shared memory.
 * Parameters: n - number of nodes to destroy semaphores for.
 * Returns: nothing.
 * Side effects: sem_destroy on each semaphore, then munmap of the shared
 * region; safe to call even if init never ran (node_sems == NULL). */
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
/* MS7 replaces MS6's plain "first to win sem_wait gets in" semaphore with
 * a per-node wait queue (NodeQueue) that the PARENT process drains in
 * scheduling order (FCFS or SJF) whenever a node is vacated, instead of
 * letting the OS scheduler pick which blocked child wakes up first. */

#ifdef MS7
/* Which policy scheduleNext() uses to pick the next waiter out of a
 * node's queue; selected via the "-schd fcfs|sjf" command-line option
 * parsed in main() (defaults to FCFS if no -schd flag is given). */
typedef enum {
    SCHED_FCFS = 0,
    SCHED_SJF,
} SchedAlgo;

/* Global because scheduleNext() runs in the single parent process and is
 * shared across every node's queue; set once at startup, never changed
 * mid-run. */
static SchedAlgo g_sched = SCHED_FCFS;

/* gate[] and arrival_time[] are indexed by traveler_id (not queue position),
 * so a waiter's wakeup slot never shifts when other entries are removed. */
typedef struct {
    int occupied;
    int queue[MAX_TRAVELERS];
    int arrival_time[MAX_TRAVELERS];
    int queue_len;
    sem_t mutex;
    sem_t gate[MAX_TRAVELERS];
} NodeQueue;

static NodeQueue *node_queues = NULL;

/* Milestone 7.
 * Purpose: allocate the per-node NodeQueue array in shared memory and
 * initialize every node to "free" with an empty queue and a closed
 * (count 0) gate semaphore per traveler slot.
 * Parameters: n - number of nodes in the loaded graph.
 * Returns: true on success; false if mmap or any sem_init call fails.
 * Side effects: mmap's a MAX_NODES-sized NodeQueue array into shared
 * memory (MAP_SHARED) and stores it in the global node_queues. Must run
 * before fork()ing any children so they inherit the same mapping. */
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

/* Milestone 7.
 * Purpose: tear down every node's mutex and gate semaphores created by
 * initNodeQueues and release the shared memory.
 * Parameters: n - number of nodes to destroy queues for.
 * Returns: nothing.
 * Side effects: sem_destroy on each mutex/gate, then munmap of the
 * shared region; safe to call even if init never ran (node_queues == NULL). */
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

/* Milestone 7.
 * Purpose: request entry into `node` on behalf of `traveler_id`, blocking
 * until the parent's scheduler lets this traveler in if the node is
 * already occupied.
 * Parameters: node - node to enter; traveler_id - caller's traveler id
 * (used as the index into queue[]/arrival_time[]/gate[]); burst - the
 * traveler's remaining hop count, stashed as the SJF sort key (see
 * scheduleNext).
 * Returns: nothing (returns once the caller may treat the node as
 * entered).
 * Side effects: takes/releases q->mutex; may block this process on
 * q->gate[traveler_id] until another process (the parent, in
 * scheduleNext) posts it.
 *
 * Called by a child arriving at `node`. Enters immediately if free, otherwise
 * joins the wait queue and blocks on its own gate slot until the parent
 * (which owns every 0/1 transition of `occupied` once contention exists)
 * schedules it in. */
static void enterNode(int node, int traveler_id, int burst) {
    NodeQueue *q = &node_queues[node];
    sem_wait(&q->mutex);
    if (!q->occupied) {
        q->occupied = 1;
        sem_post(&q->mutex);
        return;
    }
    /* arrival_time[] is mislabeled by name - it actually stores `burst`
     * (the caller's remaining hop count), which is what scheduleNext()
     * compares under SJF. It is only ever a true "arrival order" key
     * under FCFS, where queue[] position (not this array) does the job. */
    q->arrival_time[traveler_id] = burst;
    q->queue[q->queue_len++] = traveler_id;
    sem_post(&q->mutex);
    sem_wait(&q->gate[traveler_id]);
}

/* Milestone 7.
 * Purpose: runs in the PARENT process when it receives an EVENT_VACATE
 * message for `node` (see updateTravelerFromMessage); picks the next
 * waiter (if any) per g_sched and wakes it, or marks the node free if no
 * one is waiting.
 * Parameters: node - the node that was just vacated.
 * Returns: nothing.
 * Side effects: takes/releases q->mutex; sem_post's the chosen waiter's
 * gate, waking that child process.
 *
 * Runs in the parent process upon EVENT_VACATE. Picks the next waiter (if
 * any) according to g_sched and wakes it; otherwise marks the node free.
 * occupied stays 1 across the whole handoff so a newly arriving child can
 * never slip in between a vacate and the next scheduling decision. */
static void scheduleNext(int node) {
    NodeQueue *q = &node_queues[node];
    sem_wait(&q->mutex);
    if (q->queue_len == 0) {
        q->occupied = 0;
        sem_post(&q->mutex);
        return;
    }
    /* FCFS: pick index 0, i.e. whoever has been in queue[] longest (queue
     * order doubles as arrival order). SJF: scan for the queued traveler
     * with the smallest stored burst (remaining hops), i.e. the "shortest
     * job" - so it ignores how long anyone has actually been waiting. */
    int pick = 0;
    if (g_sched == SCHED_SJF) {
        for (int i = 1; i < q->queue_len; i++) {
            if (q->arrival_time[q->queue[i]] < q->arrival_time[q->queue[pick]]) {
                pick = i;
            }
        }
    }
    int traveler_id = q->queue[pick];
    /* Remove the chosen entry by shifting the rest of the queue down one
     * slot (queue[] is a small flat array, not a ring buffer). */
    for (int i = pick; i < q->queue_len - 1; i++) {
        q->queue[i] = q->queue[i + 1];
    }
    q->queue_len--;
    sem_post(&q->mutex);
    sem_post(&q->gate[traveler_id]);
}
#endif

/* Milestone 2/3/4/5/6/7 (shared utility).
 * Purpose: strip a trailing '\n' and/or '\r' from a line read by fgets,
 * so downstream sscanf calls in loadGraph don't have to deal with them.
 * Parameters: line - null-terminated string to trim in place.
 * Returns: nothing.
 * Side effects: mutates `line` in place. */
static void trimLine(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

/* Milestone 2/3/4/5/6/7 (shared utility).
 * Purpose: parse the simulation's input file into a Graph and a list of
 * TravelerDef entries. File format: a header line "n m" (node count,
 * edge count), then m "src dst weight" edge lines, then a traveler
 * section (several supported forms - see inline comments below).
 * Parameters: path - file to open; g - graph to fill in; travelers -
 * output array of traveler definitions (caller-sized, MAX_TRAVELERS);
 * travelers_count - output, number of travelers parsed.
 * Returns: true on success; false on any I/O or format error (writes a
 * diagnostic to stderr in that case).
 * Side effects: opens/closes `path`; fully overwrites g->weight. */
static bool loadGraph(const char *path, Graph *g, TravelerDef travelers[], int *travelers_count) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror("fopen");
        return false;
    }

    /* --- Header line: "n m" --- */
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

    /* No edge by default; only edges explicitly listed below become finite. */
    for (int i = 0; i < g->n; i++) {
        for (int j = 0; j < g->n; j++) {
            g->weight[i][j] = INF;
        }
    }

    /* --- Edge list: g->m lines of "src dst weight" --- */
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

    /* --- Traveler section: read line by line, supporting three formats:
     * (1) "#travelers" header (count on the same line or the next
     *     non-blank line), followed by that many "src dst" lines;
     * (2) a single bare "src dst" line (exactly one traveler);
     * (3) a bare integer on its own line giving a traveler count,
     *     followed by that many "src dst" lines.
     * Whichever form is detected first on the next non-blank line wins. --- */
    *travelers_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        trimLine(line);
        if (line[0] == '\0') continue;

        if (strncmp(line, "#travelers", 10) == 0) {
            /* Form (1): try the count inline after "#travelers"; if that
             * fails, fall back to reading the count from the next line. */
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
                    /* blank line: doesn't count as one of the `count`
                     * travelers, so retry this same slot. */
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
            /* Form (2): a bare "src dst" line with no count/header at
             * all means exactly one traveler. */
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

        /* bare integer on its own line = traveler count */
        int count;
        if (sscanf(line, "%d", &count) == 1) {
            /* Form (3): same idea as form (1) but without the
             * "#travelers" marker. */
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

/* Milestone 2/3/4/5/6/7 (shared GUI helper).
 * Purpose: compute the on-screen (x, y) position of node `idx` when all
 * n nodes are laid out evenly around a circle.
 * Parameters: idx - node index; n - total node count; cx, cy - circle
 * center; radius - circle radius.
 * Returns: the node's screen-space Vector2 position.
 * Side effects: none. */
static Vector2 nodePosition(int idx, int n, float cx, float cy, float radius) {
    float angle = ((float)idx / (float)n) * 2.0f * PI - PI / 2.0f;
    return (Vector2){cx + radius * cosf(angle), cy + radius * sinf(angle)};
}

/* Milestone 2/3/4/5/6/7 (shared GUI helper).
 * Purpose: draw a directed-edge arrow from p1 to p2, shortened at both
 * ends by NODE_RADIUS so it doesn't overlap the node circles, with a
 * triangular arrowhead at the p2 end.
 * Parameters: p1, p2 - edge endpoints in screen space; color - arrow color.
 * Returns: nothing.
 * Side effects: draws to the screen (raylib DrawLineEx/DrawTriangle). */
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

/* Milestone 2/3/4/5/6/7 (shared GUI helper).
 * Purpose: draw `text` with a 1px black outline (drawn 8 times around the
 * target position) so it stays legible over any background color.
 * Parameters: text - string to draw; x, y - top-left position; fontSize -
 * font size; color - fill color of the text (outline is always BLACK).
 * Returns: nothing.
 * Side effects: draws to the screen (raylib DrawText, 9 calls total). */
static void drawBoldText(const char *text, int x, int y, int fontSize, Color color) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            DrawText(text, x + dx, y + dy, fontSize, BLACK);
        }
    }
    DrawText(text, x, y, fontSize, color);
}

/* Milestone 2/3/4/5/6/7 (Dijkstra building block).
 * Purpose: find the closest not-yet-visited node, i.e. the "pick the next
 * node to finalize" step of Dijkstra's algorithm.
 * Parameters: dist - current tentative distances; visited - 1 for nodes
 * already finalized; n - number of nodes.
 * Returns: index of the closest unvisited node, or -1 if none remain
 * reachable (search is complete or the rest is disconnected).
 * Side effects: none. */
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

/* Milestone 2/3/4/5/6/7 (path computation).
 * Purpose: run Dijkstra's algorithm from src to dst over adjacency-matrix
 * graph g, filling in parent[] so the path can be rebuilt afterward by
 * buildPath().
 * Parameters: g - graph; src, dst - endpoints; parent - output array,
 * parent[v] = the node Dijkstra last relaxed v's distance through (-1 if
 * v is unreached, e.g. the source itself or an unreachable node).
 * Returns: the shortest distance from src to dst, or INF if unreachable.
 * Side effects: none (writes only into the caller-provided parent[]). */
static int dijkstra(const Graph *g, int src, int dst, int parent[]) {
    int dist[MAX_NODES];
    int visited[MAX_NODES];
    for (int i = 0; i < g->n; i++) {
        dist[i] = INF;
        visited[i] = 0;
        parent[i] = -1;
    }
    dist[src] = 0;

    /* Finalize one node's shortest distance per iteration: pick the
     * closest unvisited node (minDistance), mark it visited, then relax
     * every outgoing edge from it - i.e. check whether routing through u
     * beats v's current best distance, and if so remember u as v's
     * parent on the shortest path. */
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

/* Milestone 2/3/4/5/6/7 (path computation).
 * Purpose: rebuild the actual src -> ... -> dst path from the parent[]
 * array produced by dijkstra(), in forward order.
 * Parameters: parent - parent[] array from dijkstra(); dst - destination
 * node to walk back from; path - output array, filled with the path in
 * forward (source-first) order; path_len - output, number of nodes in
 * the path.
 * Returns: nothing.
 * Side effects: writes into path[] and *path_len.
 *
 * parent[] only records, for each node, which node it was reached FROM,
 * so walking parent[dst], parent[parent[dst]], ... naturally produces the
 * path in REVERSE (destination-first) order, terminating when cur == -1
 * (the source, which has no parent). tmp[] collects that reversed walk,
 * then the final loop copies it into path[] back-to-front so callers see
 * the path source-first. */
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
/* From MS5 onward, each traveler's child process computes and walks its
 * own path and reports progress to the parent over a pipe instead of the
 * parent animating everything itself (contrast with MS2-4's
 * computePathsInParent/updateSimpleAnimation below). */

#if defined(MS5) || defined(MS6) || defined(MS7)
/* Milestone 5/6/7.
 * Purpose: put a file descriptor into non-blocking mode so reads/writes
 * on it return EAGAIN instead of blocking the caller.
 * Parameters: fd - file descriptor to modify.
 * Returns: true on success, false if either fcntl call fails.
 * Side effects: changes fd's flags via fcntl. */
static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

/* Milestone 5/6/7.
 * Purpose: sleep for `ms` milliseconds, restarting cleanly if interrupted
 * by a signal (EINTR), since nanosleep() can be woken early.
 * Parameters: ms - duration to sleep, in milliseconds.
 * Returns: nothing.
 * Side effects: blocks the calling process for approximately ms milliseconds. */
static void sleepMs(long ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        ;
    }
}

/* Milestone 5/6/7.
 * Purpose: write one whole IPCMessage to a pipe, retrying as needed so a
 * partial/blocked write doesn't corrupt the message framing (every read
 * on the other end expects exactly sizeof(IPCMessage) bytes per message).
 * Parameters: fd - pipe write end; msg - message to send.
 * Returns: true if the full message was written; false on an
 * unrecoverable write error.
 * Side effects: writes to fd; may sleep 50ms and recurse if the pipe
 * buffer is momentarily full (EAGAIN/EWOULDBLOCK) since the child writes
 * in non-blocking-safe fashion even though its own fd is normally
 * blocking - this also defends against the parent's reader being slow. */
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

/* Milestone 5/6/7.
 * Purpose: send an EVENT_MOVE update (traveler is mid-edge, at a new
 * interpolated position).
 * Parameters: fd - pipe write end; pid - sender pid; traveler_id - which
 * traveler; current_node/next_node - the edge endpoints; x, y - current
 * interpolated screen position.
 * Returns: nothing (ignores writeMessage's success/failure).
 * Side effects: writes one IPCMessage to fd. */
static void sendMove(int fd, pid_t pid, int traveler_id, int current_node, int next_node, float x, float y) {
    IPCMessage msg = {pid, traveler_id, current_node, next_node, x, y, STATE_ACTIVE, EVENT_MOVE};
    writeMessage(fd, &msg);
}

/* Milestone 5/6/7.
 * Purpose: send an EVENT_ARRIVE update (traveler has just reached a node).
 * Parameters: fd - pipe write end; pid - sender pid; traveler_id - which
 * traveler; current_node - node just reached; next_node - node after it
 * on the path (-1 if this is the final node); x, y - node's screen position.
 * Returns: nothing.
 * Side effects: writes one IPCMessage to fd. */
static void sendArrival(int fd, pid_t pid, int traveler_id, int current_node, int next_node, float x, float y) {
    IPCMessage msg = {pid, traveler_id, current_node, next_node, x, y, STATE_ACTIVE, EVENT_ARRIVE};
    writeMessage(fd, &msg);
}

/* Milestone 5/6/7.
 * Purpose: send an EVENT_WAIT update (traveler is blocked waiting for a
 * node to free up, or pausing at a node), carrying the given display state.
 * Parameters: fd - pipe write end; pid - sender pid; traveler_id - which
 * traveler; current_node/next_node - edge the traveler is waiting on;
 * x, y - screen position to show while waiting (e.g. just outside the node);
 * state - TravelerStateType to report (STATE_BLOCKED while queued for a node).
 * Returns: nothing.
 * Side effects: writes one IPCMessage to fd. */
static void sendWait(int fd, pid_t pid, int traveler_id, int current_node, int next_node, float x, float y, int state) {
    IPCMessage msg = {pid, traveler_id, current_node, next_node, x, y, state, EVENT_WAIT};
    writeMessage(fd, &msg);
}

/* Milestone 5/6/7.
 * Purpose: send an EVENT_DONE update (traveler has finished its journey
 * or had no valid path at all) and mark it finished.
 * Parameters: fd - pipe write end; pid - sender pid; traveler_id - which
 * traveler; current_node - final node; x, y - final screen position.
 * Returns: nothing.
 * Side effects: writes one IPCMessage to fd. */
static void sendDone(int fd, pid_t pid, int traveler_id, int current_node, float x, float y) {
    IPCMessage msg = {pid, traveler_id, current_node, -1, x, y, STATE_FINISHED, EVENT_DONE};
    writeMessage(fd, &msg);
}

#ifdef MS7
/* Milestone 7.
 * Purpose: send an EVENT_VACATE notification telling the parent that
 * this traveler has just left `node`, so the parent's scheduleNext()
 * should let the next queued waiter (if any) in.
 * Parameters: fd - pipe write end; pid - sender pid; traveler_id - which
 * traveler; node - the node just vacated.
 * Returns: nothing.
 * Side effects: writes one IPCMessage to fd; this message carries no
 * meaningful x/y/next_node (see updateTravelerFromMessage, which treats
 * EVENT_VACATE purely as a signal and does not apply it as a position
 * update). */
static void sendVacate(int fd, pid_t pid, int traveler_id, int node) {
    IPCMessage msg = {pid, traveler_id, node, -1, 0.0f, 0.0f, STATE_ACTIVE, EVENT_VACATE};
    writeMessage(fd, &msg);
}
#endif

#endif

/* ===================== MILESTONES 2/3/4: SEQUENTIAL TRAVEL-TIME ESTIMATE ===================== */

#if !defined(MS5) && !defined(MS6) && !defined(MS7)
/* Milestone 2/3/4.
 * Purpose: estimate total travel time for a precomputed path, used only
 * to decide how long childProcessStage4's child process should sleep
 * (MS2-4 children don't report real progress, they just occupy a pid for
 * that duration while the parent animates the same path independently).
 * Parameters: path - node sequence; path_len - number of nodes in path;
 * g - graph (for edge weights).
 * Returns: estimated seconds: sum of each edge's weight * JUMP_TIME, plus
 * one WAIT_TIME per intermediate node (not counting the first/last); 1.0
 * if the path has 0 or 1 nodes (no travel needed).
 * Side effects: none. */
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

/* Milestone 2/3/4/5/6/7 (shared setup).
 * Purpose: initialize the parent's Traveler[] array (drawing state,
 * starting position, and - for MS2-4 - animation defaults) from the
 * parsed TravelerDef list.
 * Parameters: travelers - output array to fill; defs - source/destination
 * pairs parsed by loadGraph; traveler_count - number of travelers;
 * positions - screen position of every node (for the initial x/y).
 * Returns: nothing.
 * Side effects: writes into travelers[0..traveler_count). */
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
/* Milestone 2/3/4.
 * Purpose: precompute every traveler's shortest path in the PARENT
 * process (unlike MS5+, where each child computes its own path). Marks
 * travelers with no valid path as immediately finished.
 * Parameters: g - graph; travelers - traveler array to fill in (path,
 * path_len, current/next node, state); traveler_count - number of travelers.
 * Returns: nothing.
 * Side effects: writes into travelers[0..traveler_count). */
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

/* Milestone 2/3/4.
 * Purpose: child process body for one traveler - since the parent already
 * computed the path and animates it independently, the child's only job
 * is to occupy a real pid for roughly the same duration the parent's
 * animation will take, then exit. No IPC is involved at all.
 * Parameters: traveler_id - unused (kept for signature symmetry with the
 * MS5+ child functions; explicitly cast to void to silence the unused
 * warning); traveler - this traveler's precomputed path/path_len;
 * g - graph (needed by estimateTravelSeconds for edge weights).
 * Returns: never returns - calls exit(0).
 * Side effects: forks no further children; sleeps; prints a startup
 * line; terminates the process via exit(). */
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
/* Milestone 5/6/7.
 * Purpose: compute a screen position just outside a node's circle, used
 * to draw a traveler that is blocked/queued waiting to enter that node
 * (so it visibly hovers near, not on top of, the occupied node).
 * Parameters: center - the node's screen position; traveler_id - used
 * only to fan multiple waiting travelers out around the node instead of
 * stacking them on the same point.
 * Returns: the computed screen position.
 * Side effects: none. */
static Vector2 blockedOutsidePosition(Vector2 center, int traveler_id) {
    float angle = ((float)(traveler_id % 8) / 8.0f) * 2.0f * PI;
    float radius = NODE_RADIUS * 1.8f;
    return (Vector2){center.x + radius * cosf(angle), center.y + radius * sinf(angle)};
}
#endif

/* ===================== MILESTONES 5/6: PIPE-REPORTING CHILD (WITH MS6 NODE LOCK) ===================== */

#if defined(MS5) || defined(MS6)
/* Milestone 5 (base behavior) / Milestone 6 (adds the #ifdef MS6 locking
 * inside this same function).
 * Purpose: child process body for one traveler under MS5/MS6 - computes
 * its own shortest path, then walks it edge by edge, reporting each move
 * and arrival back to the parent over a pipe. Under MS6 only, also
 * takes/releases the per-node semaphore around occupying each node so
 * only one traveler is ever "at" a given node at a time.
 * Parameters: traveler_id - this traveler's id (used as the IPC tag);
 * traveler - source/destination pair; g - graph; write_fd - pipe write
 * end to report progress on; positions - screen position of every node.
 * Returns: never returns - calls exit(0) on every path.
 * Side effects: writes a stream of IPCMessages to write_fd; sleeps to
 * pace the animation; under MS6, calls sem_wait/sem_post on
 * node_sems[...] (blocking if another traveler currently occupies that
 * node); closes write_fd and exits. */
static void childProcessStage5or6(int traveler_id, TravelerDef traveler, Graph *g, int write_fd, Vector2 positions[]) {
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
#ifdef MS6
    /* MS6: claim the starting node before announcing arrival there, so no
     * other traveler can be considered "at" this node concurrently. */
    sem_wait(&node_sems[start]);
#endif
    sendArrival(write_fd, pid, traveler_id, start, (path_len > 1) ? path[1] : -1, positions[start].x, positions[start].y);

    if (path_len == 1) {
#ifdef MS6
        /* Source == destination: hold the node for one WAIT_TIME (as if
         * "visiting" it) before releasing it. */
        sleepMs((long)(WAIT_TIME * 1000.0));
        sem_post(&node_sems[start]);
#endif
        sendDone(write_fd, pid, traveler_id, start, positions[start].x, positions[start].y);
        close(write_fd);
        exit(0);
    }

#ifdef MS6
    /* About to start moving toward the next node, so this traveler no
     * longer occupies `start` - release it immediately (MS5 has no
     * concept of occupancy, so this is MS6-only). */
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
            /* Last hop: report BLOCKED/waiting just outside the
             * destination node while contending for its semaphore -
             * sem_wait below may block here until whoever currently
             * occupies v releases it. */
            Vector2 outside = blockedOutsidePosition(to, traveler_id);
            sendWait(write_fd, pid, traveler_id, u, v, outside.x, outside.y, STATE_BLOCKED);
            sem_wait(&node_sems[v]);
#endif
            sendArrival(write_fd, pid, traveler_id, v, -1, to.x, to.y);
#ifdef MS6
            sleepMs((long)(WAIT_TIME * 1000.0));
            sem_post(&node_sems[v]);
#endif
            sendDone(write_fd, pid, traveler_id, v, to.x, to.y);
            break;
        }

#ifdef MS6
        /* Intermediate hop: same contend-for-v-then-occupy-then-release
         * pattern as the final hop above, just without finishing. */
        Vector2 outside = blockedOutsidePosition(to, traveler_id);
        sendWait(write_fd, pid, traveler_id, u, v, outside.x, outside.y, STATE_BLOCKED);
        sem_wait(&node_sems[v]);
#endif
        sendArrival(write_fd, pid, traveler_id, v, path[idx + 2], to.x, to.y);
#ifdef MS6
        sleepMs((long)(WAIT_TIME * 1000.0));
        sem_post(&node_sems[v]);
#endif
    }

    close(write_fd);
    exit(0);
}
#endif

/* ===================== MILESTONE 7: SCHEDULED-ENTRY CHILD ===================== */

#ifdef MS7
/* Milestone 7.
 * Purpose: child process body for one traveler under MS7 - same overall
 * shape as childProcessStage5or6, but node occupancy is no longer a
 * simple semaphore: entry is requested via enterNode() (which may queue
 * the caller) and release is reported to the parent via sendVacate()
 * (EVENT_VACATE) so the PARENT's scheduleNext() - not the OS - decides
 * who enters next, using FCFS or SJF.
 * Parameters: traveler_id - this traveler's id; traveler -
 * source/destination pair; g - graph; write_fd - pipe write end; positions
 * - screen position of every node.
 * Returns: never returns - calls exit(0) on every path.
 * Side effects: writes a stream of IPCMessages to write_fd (including
 * EVENT_VACATE before moving on/finishing); calls enterNode() which may
 * block this process on a semaphore gate; sleeps to pace the animation;
 * closes write_fd and exits. */
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
    /* path_len - 1 = total remaining hops from the start node to the
     * destination; this is the "burst" SJF will compare on. */
    enterNode(start, traveler_id, path_len - 1);
    sendArrival(write_fd, pid, traveler_id, start, (path_len > 1) ? path[1] : -1, positions[start].x, positions[start].y);

    if (path_len == 1) {
        sleepMs((long)(WAIT_TIME * 1000.0));
        /* Tell the parent this node is free again before reporting done,
         * so scheduleNext() can let in whoever was queued behind us. */
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
        /* Hops left AFTER arriving at v - this becomes v's SJF burst if
         * we end up queueing for v below. */
        int remaining_hops = path_len - 1 - (idx + 1);

        for (int step = 1; step <= weight; step++) {
            float ratio = (float)step / (float)weight;
            float x = from.x + ratio * (to.x - from.x);
            float y = from.y + ratio * (to.y - from.y);
            sendMove(write_fd, pid, traveler_id, u, v, x, y);
            sleepMs((long)(JUMP_TIME * 1000.0));
        }

        /* Always report BLOCKED/waiting outside v before calling
         * enterNode(), since under MS7 we don't know in advance whether
         * v is free or we'll be queued by the scheduler. */
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

/* Milestone 5/6/7 (parent-side IPC handler).
 * Purpose: apply one IPCMessage from a child to the parent's view of that
 * traveler, OR - for EVENT_VACATE (MS7 only) - treat it as a pure
 * scheduling signal rather than a position update.
 * Parameters: traveler - the parent's Traveler struct to update; msg -
 * message just read from that traveler's pipe.
 * Returns: nothing.
 * Side effects: mutates *traveler; for EVENT_ARRIVE, also prints a status
 * line; for EVENT_VACATE under MS7, calls scheduleNext() (which may wake
 * another child process via sem_post). */
static void updateTravelerFromMessage(Traveler *traveler, const IPCMessage *msg) {
    if (msg->event == EVENT_VACATE) {
#ifdef MS7
        /* EVENT_VACATE -> scheduleNext() handshake: the child already
         * left msg->current_node (that field is reused to carry the
         * vacated node id here, not an actual position), so the parent
         * immediately lets the scheduler hand it to the next waiter, if
         * any. Returning here without touching `traveler` is deliberate:
         * this message carries no real x/y/state for the traveler. */
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
/* Milestone 5/6/7 (parent-side IPC handler).
 * Purpose: drain every open traveler pipe of all currently-available
 * messages, once per GUI frame, without ever blocking the render loop.
 * Parameters: pipes - traveler_count pairs of [read_fd, write_fd] (only
 * pipes[i][0], the read end, is used here); travelers - parent's
 * traveler state to update; traveler_count - number of travelers;
 * pipe_open - in/out flags tracking which pipes are still open.
 * Returns: nothing.
 * Side effects: reads from each open pipe (non-blocking, set up by
 * setNonBlocking in runStage5or6/runStage7); may close a pipe and clear
 * its pipe_open flag once the child exits (read returns 0 = EOF); mutates
 * travelers[] via updateTravelerFromMessage.
 *
 * The inner while(true) loop keeps reading from a given pipe until there
 * is genuinely nothing left to read (EAGAIN/EWOULDBLOCK) rather than just
 * reading one message per frame - a fast-moving child can otherwise queue
 * up several messages between frames, and reading only one would make the
 * GUI lag behind real progress. EINTR retries the same read; EOF (0
 * bytes) means the child closed its write end (process exited), so the
 * pipe is marked closed and not polled again. */
static void handlePipeReads(int pipes[][2], Traveler travelers[], int traveler_count, bool pipe_open[]) {
    for (int i = 0; i < traveler_count; i++) {
        if (!pipe_open[i]) continue;
        while (true) {
            IPCMessage msg;
            ssize_t bytes = read(pipes[i][0], &msg, sizeof(msg));
            if (bytes == (ssize_t)sizeof(msg)) {
                updateTravelerFromMessage(&travelers[i], &msg);
                continue;
            }
            if (bytes == 0) {
                pipe_open[i] = false;
                close(pipes[i][0]);
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

/* Milestone 2/3/4/5/6/7 (shared GUI rendering).
 * Purpose: draw every edge of the graph (as an arrow with its weight
 * label) and every node (as a labeled circle).
 * Parameters: g - graph to draw; positions - screen position of every node.
 * Returns: nothing.
 * Side effects: draws to the screen. */
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

/* Milestone 2/3/4/5/6/7 (shared GUI rendering).
 * Purpose: draw every traveler as a small colored circle (yellow while
 * WAITING/BLOCKED, gray once FINISHED, with a pulsing halo while BLOCKED)
 * plus its "T<id>" label.
 * Parameters: travelers - traveler array; traveler_count - number of travelers.
 * Returns: nothing.
 * Side effects: draws to the screen; reads GetTime() to drive the blink. */
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
/* Milestone 2/3/4.
 * Purpose: advance one traveler's position/state by one frame along its
 * precomputed path, alternating ACTIVE (animating across an edge) and
 * WAITING (paused at an intermediate node) states, using wall-clock time
 * deltas rather than a fixed per-frame step.
 * Parameters: traveler - traveler to update; g - graph (for edge
 * weights); positions - screen position of every node.
 * Returns: nothing.
 * Side effects: mutates *traveler (position, state, path_index,
 * jump_count, last_time, finished); reads GetTime(). */
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
        /* Zero-weight edge: skip straight to `next` with no animation. */
        traveler->path_index++;
        traveler->current_node = next;
        traveler->next_node = (traveler->path_index < traveler->path_len - 1) ? traveler->path[traveler->path_index + 1] : -1;
        traveler->jump_count = 0;
        traveler->state = STATE_WAITING;
        traveler->last_time = now;
        return;
    }

    if (traveler->state == STATE_ACTIVE) {
        /* Advance jump_count by one "tick" every JUMP_TIME seconds of
         * wall-clock time, and use jump_count/weight as the interpolation
         * ratio along the current edge. */
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
            /* Reached `next`: switch to WAITING so the node-pause delay
             * below runs before continuing to the following edge. */
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
/* Milestone 2/3/4.
 * Purpose: top-level run loop for MS2/MS3/MS4 - precompute every
 * traveler's path in the parent, fork one (IPC-less) child per traveler
 * just to occupy a pid for the duration, then open a raylib window and
 * animate every traveler's movement directly in the parent until all are
 * finished or the window is closed.
 * Parameters: g - graph; defs - traveler source/destination list;
 * traveler_count - number of travelers; positions - screen position of
 * every node.
 * Returns: nothing.
 * Side effects: forks traveler_count children (childProcessStage4);
 * opens/closes a raylib window; draws every frame; waitpid()'s on every
 * child before returning. */
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

/* ===================== MILESTONES 5/6: PIPE-DRIVEN ANIMATION LOOP ===================== */

#if defined(MS5) || defined(MS6)
/* Milestone 5 (base behavior) / Milestone 6 (adds initNodeSemaphores/
 * destroyNodeSemaphores around this same function).
 * Purpose: top-level run loop for MS5/MS6 - set up (MS6 only) the
 * per-node semaphores, fork one childProcessStage5or6 per traveler
 * connected by a pipe, then open a raylib window and, every frame, drain
 * each pipe and render whatever state the children have reported, until
 * all travelers are finished or the window is closed.
 * Parameters: g - graph; defs - traveler source/destination list;
 * traveler_count - number of travelers; positions - screen position of
 * every node.
 * Returns: nothing (returns early without animating if MS6's semaphore
 * setup fails).
 * Side effects: under MS6, mmap's the node semaphores; creates a pipe and
 * forks a child per traveler; opens/closes a raylib window; draws every
 * frame; waitpid()'s on every child and closes any still-open pipes; under
 * MS6, destroys the node semaphores before returning. */
static void runStage5or6(Graph *g, TravelerDef defs[], int traveler_count, Vector2 positions[]) {
#ifdef MS6
    if (!initNodeSemaphores(g->n)) {
        return;
    }
#endif
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
            childProcessStage5or6(i, defs[i], g, pipes[i][1], positions);
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

    InitWindow(SCREEN_W, SCREEN_H, "Flight Path Simulation - Milestone 5/6");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        handlePipeReads(pipes, travelers, traveler_count, pipe_open);
        bool anyActive = false;
        for (int i = 0; i < traveler_count; i++) {
            if (!travelers[i].finished) anyActive = true;
        }

        BeginDrawing();
        ClearBackground((Color){18, 24, 40, 255});
        DrawText("Flight Path Simulation - Milestone 5/6", 16, 16, 20, LIGHTGRAY);
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
#ifdef MS6
    destroyNodeSemaphores(g->n);
#endif
}
#endif

/* ===================== MILESTONE 7: SCHEDULED ANIMATION LOOP ===================== */

#ifdef MS7
/* Milestone 7.
 * Purpose: top-level run loop for MS7 - same shape as runStage5or6, but
 * sets up the NodeQueue scheduler instead of plain semaphores, forks
 * childProcessStage7 per traveler, and additionally displays which
 * scheduling policy (FCFS/SJF) is active.
 * Parameters: g - graph; defs - traveler source/destination list;
 * traveler_count - number of travelers; positions - screen position of
 * every node.
 * Returns: nothing (returns early without animating if initNodeQueues fails).
 * Side effects: mmap's the node queues; creates a pipe and forks a child
 * per traveler; opens/closes a raylib window; draws every frame (including
 * scheduling-policy text); waitpid()'s on every child and closes any
 * still-open pipes; destroys the node queues before returning. */
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
        handlePipeReads(pipes, travelers, traveler_count, pipe_open);
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

/* Milestone 2/3/4/5/6/7 (entry point).
 * Purpose: parse command-line arguments (file path, plus MS7's optional
 * "-schd fcfs|sjf" scheduler selector), load the graph/traveler file,
 * lay out node positions, and dispatch to the milestone-appropriate run
 * loop.
 * Parameters: argc, argv - standard C main arguments; usage:
 *   ./sim <file_name>                              (all milestones)
 *   ./sim -schd fcfs|sjf <file_name>                (MS7 only)
 * Returns: 0 on success; 1 on bad usage, a bad input file, or zero
 * travelers loaded.
 * Side effects: reads the input file via loadGraph; dispatches to
 * runStage4/runStage5or6/runStage7, which open a raylib window, fork
 * children, etc. */
int main(int argc, char *argv[]) {
    const char *file;

#ifdef MS7
    /* MS7 accepts either "<file>" or "-schd fcfs|sjf <file>"; any other
     * argument count/shape is a usage error. g_sched defaults to FCFS
     * (set at its declaration) when -schd is not given. */
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
    /* MS2/MS3/MS4/MS5/MS6: only "<file>" is accepted. */
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

    /* Lay every node out evenly around a circle centered on the window
     * (nodePosition), once, regardless of milestone. */
    Vector2 positions[MAX_NODES];
    float cx = SCREEN_W / 2.0f;
    float cy = SCREEN_H / 2.0f - 30.0f;
    float radius = (SCREEN_H < SCREEN_W ? SCREEN_H : SCREEN_W) * 0.36f;
    for (int i = 0; i < graph.n; i++) {
        positions[i] = nodePosition(i, graph.n, cx, cy, radius);
    }

    /* Milestone dispatch: exactly one of these run loops executes,
     * selected entirely by which -DMSx flag this translation unit was
     * compiled with (see the build/milestone legend at the top of this
     * file). MS2/MS3 (no flag) fall into the same "#else" as MS4. */
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

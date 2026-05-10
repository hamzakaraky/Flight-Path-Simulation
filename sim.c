/*
 * Milestone 3 – Graph GUI Visualization & Animation
 * OS Project – Directed Weighted Graph Simulation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <raylib.h>

/* ─── Constants ─────────────────────────────────────────────── */
#define INF          1000000000
#define MAX_NODES    15
#define SCREEN_W     1100
#define SCREEN_H     750
#define NODE_RADIUS  26
#define ARROW_HEAD   12
#define PI           3.14159265358979323846f

/* Animation Constants */
#define JUMP_TIME    0.3f  /* 300ms per jump */
#define WAIT_TIME    1.0f  /* 1 second wait at nodes */

/* ─── Structures ─────────────────────────────────────────────── */
typedef struct {
    int n, m;
    int weight[MAX_NODES][MAX_NODES];
} Graph;

typedef struct { float x, y; } Vec2;

/* Animation States */
typedef enum {
    ANIM_IDLE,
    ANIM_JUMPING,
    ANIM_WAITING,
    ANIM_FINISHED,
    ANIM_NO_PATH
} AnimState;

/* ─── Dijkstra ───────────────────────────────────────────────── */
static int minDist(int dist[], int visited[], int n) {
    int min = INF, idx = -1;
    for (int i = 0; i < n; i++)
        if (!visited[i] && dist[i] < min) { min = dist[i]; idx = i; }
    return idx;
}

static int dijkstra(Graph *g, int src, int dst, int parent[]) {
    int dist[MAX_NODES], visited[MAX_NODES];
    for (int i = 0; i < g->n; i++) { dist[i] = INF; visited[i] = 0; parent[i] = -1; }
    dist[src] = 0;

    for (int c = 0; c < g->n; c++) {
        int u = minDist(dist, visited, g->n);
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

static void markPath(int parent[], int dst, bool onPath[][MAX_NODES]) {
    int cur = dst;
    while (parent[cur] != -1) {
        onPath[parent[cur]][cur] = true;
        cur = parent[cur];
    }
}

/* ─── Geometry helpers ───────────────────────────────────────── */
static Vec2 nodePos(int idx, int n, float cx, float cy, float r) {
    float angle = (float)idx / (float)n * 2.0f * PI - PI / 2.0f;
    return (Vec2){ cx + r * cosf(angle), cy + r * sinf(angle) };
}

static void drawArrow(Vec2 p1, Vec2 p2, Color color) {
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len;

    Vec2 a = { p1.x + ux * NODE_RADIUS, p1.y + uy * NODE_RADIUS };
    Vec2 b = { p2.x - ux * NODE_RADIUS, p2.y - uy * NODE_RADIUS };

    DrawLineEx((Vector2){ a.x, a.y }, (Vector2){ b.x, b.y }, 2.5f, color);

    float angle = atan2f(uy, ux);
    float spread = 0.42f; 
    Vector2 tip  = { b.x, b.y };
    Vector2 left = { b.x - ARROW_HEAD * cosf(angle - spread),
                     b.y - ARROW_HEAD * sinf(angle - spread) };
    Vector2 right= { b.x - ARROW_HEAD * cosf(angle + spread),
                     b.y - ARROW_HEAD * sinf(angle + spread) };
    DrawTriangle(tip, left, right, color);
}

/* ─── Bold text helper (fake bold + black outline) ───────────── */
static void drawBoldText(const char *text, int x, int y, int fontSize, Color color) {
    /* Draw black outline 8 directions around */
    DrawText(text, x - 1, y,     fontSize, BLACK);
    DrawText(text, x + 1, y,     fontSize, BLACK);
    DrawText(text, x,     y - 1, fontSize, BLACK);
    DrawText(text, x,     y + 1, fontSize, BLACK);
    DrawText(text, x - 1, y - 1, fontSize, BLACK);
    DrawText(text, x + 1, y - 1, fontSize, BLACK);
    DrawText(text, x - 1, y + 1, fontSize, BLACK);
    DrawText(text, x + 1, y + 1, fontSize, BLACK);
    /* Draw the actual text on top */
    DrawText(text, x, y, fontSize, color);
}

/* ─── Load graph ─────────────────────────────────────────────── */
static bool loadGraph(const char *path, Graph *g, int *src, int *dst) {
    FILE *f = fopen(path, "r");
    if (!f) { printf("Error opening file\n"); return false; }

    if (fscanf(f, "%d %d", &g->n, &g->m) != 2 || g->n <= 0 || g->m < 0 || g->n > MAX_NODES) {
        printf("Invalid input\n"); fclose(f); return false;
    }

    for (int i = 0; i < g->n; i++)
        for (int j = 0; j < g->n; j++)
            g->weight[i][j] = INF;

    for (int i = 0; i < g->m; i++) {
        int s, d, w;
        if (fscanf(f, "%d %d %d", &s, &d, &w) != 3 || s < 0 || d < 0 || w < 0 || s >= g->n || d >= g->n) {
            printf("Invalid input\n"); fclose(f); return false;
        }
        g->weight[s][d] = w;
    }

    if (fscanf(f, "%d %d", src, dst) != 2 || *src < 0 || *dst < 0 || *src >= g->n || *dst >= g->n) {
        printf("Invalid input\n"); fclose(f); return false;
    }

    fclose(f);
    return true;
}

/* ─── Main ───────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc != 2) { printf("Usage: ./sim <file_name>\n"); return 1; }

    Graph g;
    int qSrc, qDst;
    if (!loadGraph(argv[1], &g, &qSrc, &qDst)) return 1;

    int parent[MAX_NODES];
    bool onPath[MAX_NODES][MAX_NODES] = {0};
    int totalDist = dijkstra(&g, qSrc, qDst, parent);
    bool pathFound = (totalDist != INF || qSrc == qDst);
    
    if (pathFound && qSrc != qDst) markPath(parent, qDst, onPath);

    /* Build Forward Path Array for Animation */
    int pathArr[MAX_NODES];
    int pathLen = 0;
    if (pathFound) {
        int cur = qDst;
        while (cur != -1) { pathArr[pathLen++] = cur; cur = parent[cur]; }
        /* Reverse array */
        for (int i = 0; i < pathLen / 2; i++) {
            int temp = pathArr[i];
            pathArr[i] = pathArr[pathLen - 1 - i];
            pathArr[pathLen - 1 - i] = temp;
        }
    }

    InitWindow(SCREEN_W, SCREEN_H, "OS Project – Graph Visualizer & Animation (Milestone 3)");
    SetTargetFPS(60);

    float cx = SCREEN_W / 2.0f;
    float cy = SCREEN_H / 2.0f - 20.0f;
    float r  = (SCREEN_H < SCREEN_W ? SCREEN_H : SCREEN_W) * 0.33f;
    Vec2 pos[MAX_NODES];
    for (int i = 0; i < g.n; i++) pos[i] = nodePos(i, g.n, cx, cy, r);

    Color bgColor      = { 18,  18,  35, 255 };  
    Color edgeColor    = { 140, 140, 160, 255 };  
    Color pathColor    = { 255, 210,  50, 255 };  
    Color nodeColor    = {  55,  90, 180, 255 };  
    Color srcNodeColor = {  40, 180,  90, 255 };  
    Color dstNodeColor = { 220,  60,  60, 255 };  
    Color nodeText     = WHITE;
    Color weightColor  = { 210, 230, 255, 255 };  
    Color pathEdgeW    = { 255, 235, 120, 255 };
    Color entityColor  = { 255, 100, 200, 255 };  /* Pink for moving entity */

    /* Animation Variables */
    AnimState animState = ANIM_IDLE;
    if (!pathFound) animState = ANIM_NO_PATH;
    else if (qSrc == qDst) animState = ANIM_FINISHED;

    bool isPlaying = false;
    int pathIdx = 0;     /* Current position in pathArr */
    int jumpCount = 0;   /* Jumps completed on current edge */
    double lastTime = GetTime();

    Rectangle btnPlay = { SCREEN_W - 140, 20, 110, 40 };

    while (!WindowShouldClose()) {
        double currentTime = GetTime();

        /* --- Handle Input --- */
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (CheckCollisionPointRec(GetMousePosition(), btnPlay)) {
                if (animState != ANIM_FINISHED && animState != ANIM_NO_PATH) {
                    isPlaying = !isPlaying;
                    lastTime = currentTime; /* Reset timer when toggled */
                }
            }
        }

        /* --- Animation Logic --- */
        if (isPlaying && pathLen > 1) {
            if (animState == ANIM_IDLE) {
                animState = ANIM_JUMPING;
                lastTime = currentTime;
                jumpCount = 1;
            } 
            else if (animState == ANIM_JUMPING) {
                if (currentTime - lastTime >= JUMP_TIME) {
                    lastTime = currentTime;
                    int u = pathArr[pathIdx];
                    int v = pathArr[pathIdx + 1];
                    int w = g.weight[u][v];

                    if (jumpCount < w) {
                        jumpCount++;
                    } else {
                        /* Reached next node */
                        pathIdx++;
                        if (pathIdx == pathLen - 1) {
                            animState = ANIM_FINISHED;
                            isPlaying = false;
                        } else {
                            animState = ANIM_WAITING;
                        }
                    }
                }
            }
            else if (animState == ANIM_WAITING) {
                if (currentTime - lastTime >= WAIT_TIME) {
                    lastTime = currentTime;
                    animState = ANIM_JUMPING;
                    jumpCount = 1;
                }
            }
        }

        /* --- Drawing --- */
        BeginDrawing();
        ClearBackground(bgColor);
        DrawText("Graph Animation – Milestone 3", 16, 12, 20, LIGHTGRAY);

        /* Draw Play/Stop Button */
        Color btnColor = isPlaying ? MAROON : DARKGREEN;
        if (animState == ANIM_FINISHED || animState == ANIM_NO_PATH) btnColor = GRAY;
        DrawRectangleRec(btnPlay, btnColor);
        DrawText(isPlaying ? "STOP" : "PLAY", btnPlay.x + 30, btnPlay.y + 10, 20, WHITE);

        /* Draw Edges */
        for (int u = 0; u < g.n; u++) {
            for (int v = 0; v < g.n; v++) {
                if (g.weight[u][v] == INF) continue;
                bool highlight = pathFound && onPath[u][v];
                drawArrow(pos[u], pos[v], highlight ? pathColor : edgeColor);

                float mx = (pos[u].x + pos[v].x) / 2.0f;
                float my = (pos[u].y + pos[v].y) / 2.0f;
                char wBuf[16];
                snprintf(wBuf, sizeof(wBuf), "%d", g.weight[u][v]);

                int fontSize = 26;
                int textW = MeasureText(wBuf, fontSize);
                int tx = (int)(mx - textW / 2);
                int ty = (int)(my - fontSize / 2);

                drawBoldText(wBuf, tx, ty, fontSize, highlight ? pathEdgeW : weightColor);
            }
        }

        /* Draw Nodes */
        for (int i = 0; i < g.n; i++) {
            Color nc = nodeColor;
            if (i == qSrc) nc = srcNodeColor;
            if (i == qDst) nc = dstNodeColor;
            if (i == qSrc && i == qDst) nc = pathColor;

            if (i == qSrc || i == qDst) {
                Color ring = nc; ring.a = 80;
                DrawCircle((int)pos[i].x, (int)pos[i].y, NODE_RADIUS + 8, ring);
            }
            DrawCircle((int)pos[i].x, (int)pos[i].y, NODE_RADIUS, nc);
            DrawCircleLines((int)pos[i].x, (int)pos[i].y, NODE_RADIUS, WHITE);

            char label[8];
            snprintf(label, sizeof(label), "%d", i);
            DrawText(label, (int)(pos[i].x - MeasureText(label, 20) / 2), (int)(pos[i].y - 10), 20, nodeText);
        }

        /* Draw Moving Entity */
        if (pathFound) {
            Vec2 ePos = pos[qSrc]; /* Default to source */
            
            if (animState == ANIM_FINISHED) {
                ePos = pos[qDst];
            } 
            else if (animState == ANIM_WAITING) {
                ePos = pos[pathArr[pathIdx]];
            }
            else if (animState == ANIM_JUMPING) {
                int u = pathArr[pathIdx];
                int v = pathArr[pathIdx + 1];
                int w = g.weight[u][v];
                float fraction = (float)jumpCount / w;
                if (fraction > 1.0f) fraction = 1.0f;
                
                ePos.x = pos[u].x + fraction * (pos[v].x - pos[u].x);
                ePos.y = pos[u].y + fraction * (pos[v].y - pos[u].y);
            }

            /* Draw the entity (Circle with an inner ring) */
            DrawCircle((int)ePos.x, (int)ePos.y, 16, entityColor);
            DrawCircleLines((int)ePos.x, (int)ePos.y, 16, WHITE);
            DrawCircle((int)ePos.x, (int)ePos.y, 6, WHITE);
        }

        /* --- Arrival Message --- */
        if (animState == ANIM_FINISHED && pathLen > 1) {
            int msgW = MeasureText("Destination Reached!", 30);
            DrawRectangle(SCREEN_W/2 - msgW/2 - 20, 80, msgW + 40, 50, Fade(BLACK, 0.7f));
            DrawText("Destination Reached!", SCREEN_W/2 - msgW/2, 90, 30, GREEN);
        }

        /* --- Info Panel (Bottom) --- */
        int panelY = SCREEN_H - 90;
        DrawRectangle(0, panelY, SCREEN_W, 90, (Color){ 10, 10, 25, 200 });
        DrawLine(0, panelY, SCREEN_W, panelY, DARKGRAY);

        DrawCircle(30, panelY + 22, 10, srcNodeColor); DrawText("Source", 46, panelY + 15, 16, LIGHTGRAY);
        DrawCircle(130, panelY + 22, 10, dstNodeColor); DrawText("Destination", 146, panelY + 15, 16, LIGHTGRAY);
        DrawRectangle(260, panelY + 14, 32, 6, pathColor); DrawText("Shortest path", 300, panelY + 15, 16, LIGHTGRAY);
        DrawCircle(440, panelY + 22, 10, entityColor); DrawText("Entity", 456, panelY + 15, 16, LIGHTGRAY);

        char info[128];
        if (!pathFound) snprintf(info, sizeof(info), "No path found from %d to %d", qSrc, qDst);
        else if (qSrc == qDst) snprintf(info, sizeof(info), "Source = Destination = %d | Distance: 0", qSrc);
        else snprintf(info, sizeof(info), "Total distance: %d | Jumps total time: %.1fs", totalDist, totalDist * JUMP_TIME);
        
        DrawText(info, 16, panelY + 46, 18, WHITE);
        
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
